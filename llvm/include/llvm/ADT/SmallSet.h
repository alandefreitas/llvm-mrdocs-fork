//===- llvm/ADT/SmallSet.h - 'Normally small' sets --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the SmallSet class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SMALLSET_H
#define LLVM_ADT_SMALLSET_H

#include "llvm/ADT/ADL.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <set>
#include <utility>

namespace llvm {

/// SmallSetIterator - This class implements a const_iterator for SmallSet by
/// delegating to the underlying SmallVector or Set iterators.
template <typename T, unsigned N, typename C>
class SmallSetIterator
    : public iterator_facade_base<SmallSetIterator<T, N, C>,
                                  std::forward_iterator_tag, T, std::ptrdiff_t,
                                  const T *, const T &> {
private:
  using SetIterTy = typename std::set<T, C>::const_iterator;
  using VecIterTy = typename SmallVector<T, N>::const_iterator;

  /// Iterators to the parts of the SmallSet containing the data. They are set
  /// depending on isSmall.
  union {
    /// Iterator into the large-mode std::set storage.
    SetIterTy SetIter;
    /// Iterator into the small-mode SmallVector storage.
    VecIterTy VecIter;
  };

  bool IsSmall;

public:
  /// Construct an iterator over the large-mode set storage.
  /// @param SetIter Iterator into the large-mode std::set.
  SmallSetIterator(SetIterTy SetIter) : SetIter(SetIter), IsSmall(false) {}

  /// Construct an iterator over the small-mode vector storage.
  /// @param VecIter Iterator into the small-mode SmallVector.
  SmallSetIterator(VecIterTy VecIter) : VecIter(VecIter), IsSmall(true) {}

  // Spell out destructor, copy/move constructor and assignment operators for
  // MSVC STL, where set<T>::const_iterator is not trivially copy constructible.
  /// Destroy the active underlying iterator.
  ~SmallSetIterator() {
    if (IsSmall)
      VecIter.~VecIterTy();
    else
      SetIter.~SetIterTy();
  }

  /// Copy-construct from another SmallSetIterator.
  /// @param Other Iterator to copy.
  SmallSetIterator(const SmallSetIterator &Other) : IsSmall(Other.IsSmall) {
    if (IsSmall)
      VecIter = Other.VecIter;
    else
      // Use placement new, to make sure SetIter is properly constructed, even
      // if it is not trivially copy-able (e.g. in MSVC).
      new (&SetIter) SetIterTy(Other.SetIter);
  }

  /// Move-construct from another SmallSetIterator.
  /// @param Other Iterator to move from.
  SmallSetIterator(SmallSetIterator &&Other) : IsSmall(Other.IsSmall) {
    if (IsSmall)
      VecIter = std::move(Other.VecIter);
    else
      // Use placement new, to make sure SetIter is properly constructed, even
      // if it is not trivially copy-able (e.g. in MSVC).
      new (&SetIter) SetIterTy(std::move(Other.SetIter));
  }

  /// Copy-assign from another SmallSetIterator.
  /// @param Other Iterator to copy from.
  /// @return Reference to this iterator.
  SmallSetIterator& operator=(const SmallSetIterator& Other) {
    // Call destructor for SetIter, so it gets properly destroyed if it is
    // not trivially destructible in case we are setting VecIter.
    if (!IsSmall)
      SetIter.~SetIterTy();

    IsSmall = Other.IsSmall;
    if (IsSmall)
      VecIter = Other.VecIter;
    else
      new (&SetIter) SetIterTy(Other.SetIter);
    return *this;
  }

  /// Move-assign from another SmallSetIterator.
  /// @param Other Iterator to move from.
  /// @return Reference to this iterator.
  SmallSetIterator& operator=(SmallSetIterator&& Other) {
    // Call destructor for SetIter, so it gets properly destroyed if it is
    // not trivially destructible in case we are setting VecIter.
    if (!IsSmall)
      SetIter.~SetIterTy();

    IsSmall = Other.IsSmall;
    if (IsSmall)
      VecIter = std::move(Other.VecIter);
    else
      new (&SetIter) SetIterTy(std::move(Other.SetIter));
    return *this;
  }

  /// Return true if both iterators refer to the same element.
  /// @param RHS Iterator to compare against.
  /// @return True if both iterators refer to the same element.
  bool operator==(const SmallSetIterator &RHS) const {
    if (IsSmall != RHS.IsSmall)
      return false;
    if (IsSmall)
      return VecIter == RHS.VecIter;
    return SetIter == RHS.SetIter;
  }

  /// Advance to the next element and return this iterator.
  /// @return Reference to this iterator.
  SmallSetIterator &operator++() { // Preincrement
    if (IsSmall)
      ++VecIter;
    else
      ++SetIter;
    return *this;
  }

  /// Return a const reference to the current element.
  /// @return Const reference to the current element.
  const T &operator*() const { return IsSmall ? *VecIter : *SetIter; }
};

/// A set of unique values optimized for the case when the set is small.
///
/// When the set has fewer than N elements, it is maintained with no mallocs.
/// If the set gets large, it expands to using an std::set to maintain
/// reasonable lookup times.
template <typename T, unsigned N, typename C = std::less<T>>
class SmallSet {
  /// Use a SmallVector to hold the elements here (even though it will never
  /// reach its 'large' stage) to avoid calling the default ctors of elements
  /// we will never use.
  SmallVector<T, N> Vector;
  std::set<T, C> Set;

