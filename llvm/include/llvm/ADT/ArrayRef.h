//===- ArrayRef.h - Array Reference Wrapper ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ARRAYREF_H
#define LLVM_ADT_ARRAYREF_H

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/xxhash.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <type_traits>
#include <vector>

namespace llvm {
template <typename T> class [[nodiscard]] MutableArrayRef;

/// ArrayRef - A constant reference to consecutive elements in memory.
///
/// Represents a constant reference to an array (0 or more elements
/// consecutively in memory), i.e. a start pointer and a length. It allows
/// various APIs to take consecutive elements easily and conveniently.
///
/// This class does not own the underlying data, it is expected to be used in
/// situations where the data resides in some other buffer, whose lifetime
/// extends past that of the ArrayRef. For this reason, it is not in general
/// safe to store an ArrayRef.
///
/// This is intended to be trivially copyable, so it should be passed by
/// value.
///
/// @tparam T Element type of the referenced array.
template <typename T> class LLVM_GSL_POINTER [[nodiscard]] ArrayRef {
public:
  /// Element type stored in the referenced array.
  using value_type = T;
  /// Mutable pointer to an element.
  using pointer = value_type *;
  /// Pointer to a const element.
  using const_pointer = const value_type *;
  /// Mutable reference to an element.
  using reference = value_type &;
  /// Const reference to an element.
  using const_reference = const value_type &;
  /// Iterator over the referenced array elements.
  using iterator = const_pointer;
  /// Const iterator over the referenced array elements.
  using const_iterator = const_pointer;
  /// Reverse iterator over the referenced array elements.
  using reverse_iterator = std::reverse_iterator<iterator>;
  /// Const reverse iterator over the referenced array elements.
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  /// Unsigned type used to express the size of the array.
  using size_type = size_t;
  /// Signed type used to express distances between iterators.
  using difference_type = ptrdiff_t;

private:
  /// The start of the array, in an external buffer.
  const T *Data = nullptr;

  /// The number of elements.
  size_type Length = 0;

public:
  /// @name Constructors
  /// @{

  /// Construct an empty ArrayRef.
  /*implicit*/ ArrayRef() = default;

  /// Construct an ArrayRef from a single element.
  /// @param OneElt The single element to reference.
  /*implicit*/ ArrayRef(const T &OneElt LLVM_LIFETIME_BOUND)
      : Data(&OneElt), Length(1) {}

  /// Construct an ArrayRef from a pointer and length.
  /// @param data Pointer to the first element.
  /// @param length Number of elements.
  constexpr /*implicit*/ ArrayRef(const T *data LLVM_LIFETIME_BOUND,
                                  size_t length)
      : Data(data), Length(length) {}

  /// Construct an ArrayRef from a range.
  /// @param begin Pointer to the first element.
  /// @param end Pointer one past the last element.
  constexpr ArrayRef(const T *begin LLVM_LIFETIME_BOUND, const T *end)
      : Data(begin), Length(end - begin) {
    assert(begin <= end);
  }

  /// Construct an ArrayRef from a type that has a data() method that returns
  /// a pointer convertible to const T *.
  /// @param V Object providing data() and size().
  template <
      typename C,
      typename = std::enable_if_t<
          std::conjunction_v<
              std::is_convertible<decltype(std::declval<const C &>().data()) *,
                                  const T *const *>,
              std::is_integral<decltype(std::declval<const C &>().size())>>,
          void>>
  /*implicit*/ constexpr ArrayRef(const C &V)
      : Data(V.data()), Length(V.size()) {}

  /// Construct an ArrayRef from a C array.
  /// @param Arr C array to reference.
  template <size_t N>
  /*implicit*/ constexpr ArrayRef(const T (&Arr LLVM_LIFETIME_BOUND)[N])
      : Data(Arr), Length(N) {}

#if LLVM_GNUC_PREREQ(9, 0, 0)
// Disable gcc's warning in this constructor as it generates an enormous amount
// of messages. Anyone using ArrayRef should already be aware of the fact that
// it does not do lifetime extension.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winit-list-lifetime"
#endif
  /// Construct an ArrayRef from a std::initializer_list.
  /// @param Vec Initializer list of elements to reference.
  constexpr /*implicit*/ ArrayRef(
      std::initializer_list<T> Vec LLVM_LIFETIME_BOUND)
      : Data(Vec.begin() == Vec.end() ? (T *)nullptr : Vec.begin()),
        Length(Vec.size()) {}
#if LLVM_GNUC_PREREQ(9, 0, 0)
#pragma GCC diagnostic pop
#endif

  /// Construct an ArrayRef<T> from iterator_range<U*>. This uses SFINAE
  /// to ensure that this is only used for iterator ranges over plain pointer
  /// iterators.
  /// @param Range Iterator range over contiguous elements.
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<
                            U *const *, std::add_const_t<T> *const *>>>
  ArrayRef(const iterator_range<U *> &Range)
      : Data(Range.begin()), Length(llvm::size(Range)) {}

  /// @}
  /// @name Simple Operations
  /// @{

  /// Return an iterator to the first element.
  /// @return Iterator to the first element.
  iterator begin() const { return Data; }
  /// Return an iterator past the last element.
  /// @return Iterator past the last element.
  iterator end() const { return Data + Length; }

  /// Return a reverse iterator to the last element.
  /// @return Reverse iterator to the last element.
  reverse_iterator rbegin() const { return reverse_iterator(end()); }
  /// Return a reverse iterator to the element before the first element.
  /// @return Reverse iterator past the reverse end.
  reverse_iterator rend() const { return reverse_iterator(begin()); }

  /// Check if the array is empty.
  /// @return True if the array has no elements.
  bool empty() const { return Length == 0; }

  /// Return a pointer to the first element of the array.
  /// @return Pointer to the first element.
  const T *data() const { return Data; }

  /// Get the array size.
  /// @return Number of elements in the array.
  size_t size() const { return Length; }

  /// Get the first element.
  /// @return Const reference to the first element.
  const T &front() const {
    assert(!empty());
    return Data[0];
  }

  /// Get the last element.
  /// @return Const reference to the last element.
  const T &back() const {
    assert(!empty());
    return Data[Length - 1];
  }

  /// consume_front() - Returns the first element and drops it from ArrayRef.
  /// @return Const reference to the former first element.
  const T &consume_front() {
    const T &Ret = front();
    *this = drop_front();
    return Ret;
  }

  /// consume_back() - Returns the last element and drops it from ArrayRef.
  /// @return Const reference to the former last element.
  const T &consume_back() {
    const T &Ret = back();
    *this = drop_back();
    return Ret;
  }

  /// Allocate a mutable copy in \p A and return a reference to it.
  /// @param A Allocator used to allocate the copy.
  /// @return MutableArrayRef to the newly allocated copy.
  template <typename Allocator> MutableArrayRef<T> copy(Allocator &A) {
    T *Buff = A.template Allocate<T>(Length);
    llvm::uninitialized_copy(*this, Buff);
    return MutableArrayRef<T>(Buff, Length);
  }

  /// Check for element-wise equality.
  /// @param RHS Array to compare against.
  /// @return True if both arrays have the same length and equal elements.
  bool equals(ArrayRef RHS) const {
    if (Length != RHS.Length)
      return false;
    return std::equal(begin(), end(), RHS.begin());
  }

  /// slice(n, m) - Chop off the first N elements of the array, and keep M
  /// elements in the array.
  /// @param N Number of elements to drop from the front.
  /// @param M Number of elements to keep.
  /// @return ArrayRef of \p M elements starting after the first \p N.
  ArrayRef<T> slice(size_t N, size_t M) const {
    assert(N + M <= size() && "Invalid specifier");
    return ArrayRef<T>(data() + N, M);
  }

  /// slice(n) - Chop off the first N elements of the array.
  /// @param N Number of elements to drop from the front.
  /// @return ArrayRef with the first \p N elements removed.
  ArrayRef<T> slice(size_t N) const { return drop_front(N); }

  /// Drop the first \p N elements of the array.
  /// @param N Number of elements to drop from the front.
  /// @return ArrayRef with the first \p N elements removed.
  ArrayRef<T> drop_front(size_t N = 1) const {
    assert(size() >= N && "Dropping more elements than exist");
    return slice(N, size() - N);
  }

  /// Drop the last \p N elements of the array.
  /// @param N Number of elements to drop from the back.
  /// @return ArrayRef with the last \p N elements removed.
  ArrayRef<T> drop_back(size_t N = 1) const {
    assert(size() >= N && "Dropping more elements than exist");
    return slice(0, size() - N);
  }

  /// Return a copy of *this with the first N elements satisfying the
  /// given predicate removed.
  /// @param Pred Predicate that identifies elements to drop from the front.
  /// @return Suffix starting at the first element that does not satisfy \p Pred.
  template <class PredicateT> ArrayRef<T> drop_while(PredicateT Pred) const {
    return ArrayRef<T>(find_if_not(*this, Pred), end());
  }

  /// Return a copy of *this with the first N elements not satisfying
  /// the given predicate removed.
  /// @param Pred Predicate that stops dropping when it returns true.
  /// @return Suffix starting at the first element that satisfies \p Pred.
  template <class PredicateT> ArrayRef<T> drop_until(PredicateT Pred) const {
    return ArrayRef<T>(find_if(*this, Pred), end());
  }

  /// Return a copy of *this with only the first \p N elements.
  /// @param N Number of elements to keep from the front.
  /// @return ArrayRef covering the first \p N elements.
  ArrayRef<T> take_front(size_t N = 1) const {
    if (N >= size())
      return *this;
    return drop_back(size() - N);
  }

  /// Return a copy of *this with only the last \p N elements.
  /// @param N Number of elements to keep from the back.
  /// @return ArrayRef covering the last \p N elements.
  ArrayRef<T> take_back(size_t N = 1) const {
    if (N >= size())
      return *this;
    return drop_front(size() - N);
  }

  /// Return the first N elements of this Array that satisfy the given
  /// predicate.
  /// @param Pred Predicate that identifies elements to keep from the front.
  /// @return Prefix of elements that satisfy \p Pred.
  template <class PredicateT> ArrayRef<T> take_while(PredicateT Pred) const {
    return ArrayRef<T>(begin(), find_if_not(*this, Pred));
  }

  /// Return the first N elements of this Array that don't satisfy the
  /// given predicate.
  /// @param Pred Predicate that stops taking when it returns true.
  /// @return Prefix of elements that do not satisfy \p Pred.
  template <class PredicateT> ArrayRef<T> take_until(PredicateT Pred) const {
    return ArrayRef<T>(begin(), find_if(*this, Pred));
  }

  /// @}
  /// @name Operator Overloads
  /// @{

  /// Return a const reference to the element at \p Index.
  /// @param Index Zero-based index of the element.
  /// @return Const reference to the element at \p Index.
  const T &operator[](size_t Index) const {
    assert(Index < Length && "Invalid index!");
    return Data[Index];
  }

  /// Disallow accidental assignment from a temporary.
  ///
  /// The declaration here is extra complicated so that "arrayRef = {}"
  /// continues to select the move assignment operator.
  /// @param Temporary Temporary value (assignment is deleted).
  template <typename U>
  std::enable_if_t<std::is_same<U, T>::value, ArrayRef<T>> &
  operator=(U &&Temporary) = delete;

  /// Disallow accidental assignment from a temporary.
  ///
  /// The declaration here is extra complicated so that "arrayRef = {}"
  /// continues to select the move assignment operator.
  /// @param List Initializer list (assignment is deleted).
  template <typename U>
  std::enable_if_t<std::is_same<U, T>::value, ArrayRef<T>> &
  operator=(std::initializer_list<U> List) = delete;

  /// @}
  /// @name Expensive Operations
  /// @{

  /// Copy the referenced elements into a new std::vector.
  /// @return std::vector containing copies of the referenced elements.
  std::vector<T> vec() const { return std::vector<T>(Data, Data + Length); }

  /// @}
  /// @name Conversion operators
  /// @{

  /// Convert this ArrayRef into a std::vector copy of its elements.
  /// @return std::vector containing copies of the referenced elements.
  operator std::vector<T>() const {
    return std::vector<T>(Data, Data + Length);
  }

  /// @}
};

/// A mutable reference to consecutive elements in memory.
///
/// Represent a mutable reference to an array (0 or more elements
/// consecutively in memory), i.e. a start pointer and a length.  It allows
/// various APIs to take and modify consecutive elements easily and
/// conveniently.
///
/// This class does not own the underlying data, it is expected to be used in
/// situations where the data resides in some other buffer, whose lifetime
/// extends past that of the MutableArrayRef. For this reason, it is not in
/// general safe to store a MutableArrayRef.
///
/// This is intended to be trivially copyable, so it should be passed by
/// value.
template <typename T> class [[nodiscard]] MutableArrayRef : public ArrayRef<T> {
public:
  /// Element type stored in the referenced array.
  using value_type = T;
  /// Mutable pointer to an element.
  using pointer = value_type *;
  /// Pointer to a const element.
  using const_pointer = const value_type *;
  /// Mutable reference to an element.
  using reference = value_type &;
  /// Const reference to an element.
  using const_reference = const value_type &;
  /// Iterator over the referenced array elements.
  using iterator = pointer;
  /// Const iterator over the referenced array elements.
  using const_iterator = const_pointer;
  /// Reverse iterator over the referenced array elements.
  using reverse_iterator = std::reverse_iterator<iterator>;
  /// Const reverse iterator over the referenced array elements.
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  /// Unsigned type used to express the size of the array.
  using size_type = size_t;
  /// Signed type used to express the distance between iterators.
  using difference_type = ptrdiff_t;

  /// Construct an empty MutableArrayRef.
  /*implicit*/ MutableArrayRef() = default;

  /// Construct a MutableArrayRef from a single element.
  /// @param OneElt The single element to reference.
  /*implicit*/ MutableArrayRef(T &OneElt) : ArrayRef<T>(OneElt) {}

  /// Construct a MutableArrayRef from a pointer and length.
  /// @param data Pointer to the first element.
  /// @param length Number of elements.
  /*implicit*/ MutableArrayRef(T *data, size_t length)
      : ArrayRef<T>(data, length) {}

  /// Construct a MutableArrayRef from a range.
  /// @param begin Pointer to the first element.
  /// @param end Pointer one past the last element.
  MutableArrayRef(T *begin, T *end) : ArrayRef<T>(begin, end) {}

  /// Construct a MutableArrayRef from a type that has data() and size(),
  /// where data() returns a pointer convertible to T *const *.
  /// @param V Object providing mutable data() and size().
  template <typename C,
            typename = std::enable_if_t<
                std::conjunction_v<
                    std::is_convertible<decltype(std::declval<C &>().data()) *,
                                        T *const *>,
                    std::is_integral<decltype(std::declval<C &>().size())>>,
                void>>
  /*implicit*/ constexpr MutableArrayRef(C &&V) : ArrayRef<T>(V) {}

  /// Construct a MutableArrayRef from a C array.
  /// @param Arr C array to reference.
  template <size_t N>
  /*implicit*/ constexpr MutableArrayRef(T (&Arr)[N]) : ArrayRef<T>(Arr) {}

  /// Return a mutable pointer to the start of the array.
  /// @return Pointer to the first mutable element.
  T *data() const { return const_cast<T *>(ArrayRef<T>::data()); }

  /// Iterator to the first mutable element.
  /// @return Iterator to the first element.
  iterator begin() const { return data(); }
  /// Iterator one past the last mutable element.
  /// @return Iterator past the last element.
  iterator end() const { return data() + this->size(); }

  /// Return a reverse iterator to the last mutable element.
  /// @return Reverse iterator to the last element.
  reverse_iterator rbegin() const { return reverse_iterator(end()); }
  /// Return a reverse iterator to the element before the first element.
  /// @return Reverse iterator past the reverse end.
  reverse_iterator rend() const { return reverse_iterator(begin()); }

  /// Get the first element.
  /// @return Mutable reference to the first element.
  T &front() const {
    assert(!this->empty());
    return data()[0];
  }

  /// Get the last element.
  /// @return Mutable reference to the last element.
  T &back() const {
    assert(!this->empty());
    return data()[this->size() - 1];
  }

  /// Returns the first element and drops it from ArrayRef.
  /// @return Reference to the former first element.
  T &consume_front() {
    T &Ret = front();
    *this = drop_front();
    return Ret;
  }

  /// Returns the last element and drops it from ArrayRef.
  /// @return Reference to the former last element.
  T &consume_back() {
    T &Ret = back();
    *this = drop_back();
    return Ret;
  }

  /// Chop off the first \p N elements of the array, and keep \p M elements
  /// in the array.
  /// @param N Number of elements to drop from the front.
  /// @param M Number of elements to keep.
  /// @return MutableArrayRef of \p M elements starting after the first \p N.
  MutableArrayRef<T> slice(size_t N, size_t M) const {
    assert(N + M <= this->size() && "Invalid specifier");
    return MutableArrayRef<T>(this->data() + N, M);
  }

  /// Chop off the first \p N elements of the array.
  /// @param N Number of elements to drop from the front.
  /// @return MutableArrayRef with the first \p N elements removed.
  MutableArrayRef<T> slice(size_t N) const {
    return slice(N, this->size() - N);
  }

  /// Drop the first \p N elements of the array.
  /// @param N Number of elements to drop from the front.
  /// @return MutableArrayRef with the first \p N elements removed.
  MutableArrayRef<T> drop_front(size_t N = 1) const {
    assert(this->size() >= N && "Dropping more elements than exist");
    return slice(N, this->size() - N);
  }

  /// Drop the last \p N elements of the array.
  /// @param N Number of elements to drop from the back.
  /// @return MutableArrayRef with the last \p N elements removed.
  MutableArrayRef<T> drop_back(size_t N = 1) const {
    assert(this->size() >= N && "Dropping more elements than exist");
    return slice(0, this->size() - N);
  }

  /// Return a copy of *this with the first N elements satisfying the
  /// given predicate removed.
  /// @param Pred Predicate that identifies elements to drop from the front.
  /// @return Suffix starting at the first element that does not satisfy \p Pred.
  template <class PredicateT>
  MutableArrayRef<T> drop_while(PredicateT Pred) const {
    return MutableArrayRef<T>(find_if_not(*this, Pred), end());
  }

  /// Return a copy of *this with the first N elements not satisfying
  /// the given predicate removed.
  /// @param Pred Predicate that stops dropping when it returns true.
  /// @return Suffix starting at the first element that satisfies \p Pred.
  template <class PredicateT>
  MutableArrayRef<T> drop_until(PredicateT Pred) const {
    return MutableArrayRef<T>(find_if(*this, Pred), end());
  }

  /// Return a copy of *this with only the first \p N elements.
  /// @param N Number of elements to keep from the front.
  /// @return MutableArrayRef covering the first \p N elements.
  MutableArrayRef<T> take_front(size_t N = 1) const {
    if (N >= this->size())
      return *this;
    return drop_back(this->size() - N);
  }

  /// Return a copy of *this with only the last \p N elements.
  /// @param N Number of elements to keep from the back.
  /// @return MutableArrayRef covering the last \p N elements.
  MutableArrayRef<T> take_back(size_t N = 1) const {
    if (N >= this->size())
      return *this;
    return drop_front(this->size() - N);
  }

  /// Return the first N elements of this Array that satisfy the given
  /// predicate.
  /// @param Pred Predicate that identifies elements to keep from the front.
  /// @return Prefix of elements that satisfy \p Pred.
  template <class PredicateT>
  MutableArrayRef<T> take_while(PredicateT Pred) const {
    return MutableArrayRef<T>(begin(), find_if_not(*this, Pred));
  }

  /// Return the first N elements of this Array that don't satisfy the
  /// given predicate.
  /// @param Pred Predicate that stops taking when it returns true.
  /// @return Prefix of elements that do not satisfy \p Pred.
  template <class PredicateT>
  MutableArrayRef<T> take_until(PredicateT Pred) const {
    return MutableArrayRef<T>(begin(), find_if(*this, Pred));
  }

  /// @}
  /// @name Operator Overloads
  /// @{

  /// Return a mutable reference to the element at \p Index.
  /// @param Index Zero-based index of the element.
  /// @return Mutable reference to the element at \p Index.
  T &operator[](size_t Index) const {
    assert(Index < this->size() && "Invalid index!");
    return data()[Index];
  }
};

/// @name ArrayRef Deduction guides
/// @{
/// Deduction guide to construct an ArrayRef from a single element.
template <typename T> ArrayRef(const T &OneElt) -> ArrayRef<T>;

/// Deduction guide to construct an ArrayRef from a pointer and length
template <typename T> ArrayRef(const T *data, size_t length) -> ArrayRef<T>;

/// Deduction guide to construct an ArrayRef from a range
template <typename T> ArrayRef(const T *data, const T *end) -> ArrayRef<T>;

/// Deduction guide to construct an ArrayRef from a SmallVector
template <typename T> ArrayRef(const SmallVectorImpl<T> &Vec) -> ArrayRef<T>;

/// Deduction guide to construct an ArrayRef from a SmallVector
template <typename T, unsigned N>
ArrayRef(const SmallVector<T, N> &Vec) -> ArrayRef<T>;

/// Deduction guide to construct an ArrayRef from a std::vector
template <typename T> ArrayRef(const std::vector<T> &Vec) -> ArrayRef<T>;

/// Deduction guide to construct an ArrayRef from a std::array
template <typename T, std::size_t N>
ArrayRef(const std::array<T, N> &Vec) -> ArrayRef<T>;

/// Deduction guide to construct an ArrayRef from an ArrayRef (const)
template <typename T> ArrayRef(const ArrayRef<T> &Vec) -> ArrayRef<T>;

/// Deduction guide to construct an ArrayRef from an ArrayRef
template <typename T> ArrayRef(ArrayRef<T> &Vec) -> ArrayRef<T>;

/// Deduction guide to construct an ArrayRef from a C array.
template <typename T, size_t N> ArrayRef(const T (&Arr)[N]) -> ArrayRef<T>;

/// @}

/// @name MutableArrayRef Deduction guides
/// @{
/// Deduction guide to construct a `MutableArrayRef` from a single element
template <class T> MutableArrayRef(T &OneElt) -> MutableArrayRef<T>;

/// Deduction guide to construct a `MutableArrayRef` from a pointer and
/// length.
template <class T>
MutableArrayRef(T *data, size_t length) -> MutableArrayRef<T>;

/// Deduction guide to construct a `MutableArrayRef` from a `SmallVector`.
template <class T>
MutableArrayRef(SmallVectorImpl<T> &Vec) -> MutableArrayRef<T>;

/// Deduction guide to construct a `MutableArrayRef` from a fixed-size
/// `SmallVector`.
template <class T, unsigned N>
MutableArrayRef(SmallVector<T, N> &Vec) -> MutableArrayRef<T>;

/// Deduction guide to construct a `MutableArrayRef` from a `std::vector`.
template <class T> MutableArrayRef(std::vector<T> &Vec) -> MutableArrayRef<T>;

/// Deduction guide to construct a `MutableArrayRef` from a `std::array`.
template <class T, std::size_t N>
MutableArrayRef(std::array<T, N> &Vec) -> MutableArrayRef<T>;

/// Deduction guide to construct a `MutableArrayRef` from a C array.
template <typename T, size_t N>
MutableArrayRef(T (&Arr)[N]) -> MutableArrayRef<T>;

/// @}
/// @name ArrayRef Comparison Operators
/// @{

/// Return true if \p LHS and \p RHS contain equal elements.
/// @param LHS Left-hand ArrayRef.
/// @param RHS Right-hand ArrayRef.
/// @return True if \p LHS and \p RHS contain equal elements.
template <typename T> inline bool operator==(ArrayRef<T> LHS, ArrayRef<T> RHS) {
  return LHS.equals(RHS);
}

/// Return true if SmallVector \p LHS equals ArrayRef \p RHS.
/// @param LHS Left-hand SmallVector.
/// @param RHS Right-hand ArrayRef.
/// @return True if \p LHS equals \p RHS.
template <typename T>
[[nodiscard]] inline bool operator==(const SmallVectorImpl<T> &LHS,
                                     ArrayRef<T> RHS) {
  return ArrayRef<T>(LHS).equals(RHS);
}

/// Return true if \p LHS and \p RHS differ in length or element values.
/// @param LHS Left-hand ArrayRef.
/// @param RHS Right-hand ArrayRef.
/// @return True if \p LHS and \p RHS differ in length or element values.
template <typename T> inline bool operator!=(ArrayRef<T> LHS, ArrayRef<T> RHS) {
  return !(LHS == RHS);
}

/// Return true if SmallVector \p LHS differs from ArrayRef \p RHS.
/// @param LHS Left-hand SmallVector.
/// @param RHS Right-hand ArrayRef.
/// @return True if \p LHS differs from \p RHS.
template <typename T>
[[nodiscard]] inline bool operator!=(const SmallVectorImpl<T> &LHS,
                                     ArrayRef<T> RHS) {
  return !(LHS == RHS);
}

/// Lexicographically compare two `ArrayRef`s.
/// @param LHS Left-hand ArrayRef.
/// @param RHS Right-hand ArrayRef.
/// @return True if \p LHS is lexicographically less than \p RHS.
template <typename T> inline bool operator<(ArrayRef<T> LHS, ArrayRef<T> RHS) {
  return std::lexicographical_compare(LHS.begin(), LHS.end(), RHS.begin(),
                                      RHS.end());
}

/// Lexicographically compare two `ArrayRef`s.
/// @param LHS Left-hand ArrayRef.
/// @param RHS Right-hand ArrayRef.
/// @return True if \p LHS is lexicographically greater than \p RHS.
template <typename T> inline bool operator>(ArrayRef<T> LHS, ArrayRef<T> RHS) {
  return RHS < LHS;
}

/// Return true if \p LHS is lexicographically less than or equal to \p RHS.
/// @param LHS Left-hand ArrayRef.
/// @param RHS Right-hand ArrayRef.
/// @return True if \p LHS is lexicographically less than or equal to \p RHS.
template <typename T> inline bool operator<=(ArrayRef<T> LHS, ArrayRef<T> RHS) {
  return !(LHS > RHS);
}

/// Return true if \p LHS is lexicographically greater than or equal to \p RHS.
/// @param LHS Left-hand ArrayRef.
/// @param RHS Right-hand ArrayRef.
/// @return True if \p LHS is lexicographically greater than or equal to \p RHS.
template <typename T> inline bool operator>=(ArrayRef<T> LHS, ArrayRef<T> RHS) {
  return !(LHS < RHS);
}

/// @}

/// Compute a hash_code for an ArrayRef.
/// @param S The array to hash.
/// @return Hash code for the ArrayRef.
template <typename T> hash_code hash_value(ArrayRef<T> S) {
  return hash_combine_range(S);
}

/// Compute the XXH3 64-bit hash of the bytes in \p data.
///
/// Inline ArrayRef overloads of the xxhash entry points declared out-of-line
/// in llvm/Support/xxhash.h. They live here so xxhash.h can stay free of ADT
/// dependencies.
/// @param data Bytes to hash.
/// @return XXH3 64-bit hash of the bytes.
inline uint64_t xxh3_64bits(ArrayRef<uint8_t> data) {
  return xxh3_64bits(data.data(), data.size());
}
/// Compute the XXH3 128-bit hash of the bytes in \p data.
/// @param data Bytes to hash.
/// @return XXH3 128-bit hash of the bytes.
inline XXH128_hash_t xxh3_128bits(ArrayRef<uint8_t> data) {
  return xxh3_128bits(data.data(), data.size());
}

/// DenseMapInfo specialization so ArrayRef can be used as a DenseMap key.
template <typename T> struct DenseMapInfo<ArrayRef<T>, void> {
  /// Compute a hash value for \p Val.
  /// @param Val The ArrayRef to hash.
  /// @return Hash value for \p Val.
  static unsigned getHashValue(ArrayRef<T> Val) {
    return (unsigned)(hash_value(Val));
  }

  /// Return true if \p LHS and \p RHS compare equal.
  /// @param LHS Left-hand ArrayRef.
  /// @param RHS Right-hand ArrayRef.
  /// @return True if \p LHS and \p RHS compare equal.
  static bool isEqual(ArrayRef<T> LHS, ArrayRef<T> RHS) { return LHS == RHS; }
};

} // end namespace llvm

#endif // LLVM_ADT_ARRAYREF_H
