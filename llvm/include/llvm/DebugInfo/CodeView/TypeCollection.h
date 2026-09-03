//===- TypeCollection.h - A collection of CodeView type records -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPECOLLECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPECOLLECTION_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"

namespace llvm {
namespace codeview {
/// Interface for a random-access collection of CodeView type records.
class TypeCollection {
public:
  /// Destroy a type collection.
  virtual ~TypeCollection() = default;

  /// Return true if the collection contains no type records.
  ///
  /// \returns True if the collection contains no type records.
  bool empty() { return size() == 0; }

  /// Return the first type index in the collection, or \c std::nullopt if empty.
  ///
  /// \returns The first type index, or \c std::nullopt if empty.
  virtual std::optional<TypeIndex> getFirst() = 0;
  /// Return the type index after \p Prev, or \c std::nullopt at the end.
  ///
  /// \param Prev The preceding type index in iteration order.
  /// \returns The type index after \p Prev, or \c std::nullopt at the end.
  virtual std::optional<TypeIndex> getNext(TypeIndex Prev) = 0;

  /// Return the CodeView type record at \p Index.
  ///
  /// \param Index Type index of the record to retrieve.
  /// \returns The CodeView type record at \p Index.
  virtual CVType getType(TypeIndex Index) = 0;
  /// Return the name of the type at \p Index.
  ///
  /// \param Index Type index whose name is requested.
  /// \returns The name of the type at \p Index.
  virtual StringRef getTypeName(TypeIndex Index) = 0;
  /// Return true if \p Index refers to a type record in the collection.
  ///
  /// \param Index Type index to test for membership.
  /// \returns True if \p Index refers to a type record in the collection.
  virtual bool contains(TypeIndex Index) = 0;
  /// Return the number of type records in the collection.
  ///
  /// \returns The number of type records in the collection.
  virtual uint32_t size() = 0;
  /// Return the number of type records the collection can hold without growing.
  ///
  /// \returns The number of type records the collection can hold without growing.
  virtual uint32_t capacity() = 0;
  /// Replace the record at \p Index with \p Data.
  ///
  /// \param Index Type index of the record to replace; must already exist.
  /// \param Data New record bytes to store at \p Index.
  /// \param Stabilize If true, copy \p Data into stable storage.
  /// \returns Whether the replacement succeeded.
  virtual bool replaceType(TypeIndex &Index, CVType Data, bool Stabilize) = 0;

  /// Invoke \p Func for each type record in the collection, in index order.
  ///
  /// \tparam TFunc Callable invoked as \c Func(TypeIndex, CVType) for each record.
  /// \param Func Callback receiving each type index and its corresponding record.
  template <typename TFunc> void ForEachRecord(TFunc Func) {
    std::optional<TypeIndex> Next = getFirst();

    while (Next) {
      TypeIndex N = *Next;
      Func(N, getType(N));
      Next = getNext(N);
    }
  }
};
}
}

#endif