  // In small mode SmallPtrSet uses linear search for the elements, so it is
  // not a good idea to choose this value too high. You may consider using a
  // DenseSet<> instead if you expect many elements in the set.
  static_assert(N <= 32, "N should be small");

public:
  /// Element type stored in the set (also used as the key type).
  using key_type = T;
  /// Unsigned type used for sizes.
  using size_type = size_t;
  /// Element type stored in the set.
  using value_type = T;
  /// Const iterator over the set elements.
  using const_iterator = SmallSetIterator<T, N, C>;

  /// Construct an empty set.
  SmallSet() = default;
  /// Copy-construct from another SmallSet.
  /// @param Other Set to copy.
  SmallSet(const SmallSet &Other) = default;
  /// Move-construct from another SmallSet.
  /// @param Other Set to move from.
  SmallSet(SmallSet &&Other) = default;

  /// Construct a set from the half-open iterator range [\p Begin, \p End).
  /// @param Begin Iterator to the first element to insert.
  /// @param End Iterator past the last element to insert.
  template <typename IterT> SmallSet(IterT Begin, IterT End) {
    insert(Begin, End);
  }

  /// Construct a set from the elements of range \p R.
  /// @param Tag Discriminator selecting the range constructor.
  /// @param R Range whose elements are inserted.
  template <typename Range>
  SmallSet(llvm::from_range_t Tag, Range &&R)
      : SmallSet(adl_begin(R), adl_end(R)) {}

  /// Construct a set from the initializer list \p L.
  /// @param L Initializer list of elements to insert.
  SmallSet(std::initializer_list<T> L) { insert(L.begin(), L.end()); }

  /// Copy-assign from another SmallSet.
  /// @param Other Set to copy from.
  /// @return Reference to this set.
  SmallSet &operator=(const SmallSet &Other) = default;
  /// Move-assign from another SmallSet.
  /// @param Other Set to move from.
  /// @return Reference to this set.
  SmallSet &operator=(SmallSet &&Other) = default;

  /// Return true if the set contains no elements.
  /// @return True if the set contains no elements.
  [[nodiscard]] bool empty() const { return Vector.empty() && Set.empty(); }

  /// Return the number of elements in the set.
  /// @return Number of elements in the set.
  [[nodiscard]] size_type size() const {
    return isSmall() ? Vector.size() : Set.size();
  }

  /// count - Return 1 if the element is in the set, 0 otherwise.
  /// @param V Element to look up.
  /// @return 1 if \p V is in the set, 0 otherwise.
  [[nodiscard]] size_type count(const T &V) const {
    return contains(V) ? 1 : 0;
  }

  /// Insert \p V if it is not already present.
  ///
  /// @param V Element to insert.
  /// @return An iterator to the element and whether insertion occurred.
  std::pair<const_iterator, bool> insert(const T &V) { return insertImpl(V); }

  /// Insert \p V if it is not already present, moving from \p V when inserted.
  /// @param V Element to insert, moved from when insertion occurs.
  /// @return An iterator to the element and whether insertion occurred.
  std::pair<const_iterator, bool> insert(T &&V) {
    return insertImpl(std::move(V));
  }

  /// Insert each element in the half-open range [\p I, \p E).
  /// @param I Iterator to the first element to insert.
  /// @param E Iterator past the last element to insert.
  template <typename IterT>
  void insert(IterT I, IterT E) {
    for (; I != E; ++I)
      insert(*I);
  }

