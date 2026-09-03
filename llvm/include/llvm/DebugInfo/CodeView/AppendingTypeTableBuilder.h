//===- AppendingTypeTableBuilder.h -------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_APPENDINGTYPETABLEBUILDER_H
#define LLVM_DEBUGINFO_CODEVIEW_APPENDINGTYPETABLEBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/SimpleTypeSerializer.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {
/// CodeView debug-info types, records, and type/symbol stream builders.
namespace codeview {

class ContinuationRecordBuilder;

/// Builds a CodeView type table by appending records without merging duplicates.
class LLVM_ABI AppendingTypeTableBuilder : public TypeCollection {

  BumpPtrAllocator &RecordStorage;
  SimpleTypeSerializer SimpleSerializer;

  /// Contains a list of all records indexed by TypeIndex.toArrayIndex().
  SmallVector<ArrayRef<uint8_t>, 2> SeenRecords;

public:
  /// Construct a builder that stores serialized records in \p Storage.
  ///
  /// \param Storage Allocator that owns stable copies of appended records.
  explicit AppendingTypeTableBuilder(BumpPtrAllocator &Storage);
  /// Destroy the appending type table builder.
  ~AppendingTypeTableBuilder() override;

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
  /// \returns Always true on success.
  bool replaceType(TypeIndex &Index, CVType Data, bool Stabilize) override;

  // public interface
  /// Remove all appended type records from the table.
  void reset();
  /// Return the type index that will be assigned to the next appended record.
  ///
  /// \returns The type index that will be assigned to the next appended record.
  TypeIndex nextTypeIndex() const;

  /// Return the allocator used to store stable record bytes.
  ///
  /// \returns The allocator used to store stable record bytes.
  BumpPtrAllocator &getAllocator() { return RecordStorage; }

  /// Return the serialized bytes of every record in type-index order.
  ///
  /// \returns The serialized bytes of every record in type-index order.
  ArrayRef<ArrayRef<uint8_t>> records() const;
  /// Append a pre-serialized type record and return its type index.
  ///
  /// Copies \p Record into the builder's allocator and updates \p Record to
  /// refer to the stable storage.
  ///
  /// \param Record Serialized type record bytes to append.
  /// \returns The type index assigned to the appended record.
  TypeIndex insertRecordBytes(ArrayRef<uint8_t> &Record);
  /// Append the continuation fragments from \p Builder and return the last index.
  ///
  /// \param Builder Continuation record builder whose fragments are appended.
  /// \returns The type index of the last appended continuation fragment.
  TypeIndex insertRecord(ContinuationRecordBuilder &Builder);

  /// Serialize leaf type \p Record and append it to the table.
  ///
  /// \param Record Leaf type record to serialize and append.
  /// \returns The type index assigned to the appended record.
  template <typename T> TypeIndex writeLeafType(T &Record) {
    ArrayRef<uint8_t> Data = SimpleSerializer.serialize(Record);
    return insertRecordBytes(Data);
  }
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_APPENDINGTYPETABLEBUILDER_H
