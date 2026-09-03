//===- Interval.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Interval class is a generic interval of ordered objects that implement:
// - T * T::getPrevNode()
// - T * T::getNextNode()
// - bool T::comesBefore(const T *) const
//
// This is currently used for Instruction intervals.
// It provides an API for some basic operations on the interval, including some
// simple set operations, like union, intersection and others.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_INSTRINTERVAL_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_INSTRINTERVAL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/SandboxIR/Instruction.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>
#include <type_traits>

namespace llvm::sandboxir {

/// A simple iterator for iterating the interval.
template <typename T, typename IntervalType> class IntervalIterator {
  T *I;
  IntervalType &R;

public:
  /// Signed type used to express the distance between iterators.
  using difference_type = std::ptrdiff_t;
  /// Value type produced by the iterator.
  using value_type = T;
  /// Pointer type returned by the iterator.
  using pointer = value_type *;
  /// Reference type returned by the iterator.
  using reference = T &;
  /// Iterator category tag for this iterator.
  using iterator_category = std::bidirectional_iterator_tag;

  /// Construct an iterator at element \p I within interval \p R.
  ///
  /// \param I Element this iterator refers to, or nullptr for end.
  /// \param R Interval this iterator walks.
  IntervalIterator(T *I, IntervalType &R) : I(I), R(R) {}
  /// Return true if both iterators refer to the same element in the same
  /// interval.
  ///
  /// \param Other Iterator to compare against.
  /// \return True if both iterators refer to the same element.
  bool operator==(const IntervalIterator &Other) const {
    assert(&R == &Other.R && "Iterators belong to different regions!");
    return Other.I == I;
  }
  /// Return true if the iterators refer to different elements or intervals.
  ///
  /// \param Other Iterator to compare against.
  /// \return True if the iterators refer to different elements or intervals.
  bool operator!=(const IntervalIterator &Other) const {
    return !(*this == Other);
  }
  /// Advance to the next element in the interval.
  /// \return A reference to this iterator after advancement.
  IntervalIterator &operator++() {
    assert(I != nullptr && "already at end()!");
    I = I->getNextNode();
    return *this;
  }
  /// Advance to the next element and return the previous iterator value.
  ///
  /// \param Unused Unused postfix-discriminator parameter.
  /// \return A copy of the iterator as it was before advancement.
  IntervalIterator operator++(int Unused) {
    (void)Unused;
    auto ItCopy = *this;
    ++*this;
    return ItCopy;
  }
  /// Move to the previous element in the interval.
  /// \return A reference to this iterator after moving back.
  IntervalIterator &operator--() {
    // `I` is nullptr for end() when To is the BB terminator.
    I = I != nullptr ? I->getPrevNode() : R.bottom();
    return *this;
  }
  /// Move to the previous element and return the previous iterator value.
  ///
  /// \param Unused Unused postfix-discriminator parameter.
  /// \return A copy of the iterator as it was before moving back.
  IntervalIterator operator--(int Unused) {
    (void)Unused;
    auto ItCopy = *this;
    --*this;
    return ItCopy;
  }
  /// Return a reference to the current element.
  /// \return A reference to the current element.
  template <typename HT = std::enable_if<std::is_same<T, T *&>::value>>
  T &operator*() {
    return *I;
  }
  /// Return a reference to the current element.
  /// \return A reference to the current element.
  T &operator*() const { return *I; }
};

/// A contiguous inclusive range of ordered nodes from top through bottom.
///
/// The element type must provide getPrevNode(), getNextNode(), and
/// comesBefore(). Used for instruction intervals and similar ordered chains.
template <typename T> class Interval {
  T *Top;
  T *Bottom;

public:
  /// Construct an empty interval.
  Interval() : Top(nullptr), Bottom(nullptr) {}
  /// Construct an interval spanning from \p Top to \p Bottom inclusive.
  ///
  /// \param Top First element of the interval.
  /// \param Bottom Last element of the interval; must not come before \p Top.
  Interval(T *Top, T *Bottom) : Top(Top), Bottom(Bottom) {
    assert((Top == Bottom || Top->comesBefore(Bottom)) &&
           "Top should come before Bottom!");
  }
  /// Construct the smallest interval that spans all of \p Elems.
  ///
  /// \param Elems Non-empty list of elements to cover.
  Interval(ArrayRef<T *> Elems) {
    assert(!Elems.empty() && "Expected non-empty Elems!");
    Top = Elems[0];
    Bottom = Elems[0];
    for (auto *I : drop_begin(Elems)) {
      if (I->comesBefore(Top))
        Top = I;
      else if (Bottom->comesBefore(I))
        Bottom = I;
    }
  }
  /// Return true if this interval contains no elements.
  /// \return True if this interval contains no elements.
  bool empty() const {
    assert(((Top == nullptr && Bottom == nullptr) ||
            (Top != nullptr && Bottom != nullptr)) &&
           "Either none or both should be null");
    return Top == nullptr;
  }
  /// Return true if \p I lies within this interval inclusive.
  ///
  /// \param I Element to test for membership.
  /// \return True if \p I lies within this interval inclusive.
  bool contains(T *I) const {
    if (empty())
      return false;
    return (Top == I || Top->comesBefore(I)) &&
           (I == Bottom || I->comesBefore(Bottom));
  }
  /// Return true if \p Elm is right before the top or right after the bottom.
  ///
  /// \param Elm Element adjacent to this interval to test.
  /// \return True if \p Elm is adjacent to this interval.
  bool touches(T *Elm) const {
    return Top == Elm->getNextNode() || Bottom == Elm->getPrevNode();
  }
  /// Return the top (first) element of this interval.
  /// \return The top (first) element, or nullptr if empty.
  T *top() const { return Top; }
  /// Return the bottom (last) element of this interval.
  /// \return The bottom (last) element, or nullptr if empty.
  T *bottom() const { return Bottom; }

  /// Iterator type for walking elements in this interval.
  using iterator = IntervalIterator<T, Interval>;
  /// Return an iterator to the top element.
  /// \return An iterator to the top element.
  iterator begin() { return iterator(Top, *this); }
  /// Return an iterator past the bottom element.
  /// \return An iterator past the bottom element.
  iterator end() {
    return iterator(Bottom != nullptr ? Bottom->getNextNode() : nullptr, *this);
  }
  /// Return an iterator to the top element.
  /// \return An iterator to the top element.
  iterator begin() const {
    return iterator(Top, const_cast<Interval &>(*this));
  }
  /// Return an iterator past the bottom element.
  /// \return An iterator past the bottom element.
  iterator end() const {
    return iterator(Bottom != nullptr ? Bottom->getNextNode() : nullptr,
                    const_cast<Interval &>(*this));
  }
  /// Return true if both intervals have the same top and bottom.
  ///
  /// \param Other Interval to compare against.
  /// \return True if both intervals have the same top and bottom.
  bool operator==(const Interval &Other) const {
    return Top == Other.Top && Bottom == Other.Bottom;
  }
  /// Return true if the intervals differ in top or bottom.
  ///
  /// \param Other Interval to compare against.
  /// \return True if the intervals differ in top or bottom.
  bool operator!=(const Interval &Other) const { return !(*this == Other); }
  /// Return true if this interval comes before \p Other in program order.
  ///
  /// This expects disjoint intervals.
  ///
  /// \param Other Disjoint interval to compare against in program order.
  /// \return True if this interval comes before \p Other.
  bool comesBefore(const Interval &Other) const {
    assert(disjoint(Other) && "Expect disjoint intervals!");
    return bottom()->comesBefore(Other.top());
  }
  /// Return true if this and \p Other have nothing in common.
  ///
  /// \param Other Interval to test for overlap with this one.
  /// \return True if this and \p Other have nothing in common.
  bool disjoint(const Interval &Other) const;
  /// Return the intersection between this and \p Other.
  ///
  /// \param Other Interval to intersect with this one.
  /// \return The overlapping interval, or empty if none.
  // Example:
  // |----|   this
  //    |---| Other
  //    |-|   this->getIntersection(Other)
  Interval intersection(const Interval &Other) const {
    if (empty())
      return *this;
    if (Other.empty())
      return Interval();
    // 1. No overlap
    // A---B      this
    //       C--D Other
    if (Bottom->comesBefore(Other.Top) || Other.Bottom->comesBefore(Top))
      return Interval();
    // 2. Overlap.
    // A---B   this
    //   C--D  Other
    auto NewTopI = Top->comesBefore(Other.Top) ? Other.Top : Top;
    auto NewBottomI = Bottom->comesBefore(Other.Bottom) ? Bottom : Other.Bottom;
    return Interval(NewTopI, NewBottomI);
  }
  /// Return up to two intervals that remain after subtracting \p Other.
  ///
  /// \param Other Interval to subtract from this one.
  /// \return Up to two intervals remaining after subtraction.
  // Example:
  // |--------| this
  //    |-|     Other
  // |-|   |--| this - Other
  SmallVector<Interval, 2> operator-(const Interval &Other) {
    if (disjoint(Other))
      return {*this};
    if (Other.empty())
      return {*this};
    if (*this == Other)
      return {Interval()};
    Interval Intersection = intersection(Other);
    SmallVector<Interval, 2> Result;
    // Part 1, skip if empty.
    if (Top != Intersection.Top)
      Result.emplace_back(Top, Intersection.Top->getPrevNode());
    // Part 2, skip if empty.
    if (Intersection.Bottom != Bottom)
      Result.emplace_back(Intersection.Bottom->getNextNode(), Bottom);
    return Result;
  }
  /// Return the interval difference `this - Other` as a single interval.
  ///
  /// This will crash in Debug if the result is not a single interval.
  ///
  /// \param Other Interval to subtract from this one.
  /// \return The difference as a single interval.
  Interval getSingleDiff(const Interval &Other) {
    auto Diff = *this - Other;
    assert(Diff.size() == 1 && "Expected a single interval!");
    return Diff[0];
  }
  /// Return a single interval that spans across both this and \p Other.
  ///
  /// \param Other Interval to union with this one.
  /// \return A single interval spanning both this and \p Other.
  // For example:
  // |---|        this
  //        |---| Other
  // |----------| this->getUnionInterval(Other)
  Interval getUnionInterval(const Interval &Other) {
    if (empty())
      return Other;
    if (Other.empty())
      return *this;
    auto *NewTop = Top->comesBefore(Other.Top) ? Top : Other.Top;
    auto *NewBottom = Bottom->comesBefore(Other.Bottom) ? Other.Bottom : Bottom;
    return {NewTop, NewBottom};
  }

  /// Update the interval when \p I is about to be moved before \p BeforeIt.
  ///
  /// \param I Instruction in this interval that is about to move.
  /// \param BeforeIt Destination iterator; \p I will be placed before it.
  // SFINAE disables this for non-Instructions.
  template <typename HelperT = T>
  std::enable_if_t<std::is_same<HelperT, Instruction>::value, void>
  notifyMoveInstr(HelperT *I, decltype(I->getIterator()) BeforeIt) {
    assert(contains(I) && "Expect `I` in interval!");
    assert(I->getIterator() != BeforeIt && "Can't move `I` before itself!");

    // Nothing to do if the instruction won't move.
    if (std::next(I->getIterator()) == BeforeIt)
      return;

    T *NewTop = Top->getIterator() == BeforeIt ? I
                : I == Top                     ? Top->getNextNode()
                                               : Top;
    T *NewBottom = std::next(Bottom->getIterator()) == BeforeIt ? I
                   : I == Bottom ? Bottom->getPrevNode()
                                 : Bottom;
    Top = NewTop;
    Bottom = NewBottom;
  }

#ifndef NDEBUG
  /// Print a textual representation of this interval to \p OS.
  ///
  /// \param OS Destination stream.
  void print(raw_ostream &OS) const;
  /// Dump this interval to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif
};

// Defined in Transforms/Vectorize/SandboxVectorizer/Interval.cpp
/// Explicit instantiation of Interval for Instruction.
extern template class LLVM_TEMPLATE_ABI Interval<Instruction>;

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_INSTRINTERVAL_H
