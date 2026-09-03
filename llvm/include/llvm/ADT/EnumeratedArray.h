//===- llvm/ADT/EnumeratedArray.h - Enumerated Array-------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines an array type that can be indexed using scoped enum
/// values.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ENUMERATEDARRAY_H
#define LLVM_ADT_ENUMERATEDARRAY_H

#include "llvm/ADT/STLExtras.h"
#include <array>
#include <cassert>

namespace llvm {

/// Fixed-size array indexed by values of a scoped enumeration.
///
/// The array holds one element per enumerator from zero through
/// \p LargestEnum (inclusive). Indexing uses the enumeration type rather than
/// a raw integer.
///
/// @tparam ValueType Element type stored in the array.
/// @tparam Enumeration Scoped enum used as the index type.
/// @tparam LargestEnum Last enumerator; determines the array size.
/// @tparam IndexType Underlying integer type used for indexing and size.
/// @tparam Size Number of elements (defaults to \c LargestEnum + 1).
template <typename ValueType, typename Enumeration,
          Enumeration LargestEnum = Enumeration::Last, typename IndexType = int,
          IndexType Size = 1 + static_cast<IndexType>(LargestEnum)>
class EnumeratedArray {
  static_assert(Size > 0);
  using ArrayTy = std::array<ValueType, Size>;
  ArrayTy Underlying;

public:
  /// Mutable iterator over the stored elements.
  using iterator = typename ArrayTy::iterator;
  /// Const iterator over the stored elements.
  using const_iterator = typename ArrayTy::const_iterator;
  /// Mutable reverse iterator over the stored elements.
  using reverse_iterator = typename ArrayTy::reverse_iterator;
  /// Const reverse iterator over the stored elements.
  using const_reverse_iterator = typename ArrayTy::const_reverse_iterator;

  /// Element type stored in the array.
  using value_type = ValueType;
  /// Mutable reference to an element.
  using reference = ValueType &;
  /// Const reference to an element.
  using const_reference = const ValueType &;
  /// Mutable pointer to an element.
  using pointer = ValueType *;
  /// Const pointer to an element.
  using const_pointer = const ValueType *;

  /// Construct an array with value-initialized elements.
  EnumeratedArray() = default;
  /// Construct an array with every element set to \p V.
  /// @param V Value used to fill all slots.
  EnumeratedArray(ValueType V) { Underlying.fill(V); }
  /// Construct an array from exactly \c Size initializer values.
  /// @param Init Initializer list whose size must equal \c Size.
  EnumeratedArray(std::initializer_list<ValueType> Init) {
    assert(Init.size() == Size && "Incorrect initializer size");
    llvm::copy(Init, Underlying.begin());
  }

  /// Return a const reference to the element at enumerator \p Index.
  /// @param Index Enumerator used as the array index.
  /// @return Const reference to the element at \p Index.
  const ValueType &operator[](Enumeration Index) const {
    auto IX = static_cast<IndexType>(Index);
    assert(IX >= 0 && IX < Size && "Index is out of bounds.");
    return Underlying[IX];
  }
  /// Return a mutable reference to the element at enumerator \p Index.
  /// @param Index Enumerator used as the array index.
  /// @return Mutable reference to the element at \p Index.
  ValueType &operator[](Enumeration Index) {
    return const_cast<ValueType &>(
        static_cast<const EnumeratedArray &>(*this)[Index]);
  }
  /// Return the number of elements in the array.
  /// @return Number of elements in the array.
  IndexType size() const { return Size; }
  /// Return true if the array has zero elements.
  /// @return True if the array has zero elements.
  bool empty() const { return size() == 0; }

  /// Return an iterator to the first element.
  /// @return Iterator to the first element.
  iterator begin() { return Underlying.begin(); }
  /// Return a const iterator to the first element.
  /// @return Const iterator to the first element.
  const_iterator begin() const { return Underlying.begin(); }
  /// Return an iterator past the last element.
  /// @return Iterator past the last element.
  iterator end() { return Underlying.end(); }
  /// Return a const iterator past the last element.
  /// @return Const iterator past the last element.
  const_iterator end() const { return Underlying.end(); }

  /// Return a reverse iterator to the last element.
  /// @return Reverse iterator to the last element.
  reverse_iterator rbegin() { return Underlying.rbegin(); }
  /// Return a const reverse iterator to the last element.
  /// @return Const reverse iterator to the last element.
  const_reverse_iterator rbegin() const { return Underlying.rbegin(); }
  /// Return a reverse iterator past the first element.
  /// @return Reverse iterator past the first element.
  reverse_iterator rend() { return Underlying.rend(); }
  /// Return a const reverse iterator past the first element.
  /// @return Const reverse iterator past the first element.
  const_reverse_iterator rend() const { return Underlying.rend(); }
};

} // namespace llvm

#endif // LLVM_ADT_ENUMERATEDARRAY_H
