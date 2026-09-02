//===- llvm/ADT/Repeated.h - Repeated value range ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the Repeated<T> class, a memory-efficient range representing N
// copies of the same value.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_REPEATED_H
#define LLVM_ADT_REPEATED_H

#include "llvm/ADT/iterator.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <utility>

namespace llvm {

/// A random-access iterator that always dereferences to the same value.
template <typename T>
class RepeatedIterator
    : public iterator_facade_base<RepeatedIterator<T>,
                                  std::random_access_iterator_tag, T, ptrdiff_t,
                                  const T *, const T &> {
  const T *value = nullptr;
  ptrdiff_t index = 0;

public:
  /// Construct a singular (unusable) iterator.
  RepeatedIterator() = default;
  /// Construct an iterator at logical position \p index over \p value.
  /// @param value Pointer to the repeated element storage.
  /// @param index Logical offset within the repeated range.
  RepeatedIterator(const T *value, ptrdiff_t index)
      : value(value), index(index) {}

  /// Return a reference to the repeated value.
  const T &operator*() const { return *value; }

  /// Return true if both iterators are at the same logical index.
  /// @param rhs Iterator to compare with.
  bool operator==(const RepeatedIterator &rhs) const {
    assert((!value || !rhs.value || value == rhs.value) &&
           "comparing iterators from different Repeated ranges");
    return index == rhs.index;
  }

  /// Return true if this iterator precedes \p rhs.
  /// @param rhs Iterator to compare with.
  bool operator<(const RepeatedIterator &rhs) const {
    assert((!value || !rhs.value || value == rhs.value) &&
           "comparing iterators from different Repeated ranges");
    return index < rhs.index;
  }

  /// Return the distance from \p rhs to this iterator.
  /// @param rhs Iterator to subtract.
  ptrdiff_t operator-(const RepeatedIterator &rhs) const {
    assert((!value || !rhs.value || value == rhs.value) &&
           "subtracting iterators from different Repeated ranges");
    return index - rhs.index;
  }

  /// Advance this iterator by \p n positions.
  /// @param n Number of positions to move forward.
  RepeatedIterator &operator+=(ptrdiff_t n) {
    index += n;
    return *this;
  }

  /// Move this iterator backward by \p n positions.
  /// @param n Number of positions to move backward.
  RepeatedIterator &operator-=(ptrdiff_t n) {
    index -= n;
    return *this;
  }
};

/// A memory-efficient immutable range with a single value repeated N times.
/// The value is owned by the range.
///
/// `Repeated<T>` is also a proper random-access range: `begin()`/`end()`
/// return iterators that always dereference to the same stored value.
// At least 16-byte aligned so that Repeated<T>* has more low bits available
// than a plain pointer. The primary use case is pointer-like types (e.g. MLIR
// Type, Value) where Repeated<T>* appears in a PointerUnion alongside them.
template <typename T>
struct [[nodiscard]] alignas(std::max(size_t{16}, alignof(T))) Repeated {
  /// The single stored element that every index yields.
  T storage;
  /// Number of logical repetitions of \c storage.
  size_t count;

  /// Create a `value` repeated `count` times.
  /// Uses the same argument order like std container constructors.
  template <typename U>
  Repeated(size_t count, U &&value)
      : storage(std::forward<U>(value)), count(count) {}

  /// Random-access iterator over the repeated range.
  using iterator = RepeatedIterator<T>;
  /// Const iterator type (same as \c iterator; the range is immutable).
  using const_iterator = iterator;
  /// Reverse iterator over the repeated range.
  using reverse_iterator = std::reverse_iterator<iterator>;
  /// Const reverse iterator type (same as \c reverse_iterator).
  using const_reverse_iterator = reverse_iterator;
  /// Element type of the range.
  using value_type = T;
  /// Unsigned size type for the logical length.
  using size_type = size_t;

  /// Return an iterator to the first logical element.
  iterator begin() const { return {&storage, 0}; }
  /// Return a past-the-end iterator.
  iterator end() const { return {&storage, static_cast<ptrdiff_t>(count)}; }
  /// Return a reverse iterator to the last logical element.
  reverse_iterator rbegin() const { return reverse_iterator(end()); }
  /// Return a reverse past-the-end iterator.
  reverse_iterator rend() const { return reverse_iterator(begin()); }

  /// Return the number of logical repetitions.
  size_t size() const { return count; }
  /// Return true if there are no logical elements.
  bool empty() const { return count == 0; }

  /// Return a const reference to the stored value.
  const T &value() const { return storage; }
  /// Return the stored value for any valid index \p idx.
  /// @param idx Logical index; must be less than \c size().
  const T &operator[](size_t idx) const {
    assert(idx < size() && "Out of bounds");
    (void)idx;
    return storage;
  }
};

/// Deduce \c Repeated element type from the forwarded value argument.
template <typename U> Repeated(size_t, U &&) -> Repeated<std::decay_t<U>>;

} // namespace llvm

#endif // LLVM_ADT_REPEATED_H
