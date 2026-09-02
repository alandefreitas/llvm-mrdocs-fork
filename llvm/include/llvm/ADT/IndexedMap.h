//===- llvm/ADT/IndexedMap.h - An index map implementation ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements an indexed map. The index map template takes two
/// types. The first is the mapped type and the second is a functor
/// that maps its argument to a size_t. On instantiation a "null" value
/// can be provided to be used as a "does not exist" indicator in the
/// map. A member function grow() is provided that given the value of
/// the maximally indexed key (the argument of the functor) makes sure
/// the map has enough space for it.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_INDEXEDMAP_H
#define LLVM_ADT_INDEXEDMAP_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>

namespace llvm {

namespace detail {
template <class Ty> struct IdentityIndex {
  using argument_type = Ty;

  Ty &operator()(Ty &self) const { return self; }
  const Ty &operator()(const Ty &self) const { return self; }
};
} // namespace detail

/// Dense map from keys to values, indexed through a key-to-index functor.
///
/// Keys are mapped to \c size_t slots via \p ToIndexT. Missing entries are
/// represented by a configurable null value. Call \c grow to ensure capacity
/// for a given key.
template <typename T, typename ToIndexT = detail::IdentityIndex<unsigned>>
class IndexedMap {
  using IndexT = typename ToIndexT::argument_type;
  // Prefer SmallVector with zero inline storage over std::vector. IndexedMaps
  // can grow very large and SmallVector grows more efficiently as long as T
  // is trivially copyable.
  using StorageT = SmallVector<T, 0>;

  StorageT Storage;
  T NullVal = T();
  ToIndexT ToIndex;

public:
  /// Construct an empty map whose null sentinel is default-constructed \p T.
  IndexedMap() = default;

  /// Construct an empty map that uses \p Val as the null / missing sentinel.
  explicit IndexedMap(const T &Val) : NullVal(Val) {}

  /// Return a mutable reference to the value at key \p N.
  ///
  /// \pre \c inBounds(N) is true.
  typename StorageT::reference operator[](IndexT N) {
    assert(ToIndex(N) < Storage.size() && "index out of bounds!");
    return Storage[ToIndex(N)];
  }

  /// Return a const reference to the value at key \p N.
  ///
  /// \pre \c inBounds(N) is true.
  typename StorageT::const_reference operator[](IndexT N) const {
    assert(ToIndex(N) < Storage.size() && "index out of bounds!");
    return Storage[ToIndex(N)];
  }

  /// Reserve storage capacity for at least \p S mapped slots.
  void reserve(typename StorageT::size_type S) { Storage.reserve(S); }

  /// Resize to \p S slots, filling new entries with the null sentinel.
  void resize(typename StorageT::size_type S) { Storage.resize(S, NullVal); }

  /// Remove all entries, leaving the map empty.
  void clear() { Storage.clear(); }

  /// Ensure the map has space for key \p N (and all smaller indices).
  void grow(IndexT N) {
    unsigned NewSize = ToIndex(N) + 1;
    if (NewSize > Storage.size())
      resize(NewSize);
  }

  /// Return true if key \p N falls within the current storage size.
  bool inBounds(IndexT N) const { return ToIndex(N) < Storage.size(); }

  /// Return the number of indexed slots currently allocated.
  typename StorageT::size_type size() const { return Storage.size(); }
};

} // namespace llvm

#endif // LLVM_ADT_INDEXEDMAP_H
