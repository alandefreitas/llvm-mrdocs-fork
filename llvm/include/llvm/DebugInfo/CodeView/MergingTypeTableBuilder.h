//===- MergingTypeTableBuilder.h ---------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_MERGINGTYPETABLEBUILDER_H
#define LLVM_DEBUGINFO_CODEVIEW_MERGINGTYPETABLEBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/SimpleTypeSerializer.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {
namespace codeview {
struct LocallyHashedType;

class ContinuationRecordBuilder;

/// Builds a CodeView type table that merges duplicate records by local hash.
class LLVM_ABI MergingTypeTableBuilder : public TypeCollection {
  /// Storage for records.  These need to outlive the TypeTableBuilder.
  BumpPtrAllocator &RecordStorage;

  /// A serializer that can write non-continuation leaf types.  Only used as
  /// a convenience function so that we can provide an interface method to
  /// write an unserialized record.
  SimpleTypeSerializer SimpleSerializer;

  /// Hash table.
  DenseMap<LocallyHashedType, TypeIndex> HashedRecords;

  /// Contains a list of all records indexed by TypeIndex.toArrayIndex().
  SmallVector<ArrayRef<uint8_t>, 2> SeenRecords;

public:
  /// Construct a builder that stores merged records in \p Storage.
  ///
  /// \param Storage Allocator that owns stable copies of inserted records.
  explicit MergingTypeTableBuilder(BumpPtrAllocator &Storage);
  /// Destroy the merging type table builder.
  ~MergingTypeTableBuilder() override;

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
  /// \returns True if \p Index refers to a record stored in this table.
  bool contains(TypeIndex Index) override;
  /// Return the number of unique type records currently stored.
  ///
  /// \returns The number of unique type records currently stored.
  uint32_t size() override;
  /// Return the number of type records the table can hold without growing.
  ///
  /// \returns The number of type records the table can hold without growing.
  uint32_t capacity() override;
  /// Replace the record at \p Index with \p Data, merging if a duplicate exists.
  ///
  /// If an equivalent record is already present, updates \p Index to that
  /// existing type index and returns false. Otherwise stores \p Data at
  /// \p Index and returns true.
  ///
  /// \param Index Type index of the record to replace; must already exist.
  /// \param Data New record bytes to store at \p Index.
  /// \param Stabilize If true, copy \p Data into the builder's allocator.
  /// \returns False if a duplicate was found; true if the replacement succeeded.
  bool replaceType(TypeIndex &Index, CVType Data, bool Stabilize) override;

  // public interface
  /// Remove all hashed type records from the table.
  void reset();
  /// Return the type index that will be assigned to the next new unique record.
  ///
  /// \returns The type index that will be assigned to the next new unique record.
  TypeIndex nextTypeIndex() const;

  /// Return the allocator used to store stable record bytes.
  ///
  /// \returns The allocator used to store stable record bytes.
  BumpPtrAllocator &getAllocator() { return RecordStorage; }

  /// Return the serialized bytes of every unique record in type-index order.
  ///
  /// \returns The serialized bytes of every unique record in type-index order.
  ArrayRef<ArrayRef<uint8_t>> records() const;

  /// Insert \p Record under \p Hash, merging with an existing duplicate if any.
  ///
  /// On return, \p Record refers to the stable stored bytes for the resulting
  /// type index.
  ///
  /// \param Hash Local hash used to look up and merge equivalent records.
  /// \param Record Serialized type record bytes to insert or merge.
  /// \returns The type index of the existing or newly inserted record.
  TypeIndex insertRecordAs(hash_code Hash, ArrayRef<uint8_t> &Record);
  /// Insert \p Record, hashing its bytes and merging duplicates.
  ///
  /// Copies \p Record into the builder's allocator when newly inserted, and
  /// updates \p Record to refer to the stable storage.
  ///
  /// \param Record Serialized type record bytes to insert or merge.
  /// \returns The type index of the existing or newly inserted record.
  TypeIndex insertRecordBytes(ArrayRef<uint8_t> &Record);
  /// Insert the continuation fragments from \p Builder, merging each fragment.
  ///
  /// \param Builder Continuation record builder whose fragments are inserted.
  /// \returns The type index of the last fragment after insertion or merge.
  TypeIndex insertRecord(ContinuationRecordBuilder &Builder);

  /// Serialize leaf type \p Record and insert it, merging duplicates.
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

#endif // LLVM_DEBUGINFO_CODEVIEW_MERGINGTYPETABLEBUILDER_H
