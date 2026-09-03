//===- TypeTableCollection.h ---------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPETABLECOLLECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPETABLECOLLECTION_H

#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/StringSaver.h"

#include <vector>

namespace llvm {
namespace codeview {

/// A TypeCollection backed by a fixed table of serialized CodeView type records.
class LLVM_ABI TypeTableCollection : public TypeCollection {
public:
  /// Construct a collection over the serialized type records in \p Records.
  ///
  /// \param Records Array of serialized CodeView type record byte ranges.
  explicit TypeTableCollection(ArrayRef<ArrayRef<uint8_t>> Records);

  /// Return the first non-simple type index, or \c std::nullopt if none exist.
  ///
  /// \returns The first non-simple type index, or \c std::nullopt if none exist.
  std::optional<TypeIndex> getFirst() override;
  /// Return the type index after \p Prev, or \c std::nullopt at the end.
  ///
  /// \param Prev The preceding type index in iteration order.
  /// \returns The type index after \p Prev, or \c std::nullopt at the end.
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
  /// Return true if \p Index refers to a type record in the table.
  ///
  /// \param Index Type index to test for membership.
  /// \returns True if \p Index refers to a type record in the table.
  bool contains(TypeIndex Index) override;
  /// Return the number of type records in the table.
  ///
  /// \returns The number of type records in the table.
  uint32_t size() override;
  /// Return the capacity of the underlying type record table.
  ///
  /// \returns The capacity of the underlying type record table.
  uint32_t capacity() override;
  /// Replace the record at \p Index with \p Data.
  ///
  /// \param Index Type index of the record to replace; must already exist.
  /// \param Data New record bytes to store at \p Index.
  /// \param Stabilize If true, copy \p Data into stable storage.
  /// \returns Whether the replacement succeeded.
  bool replaceType(TypeIndex &Index, CVType Data, bool Stabilize) override;

private:
  BumpPtrAllocator Allocator;
  StringSaver NameStorage;
  std::vector<StringRef> Names;
  ArrayRef<ArrayRef<uint8_t>> Records;
};
}
}

#endif
