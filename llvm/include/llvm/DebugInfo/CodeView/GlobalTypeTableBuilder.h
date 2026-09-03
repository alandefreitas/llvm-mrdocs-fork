//===- GlobalTypeTableBuilder.h ----------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_GLOBALTYPETABLEBUILDER_H
#define LLVM_DEBUGINFO_CODEVIEW_GLOBALTYPETABLEBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/SimpleTypeSerializer.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeHashing.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>

namespace llvm {
namespace codeview {

class ContinuationRecordBuilder;

/// Builds a CodeView type table that merges records by globally unique hash.
class LLVM_ABI GlobalTypeTableBuilder : public TypeCollection {
  /// Storage for records.  These need to outlive the TypeTableBuilder.
  BumpPtrAllocator &RecordStorage;

  /// A serializer that can write non-continuation leaf types.  Only used as
  /// a convenience function so that we can provide an interface method to
  /// write an unserialized record.
  SimpleTypeSerializer SimpleSerializer;

  /// Hash table.
  DenseMap<GloballyHashedType, TypeIndex> HashedRecords;

  /// Contains a list of all records indexed by TypeIndex.toArrayIndex().
  SmallVector<ArrayRef<uint8_t>, 2> SeenRecords;

  /// Contains a list of all hash values indexed by TypeIndex.toArrayIndex().
  SmallVector<GloballyHashedType, 2> SeenHashes;

public:
  /// Construct a builder that stores serialized records in \p Storage.
  ///
  /// \param Storage Allocator that owns stable copies of merged records.
  explicit GlobalTypeTableBuilder(BumpPtrAllocator &Storage);
  /// Destroy the global type table builder.
  ~GlobalTypeTableBuilder() override;

  // TypeCollection overrides
  /// Return the first non-simple type index, or \c std::nullopt if empty.
  ///
  /// \returns The first non-simple type index, or \c std::nullopt if empty.
  std::optional<TypeIndex> getFirst() override;
  /// Return the type index after \p Prev, or \c std::nullopt at the end.
  ///
  /// \param Prev The preceding type index in iteration order.
  /// \returns The next type index, or \c std::nullopt at the end.
  std::optional<TypeIndex> getNext(TypeIndex Prev) override;
  /// Return the CodeView type record at \p Index.
  ///
  /// \param Index Type index of the record to retrieve.
  /// \returns The CodeView type record at \p Index.
  CVType getType(TypeIndex Index) override;
  /// Return the name of the type at \p Index.
  ///
  /// \param Index Type index whose name is requested.
  /// \returns The name of the type at \p Index.
  StringRef getTypeName(TypeIndex Index) override;
  /// Return true if \p Index refers to a record stored in this table.
  ///
  /// \param Index Type index to test for membership.
  /// \returns True if \p Index refers to a record in this table.
  bool contains(TypeIndex Index) override;
  /// Return the number of type records currently stored.
  ///
  /// \returns The number of type records currently stored.
  uint32_t size() override;
  /// Return the number of type records the table can hold without growing.
  ///
  /// \returns The number of type records the table can hold without growing.
  uint32_t capacity() override;
  /// Replace the record at \p Index with \p Data.
  ///
  /// \param Index Type index of the record to replace; must already exist.
  /// \param Data New record bytes to store at \p Index.
  /// \param Stabilize If true, copy \p Data into the builder's allocator.
  /// \returns True if the replacement was stored; false if a matching hash
  ///     already mapped \p Index to another location.
  bool replaceType(TypeIndex &Index, CVType Data, bool Stabilize) override;

  // public interface
  /// Remove all records and hashes from the table.
  void reset();
  /// Return the type index that will be assigned to the next inserted record.
  ///
  /// \returns The type index that will be assigned to the next inserted record.
  TypeIndex nextTypeIndex() const;

  /// Return the allocator used to store stable record bytes.
  ///
  /// \returns The allocator used to store stable record bytes.
  BumpPtrAllocator &getAllocator() { return RecordStorage; }

  /// Return the serialized bytes of every record in type-index order.
  ///
  /// \returns The serialized bytes of every record in type-index order.
  ArrayRef<ArrayRef<uint8_t>> records() const;
  /// Return the globally unique hash of every record in type-index order.
  ///
  /// \returns The globally unique hash of every record in type-index order.
  ArrayRef<GloballyHashedType> hashes() const;

  /// Insert or reuse a record identified by \p Hash, creating bytes via \p Create.
  ///
  /// If \p Hash is new (or was previously deferred as NotTranslated), allocates
  /// \p RecordSize bytes and invokes \p Create to fill them. An empty result
  /// from \p Create defers insertion for a later pass.
  ///
  /// \param Hash Globally unique hash that keys the record for merging.
  /// \param RecordSize Size in bytes of the record buffer to allocate; must be
  ///     a multiple of 4 and less than UINT32_MAX.
  /// \param Create Callable that writes the record into a mutable buffer and
  ///     returns the stable bytes to store, or an empty array to defer.
  /// \returns The type index of the existing or newly inserted record.
  template <typename CreateFunc>
  TypeIndex insertRecordAs(GloballyHashedType Hash, size_t RecordSize,
                           CreateFunc Create) {
    assert(RecordSize < UINT32_MAX && "Record too big");
    assert(RecordSize % 4 == 0 &&
           "RecordSize is not a multiple of 4 bytes which will cause "
           "misalignment in the output TPI stream!");

    auto Result = HashedRecords.try_emplace(Hash, nextTypeIndex());

    if (LLVM_UNLIKELY(Result.second /*inserted*/ ||
                      Result.first->second.isSimple())) {
      uint8_t *Stable = RecordStorage.Allocate<uint8_t>(RecordSize);
      MutableArrayRef<uint8_t> Data(Stable, RecordSize);
      ArrayRef<uint8_t> StableRecord = Create(Data);
      if (StableRecord.empty()) {
        // Records with forward references into the Type stream will be deferred
        // for insertion at a later time, on the second pass.
        Result.first->getSecond() = TypeIndex(SimpleTypeKind::NotTranslated);
        return TypeIndex(SimpleTypeKind::NotTranslated);
      }
      if (Result.first->second.isSimple()) {
        assert(Result.first->second.getIndex() ==
               (uint32_t)SimpleTypeKind::NotTranslated);
        // On the second pass, update with index to remapped record. The
        // (initially misbehaved) record will now come *after* other records
        // resolved in the first pass, with proper *back* references in the
        // stream.
        Result.first->second = nextTypeIndex();
      }
      SeenRecords.push_back(StableRecord);
      SeenHashes.push_back(Hash);
    }

    return Result.first->second;
  }

  /// Insert a pre-serialized type record by global hash and return its index.
  ///
  /// \param Data Serialized type record bytes to insert or merge.
  /// \returns The type index of the existing or newly inserted record.
  TypeIndex insertRecordBytes(ArrayRef<uint8_t> Data);
  /// Insert the continuation fragments from \p Builder and return the last index.
  ///
  /// \param Builder Continuation record builder whose fragments are inserted.
  /// \returns The type index of the last inserted continuation fragment.
  TypeIndex insertRecord(ContinuationRecordBuilder &Builder);

  /// Serialize leaf type \p Record and insert it into the table.
  ///
  /// \param Record Leaf type record to serialize and insert.
  /// \returns The type index of the existing or newly inserted record.
  template <typename T> TypeIndex writeLeafType(T &Record) {
    ArrayRef<uint8_t> Data = SimpleSerializer.serialize(Record);
    return insertRecordBytes(Data);
  }
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_GLOBALTYPETABLEBUILDER_H