  /// Insert each element from range \p R.
  /// @param R Range whose elements are inserted.
  template <typename Range> void insert_range(Range &&R) {
    insert(adl_begin(R), adl_end(R));
  }

  /// Erase \p V if present; return whether an element was removed.
  /// @param V Element to remove.
  /// @return True if an element was removed.
  bool erase(const T &V) {
    if (!isSmall())
      return Set.erase(V);
    auto I = vfind(V);
    if (I != Vector.end()) {
      Vector.erase(I);
      return true;
    }
    return false;
  }

  /// Remove all elements from the set.
  void clear() {
    Vector.clear();
    Set.clear();
  }

  /// Return an iterator to the first element.
  /// @return Const iterator to the first element.
  [[nodiscard]] const_iterator begin() const {
    if (isSmall())
      return {Vector.begin()};
    return {Set.begin()};
  }

  /// Return an iterator past the last element.
  /// @return Const iterator past the last element.
  [[nodiscard]] const_iterator end() const {
    if (isSmall())
      return {Vector.end()};
    return {Set.end()};
  }

  /// Check if the SmallSet contains the given element.
  /// @param V Element to look up.
  /// @return True if \p V is present in the set.
  [[nodiscard]] bool contains(const T &V) const {
    if (isSmall())
      return vfind(V) != Vector.end();
    return Set.find(V) != Set.end();
  }

private:
  bool isSmall() const { return Set.empty(); }

  template <typename ArgType>
  std::pair<const_iterator, bool> insertImpl(ArgType &&V) {
    static_assert(std::is_convertible_v<ArgType, T>,
                  "ArgType must be convertible to T!");
    if (!isSmall()) {
      auto [I, Inserted] = Set.insert(std::forward<ArgType>(V));
      return {const_iterator(I), Inserted};
    }

    auto I = vfind(V);
    if (I != Vector.end()) // Don't reinsert if it already exists.
      return {const_iterator(I), false};
    if (Vector.size() < N) {
      Vector.push_back(std::forward<ArgType>(V));
      return {const_iterator(std::prev(Vector.end())), true};
    }
    // Otherwise, grow from vector to set.
    Set.insert(std::make_move_iterator(Vector.begin()),
               std::make_move_iterator(Vector.end()));
    Vector.clear();
    return {const_iterator(Set.insert(std::forward<ArgType>(V)).first), true};
  }

  // Handwritten linear search. The use of std::find might hurt performance as
  // its implementation may be optimized for larger containers.
  typename SmallVector<T, N>::const_iterator vfind(const T &V) const {
    for (auto I = Vector.begin(), E = Vector.end(); I != E; ++I)
      if (*I == V)
        return I;
    return Vector.end();
  }
};

/// If this set is of pointer values, transparently switch over to using
/// SmallPtrSet for performance.
template <typename PointeeType, unsigned N>
class SmallSet<PointeeType *, N> : public SmallPtrSet<PointeeType *, N> {};

/// Equality comparison for SmallSet.
///
/// Iterates over elements of LHS confirming that each element is also a member
/// of RHS, and that RHS contains no additional values.
/// Equivalent to N calls to RHS.count.
/// For small-set mode amortized complexity is O(N^2)
/// For large-set mode amortized complexity is linear, worst case is O(N^2) (if
/// every hash collides).
/// @param LHS Left-hand set to compare.
/// @param RHS Right-hand set to compare.
/// @return True if both sets contain the same elements.
template <typename T, unsigned LN, unsigned RN, typename C>
[[nodiscard]] bool operator==(const SmallSet<T, LN, C> &LHS,
                              const SmallSet<T, RN, C> &RHS) {
  if (LHS.size() != RHS.size())
    return false;

  // All elements in LHS must also be in RHS
  return all_of(LHS, [&RHS](const T &E) { return RHS.count(E); });
}

/// Inequality comparison for SmallSet.
///
/// Equivalent to !(LHS == RHS). See operator== for performance notes.
/// @param LHS Left-hand set to compare.
/// @param RHS Right-hand set to compare.
/// @return True if the sets are not equal.
template <typename T, unsigned LN, unsigned RN, typename C>
[[nodiscard]] bool operator!=(const SmallSet<T, LN, C> &LHS,
                              const SmallSet<T, RN, C> &RHS) {
  return !(LHS == RHS);
}

} // end namespace llvm

#endif // LLVM_ADT_SMALLSET_H
