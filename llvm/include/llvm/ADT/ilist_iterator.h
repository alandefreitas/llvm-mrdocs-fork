//===- llvm/ADT/ilist_iterator.h - Intrusive List Iterator ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ILIST_ITERATOR_H
#define LLVM_ADT_ILIST_ITERATOR_H

#include "llvm/ADT/ilist_node.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace llvm {

namespace ilist_detail {

/// Find const-correct node types.
template <class OptionsT, bool IsConst> struct IteratorTraits;
/// Mutable iterator traits for \p OptionsT.
template <class OptionsT> struct IteratorTraits<OptionsT, false> {
  /// List element type.
  using value_type = typename OptionsT::value_type;
  /// Mutable pointer to a list element.
  using pointer = typename OptionsT::pointer;
  /// Mutable reference to a list element.
  using reference = typename OptionsT::reference;
  /// Mutable pointer to the underlying ilist node.
  using node_pointer = ilist_node_impl<OptionsT> *;
  /// Mutable reference to the underlying ilist node.
  using node_reference = ilist_node_impl<OptionsT> &;
};
/// Const iterator traits for \p OptionsT.
template <class OptionsT> struct IteratorTraits<OptionsT, true> {
  /// Const list element type.
  using value_type = const typename OptionsT::value_type;
  /// Const pointer to a list element.
  using pointer = typename OptionsT::const_pointer;
  /// Const reference to a list element.
  using reference = typename OptionsT::const_reference;
  /// Const pointer to the underlying ilist node.
  using node_pointer = const ilist_node_impl<OptionsT> *;
  /// Const reference to the underlying ilist node.
  using node_reference = const ilist_node_impl<OptionsT> &;
};

/// Helper selecting next/prev direction for forward or reverse iterators.
template <bool IsReverse> struct IteratorHelper;
/// Forward-iterator next/prev helpers.
template <> struct IteratorHelper<false> : ilist_detail::NodeAccess {
  /// NodeAccess alias used to reach next/prev links.
  using Access = ilist_detail::NodeAccess;

  /// Advance \p I to the next node.
  /// @param I Node pointer to advance.
  template <class T> static void increment(T *&I) { I = Access::getNext(*I); }
  /// Retreat \p I to the previous node.
  /// @param I Node pointer to retreat.
  template <class T> static void decrement(T *&I) { I = Access::getPrev(*I); }
};
/// Reverse-iterator next/prev helpers (swapped directions).
template <> struct IteratorHelper<true> : ilist_detail::NodeAccess {
  /// NodeAccess alias used to reach next/prev links.
  using Access = ilist_detail::NodeAccess;

  /// Advance \p I in reverse order (via getPrev).
  /// @param I Node pointer to advance.
  template <class T> static void increment(T *&I) { I = Access::getPrev(*I); }
  /// Retreat \p I in reverse order (via getNext).
  /// @param I Node pointer to retreat.
  template <class T> static void decrement(T *&I) { I = Access::getNext(*I); }
};

/// Mixin that exposes getNodeParent() when the list uses ilist_parent.
///
/// Adds a \a getNodeParent() function to iterators iff the list uses \a
/// ilist_parent, calling through to the node's \a getParent(). For more
/// details see \a ilist_node.
template <class IteratorTy, class ParentTy, bool IsConst>
class iterator_parent_access;
/// Const iterator mixin that returns a const parent pointer.
template <class IteratorTy, class ParentTy>
class iterator_parent_access<IteratorTy, ParentTy, true> {
public:
  /// Return the const parent of the node this iterator refers to.
  /// @return Const pointer to the parent of the referred node.
  inline const ParentTy *getNodeParent() const {
    return static_cast<IteratorTy *>(this)->NodePtr->getParent();
  }
};
/// Mutable iterator mixin that returns a mutable parent pointer.
template <class IteratorTy, class ParentTy>
class iterator_parent_access<IteratorTy, ParentTy, false> {
public:
  /// Return the parent of the node this iterator refers to.
  /// @return Pointer to the parent of the referred node.
  inline ParentTy *getNodeParent() {
    return static_cast<IteratorTy *>(this)->NodePtr->getParent();
  }
};
/// Empty const mixin when the list does not store a parent.
template <class IteratorTy>
class iterator_parent_access<IteratorTy, void, true> {};
/// Empty mutable mixin when the list does not store a parent.
template <class IteratorTy>
class iterator_parent_access<IteratorTy, void, false> {};

} // end namespace ilist_detail

/// Iterator for intrusive lists  based on ilist_node.
template <class OptionsT, bool IsReverse, bool IsConst>
class ilist_iterator : ilist_detail::SpecificNodeAccess<OptionsT>,
                       public ilist_detail::iterator_parent_access<
                           ilist_iterator<OptionsT, IsReverse, IsConst>,
                           typename OptionsT::parent_ty, IsConst> {
  friend ilist_iterator<OptionsT, IsReverse, !IsConst>;
  friend ilist_iterator<OptionsT, !IsReverse, IsConst>;
  friend ilist_iterator<OptionsT, !IsReverse, !IsConst>;
  friend ilist_detail::iterator_parent_access<
      ilist_iterator<OptionsT, IsReverse, IsConst>,
      typename OptionsT::parent_ty, IsConst>;

  using Traits = ilist_detail::IteratorTraits<OptionsT, IsConst>;
  using Access = ilist_detail::SpecificNodeAccess<OptionsT>;

public:
  /// Element type referred to by this iterator.
  using value_type = typename Traits::value_type;
  /// Mutable or const pointer to a list element.
  using pointer = typename Traits::pointer;
  /// Mutable or const reference to a list element.
  using reference = typename Traits::reference;
  /// Signed distance between iterators.
  using difference_type = ptrdiff_t;
  /// Bidirectional traversal category.
  using iterator_category = std::bidirectional_iterator_tag;
  /// Const pointer to a list element.
  using const_pointer = typename OptionsT::const_pointer;
  /// Const reference to a list element.
  using const_reference = typename OptionsT::const_reference;

private:
  using node_pointer = typename Traits::node_pointer;
  using node_reference = typename Traits::node_reference;

  node_pointer NodePtr = nullptr;

public:
  /// Create from an ilist_node.
  /// @param N Node this iterator should refer to.
  explicit ilist_iterator(node_reference N) : NodePtr(&N) {}

  /// Construct an iterator from value pointer \p NP.
  /// @param NP Pointer to the list element.
  explicit ilist_iterator(pointer NP) : NodePtr(Access::getNodePtr(NP)) {}
  /// Construct an iterator referring to value \p NR.
  /// @param NR List element this iterator should refer to.
  explicit ilist_iterator(reference NR) : NodePtr(Access::getNodePtr(&NR)) {}
  /// Construct a singular (empty) iterator.
  ilist_iterator() = default;

  /// Construct from another iterator, allowing non-const to const conversion.
  /// @param RHS Source iterator to copy from.
  template <bool RHSIsConst>
  ilist_iterator(const ilist_iterator<OptionsT, IsReverse, RHSIsConst> &RHS,
                 std::enable_if_t<IsConst || !RHSIsConst, void *> EnableIf =
                     nullptr)
      : NodePtr(RHS.NodePtr) {}

  /// Assign from another iterator, allowing non-const to const conversion.
  /// @param RHS Source iterator to copy from.
  /// @return Reference to this iterator after assignment.
  template <bool RHSIsConst>
  std::enable_if_t<IsConst || !RHSIsConst, ilist_iterator &>
  operator=(const ilist_iterator<OptionsT, IsReverse, RHSIsConst> &RHS) {
    NodePtr = RHS.NodePtr;
    return *this;
  }

  /// Explicit conversion between forward/reverse iterators.
  ///
  /// Translate between forward and reverse iterators without changing range
  /// boundaries.  The resulting iterator will dereference (and have a handle)
  /// to the previous node, which is somewhat unexpected; but converting the
  /// two endpoints in a range will give the same range in reverse.
  ///
  /// This matches std::reverse_iterator conversions.
  /// @param RHS Opposite-direction iterator to convert from.
  explicit ilist_iterator(
      const ilist_iterator<OptionsT, !IsReverse, IsConst> &RHS)
      : ilist_iterator(++RHS.getReverse()) {}

  /// Get a reverse iterator to the same node.
  ///
  /// Gives a reverse iterator that will dereference (and have a handle) to the
  /// same node.  Converting the endpoint iterators in a range will give a
  /// different range; for range operations, use the explicit conversions.
  /// @return Reverse iterator referring to the same node.
  ilist_iterator<OptionsT, !IsReverse, IsConst> getReverse() const {
    if (NodePtr)
      return ilist_iterator<OptionsT, !IsReverse, IsConst>(*NodePtr);
    return ilist_iterator<OptionsT, !IsReverse, IsConst>();
  }

  /// Const-cast.
  /// @return Non-const iterator referring to the same node.
  ilist_iterator<OptionsT, IsReverse, false> getNonConst() const {
    if (NodePtr)
      return ilist_iterator<OptionsT, IsReverse, false>(
          const_cast<typename ilist_iterator<OptionsT, IsReverse,
                                             false>::node_reference>(*NodePtr));
    return ilist_iterator<OptionsT, IsReverse, false>();
  }

  /// Dereference to the list element at this position.
  /// @return Reference to the list element at this position.
  reference operator*() const {
    assert(!NodePtr->isKnownSentinel());
    return *Access::getValuePtr(NodePtr);
  }
  /// Member access for the list element at this position.
  /// @return Pointer to the list element at this position.
  pointer operator->() const { return &operator*(); }

  /// Return true if \p LHS and \p RHS refer to the same node.
  /// @param LHS Left-hand iterator.
  /// @param RHS Right-hand iterator.
  /// @return True if \p LHS and \p RHS refer to the same node.
  friend bool operator==(const ilist_iterator &LHS, const ilist_iterator &RHS) {
    return LHS.NodePtr == RHS.NodePtr;
  }
  /// Return true if \p LHS and \p RHS refer to different nodes.
  /// @param LHS Left-hand iterator.
  /// @param RHS Right-hand iterator.
  /// @return True if \p LHS and \p RHS refer to different nodes.
  friend bool operator!=(const ilist_iterator &LHS, const ilist_iterator &RHS) {
    return LHS.NodePtr != RHS.NodePtr;
  }

  /// Move to the previous node in iteration order.
  /// @return Reference to this iterator after retreating.
  ilist_iterator &operator--() {
    NodePtr = IsReverse ? NodePtr->getNext() : NodePtr->getPrev();
    return *this;
  }
  /// Move to the next node in iteration order.
  /// @return Reference to this iterator after advancing.
  ilist_iterator &operator++() {
    NodePtr = IsReverse ? NodePtr->getPrev() : NodePtr->getNext();
    return *this;
  }
  /// Post-decrement to the previous node.
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return Copy of the iterator before retreating.
  ilist_iterator operator--(int Unused) {
    ilist_iterator tmp = *this;
    --*this;
    return tmp;
  }
  /// Post-increment to the next node.
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return Copy of the iterator before advancing.
  ilist_iterator operator++(int Unused) {
    ilist_iterator tmp = *this;
    ++*this;
    return tmp;
  }

  /// Return true if this iterator points at a node (not a default-constructed null).
  /// @return True if this iterator points at a node.
  bool isValid() const { return NodePtr; }

  /// Get the underlying ilist_node.
  /// @return Pointer to the underlying ilist_node.
  node_pointer getNodePtr() const { return static_cast<node_pointer>(NodePtr); }

  /// Check for end.  Only valid if ilist_sentinel_tracking<true>.
  /// @return True if this iterator points at the sentinel end node.
  bool isEnd() const { return NodePtr ? NodePtr->isSentinel() : false; }
};

/// Iterator for ilist_node lists that records half-open range bits.
///
/// Much like ilist_iterator, but with the addition of two bits recording
/// whether this position (when in a range) is half or fully open.
template <class OptionsT, bool IsReverse, bool IsConst>
class ilist_iterator_w_bits
    : ilist_detail::SpecificNodeAccess<OptionsT>,
      public ilist_detail::iterator_parent_access<
          ilist_iterator_w_bits<OptionsT, IsReverse, IsConst>,
          typename OptionsT::parent_ty, IsConst> {
  friend ilist_iterator_w_bits<OptionsT, IsReverse, !IsConst>;
  friend ilist_iterator_w_bits<OptionsT, !IsReverse, IsConst>;
  friend ilist_iterator<OptionsT, !IsReverse, !IsConst>;
  friend ilist_detail::iterator_parent_access<
      ilist_iterator_w_bits<OptionsT, IsReverse, IsConst>,
      typename OptionsT::parent_ty, IsConst>;

  using Traits = ilist_detail::IteratorTraits<OptionsT, IsConst>;
  using Access = ilist_detail::SpecificNodeAccess<OptionsT>;

public:
  /// List element type (mutable or const, per \p IsConst).
  using value_type = typename Traits::value_type;
  /// Mutable or const pointer to a list element.
  using pointer = typename Traits::pointer;
  /// Mutable or const reference to a list element.
  using reference = typename Traits::reference;
  /// Signed distance between iterators.
  using difference_type = ptrdiff_t;
  /// Bidirectional traversal category.
  using iterator_category = std::bidirectional_iterator_tag;
  /// Const pointer to a list element.
  using const_pointer = typename OptionsT::const_pointer;
  /// Const reference to a list element.
  using const_reference = typename OptionsT::const_reference;

private:
  using node_pointer = typename Traits::node_pointer;
  using node_reference = typename Traits::node_reference;

  node_pointer NodePtr = nullptr;

  /// Is this position intended to contain any debug-info immediately before
  /// the position?
  mutable bool HeadInclusiveBit = false;
  /// Is this position intended to contain any debug-info immediately after
  /// the position?
  mutable bool TailInclusiveBit = false;

public:
  /// Create from an ilist_node.
  /// @param N Node this iterator should refer to.
  explicit ilist_iterator_w_bits(node_reference N) : NodePtr(&N) {}

  /// Construct an iterator referring to the node for pointer \p NP.
  /// @param NP Pointer to the list element.
  explicit ilist_iterator_w_bits(pointer NP)
      : NodePtr(Access::getNodePtr(NP)) {}
  /// Construct an iterator referring to value \p NR.
  /// @param NR List element this iterator should refer to.
  explicit ilist_iterator_w_bits(reference NR)
      : NodePtr(Access::getNodePtr(&NR)) {}
  /// Default-construct a null iterator with inclusive bits cleared.
  ilist_iterator_w_bits() = default;

  /// Construct from another iterator, allowing non-const to const conversion.
  /// @param RHS Source iterator to copy from.
  template <bool RHSIsConst>
  ilist_iterator_w_bits(
      const ilist_iterator_w_bits<OptionsT, IsReverse, RHSIsConst> &RHS,
      std::enable_if_t<IsConst || !RHSIsConst, void *> = nullptr)
      : NodePtr(RHS.NodePtr) {
    HeadInclusiveBit = RHS.HeadInclusiveBit;
    TailInclusiveBit = RHS.TailInclusiveBit;
  }

  /// Assign from another iterator, copying position and inclusive bits.
  /// @param RHS Source iterator to copy from.
  /// @return Reference to this iterator after assignment.
  template <bool RHSIsConst>
  std::enable_if_t<IsConst || !RHSIsConst, ilist_iterator_w_bits &>
  operator=(const ilist_iterator_w_bits<OptionsT, IsReverse, RHSIsConst> &RHS) {
    NodePtr = RHS.NodePtr;
    HeadInclusiveBit = RHS.HeadInclusiveBit;
    TailInclusiveBit = RHS.TailInclusiveBit;
    return *this;
  }

  /// Explicit conversion between forward/reverse iterators.
  ///
  /// Translate between forward and reverse iterators without changing range
  /// boundaries.  The resulting iterator will dereference (and have a handle)
  /// to the previous node, which is somewhat unexpected; but converting the
  /// two endpoints in a range will give the same range in reverse.
  ///
  /// This matches std::reverse_iterator conversions.
  /// @param RHS Opposite-direction iterator to convert from.
  explicit ilist_iterator_w_bits(
      const ilist_iterator_w_bits<OptionsT, !IsReverse, IsConst> &RHS)
      : ilist_iterator_w_bits(++RHS.getReverse()) {}

  /// Get a reverse iterator to the same node.
  ///
  /// Gives a reverse iterator that will dereference (and have a handle) to the
  /// same node.  Converting the endpoint iterators in a range will give a
  /// different range; for range operations, use the explicit conversions.
  /// @return Reverse iterator referring to the same node.
  ilist_iterator_w_bits<OptionsT, !IsReverse, IsConst> getReverse() const {
    if (NodePtr)
      return ilist_iterator_w_bits<OptionsT, !IsReverse, IsConst>(*NodePtr);
    return ilist_iterator_w_bits<OptionsT, !IsReverse, IsConst>();
  }

  /// Const-cast.
  /// @return Non-const iterator referring to the same node.
  ilist_iterator_w_bits<OptionsT, IsReverse, false> getNonConst() const {
    if (NodePtr) {
      auto New = ilist_iterator_w_bits<OptionsT, IsReverse, false>(
          const_cast<typename ilist_iterator_w_bits<OptionsT, IsReverse,
                                                    false>::node_reference>(
              *NodePtr));
      New.HeadInclusiveBit = HeadInclusiveBit;
      New.TailInclusiveBit = TailInclusiveBit;
      return New;
    }
    return ilist_iterator_w_bits<OptionsT, IsReverse, false>();
  }

  /// Dereference to the list element at this position.
  /// @return Reference to the list element at this position.
  reference operator*() const {
    assert(!NodePtr->isKnownSentinel());
    return *Access::getValuePtr(NodePtr);
  }
  /// Member access for the list element at this position.
  /// @return Pointer to the list element at this position.
  pointer operator->() const { return &operator*(); }

  /// Return true if \p LHS and \p RHS refer to the same node.
  /// @param LHS Left-hand iterator.
  /// @param RHS Right-hand iterator.
  /// @return True if \p LHS and \p RHS refer to the same node.
  friend bool operator==(const ilist_iterator_w_bits &LHS,
                         const ilist_iterator_w_bits &RHS) {
    return LHS.NodePtr == RHS.NodePtr;
  }
  /// Return true if \p LHS and \p RHS refer to different nodes.
  /// @param LHS Left-hand iterator.
  /// @param RHS Right-hand iterator.
  /// @return True if \p LHS and \p RHS refer to different nodes.
  friend bool operator!=(const ilist_iterator_w_bits &LHS,
                         const ilist_iterator_w_bits &RHS) {
    return LHS.NodePtr != RHS.NodePtr;
  }

  /// Move to the previous node and clear inclusive bits.
  /// @return Reference to this iterator after retreating.
  ilist_iterator_w_bits &operator--() {
    NodePtr = IsReverse ? NodePtr->getNext() : NodePtr->getPrev();
    HeadInclusiveBit = false;
    TailInclusiveBit = false;
    return *this;
  }
  /// Move to the next node and clear inclusive bits.
  /// @return Reference to this iterator after advancing.
  ilist_iterator_w_bits &operator++() {
    NodePtr = IsReverse ? NodePtr->getPrev() : NodePtr->getNext();
    HeadInclusiveBit = false;
    TailInclusiveBit = false;
    return *this;
  }
  /// Post-decrement to the previous node.
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return Copy of the iterator before retreating.
  ilist_iterator_w_bits operator--(int Unused) {
    ilist_iterator_w_bits tmp = *this;
    --*this;
    return tmp;
  }
  /// Post-increment to the next node.
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return Copy of the iterator before advancing.
  ilist_iterator_w_bits operator++(int Unused) {
    ilist_iterator_w_bits tmp = *this;
    ++*this;
    return tmp;
  }

  /// Return true if this iterator points at a node.
  /// @return True if this iterator points at a node.
  bool isValid() const { return NodePtr; }

  /// Get the underlying ilist_node.
  /// @return Pointer to the underlying ilist_node.
  node_pointer getNodePtr() const { return static_cast<node_pointer>(NodePtr); }

  /// Check for end.  Only valid if ilist_sentinel_tracking<true>.
  /// @return True if this iterator points at the sentinel end node.
  bool isEnd() const { return NodePtr ? NodePtr->isSentinel() : false; }

  /// Return whether the position includes leading adjacent debug-info.
  /// @return True if leading adjacent debug-info is included.
  bool getHeadBit() const { return HeadInclusiveBit; }
  /// Return whether the position includes trailing adjacent debug-info.
  /// @return True if trailing adjacent debug-info is included.
  bool getTailBit() const { return TailInclusiveBit; }
  /// Set whether the position includes leading adjacent debug-info.
  /// @param SetBit True to include leading adjacent debug-info.
  void setHeadBit(bool SetBit) const { HeadInclusiveBit = SetBit; }
  /// Set whether the position includes trailing adjacent debug-info.
  /// @param SetBit True to include trailing adjacent debug-info.
  void setTailBit(bool SetBit) const { TailInclusiveBit = SetBit; }
};

template <typename From> struct simplify_type;

/// Allow ilist_iterators to convert into pointers to a node automatically when
/// used by the dyn_cast, cast, isa mechanisms...
///
/// FIXME: remove this, since there is no implicit conversion to NodeTy.
template <class OptionsT, bool IsConst>
struct simplify_type<ilist_iterator<OptionsT, false, IsConst>> {
  /// Forward ilist_iterator type being simplified.
  using iterator = ilist_iterator<OptionsT, false, IsConst>;
  /// Pointer type produced by simplification.
  using SimpleType = typename iterator::pointer;

  /// Return the element pointer for \p Node.
  /// @param Node Iterator to simplify to a pointer.
  /// @return Pointer to the list element referred to by \p Node.
  static SimpleType getSimplifiedValue(const iterator &Node) { return &*Node; }
};
/// Const-iterator simplify_type forwarding to the non-const specialization.
template <class OptionsT, bool IsConst>
struct simplify_type<const ilist_iterator<OptionsT, false, IsConst>>
    : simplify_type<ilist_iterator<OptionsT, false, IsConst>> {};

/// Allow ilist_iterator_w_bits to convert into pointers for isa/dyn_cast.
template <class OptionsT, bool IsConst>
struct simplify_type<ilist_iterator_w_bits<OptionsT, false, IsConst>> {
  /// Forward ilist_iterator_w_bits type being simplified.
  using iterator = ilist_iterator_w_bits<OptionsT, false, IsConst>;
  /// Pointer type produced by simplification.
  using SimpleType = typename iterator::pointer;

  /// Return the element pointer for \p Node.
  /// @param Node Iterator to simplify to a pointer.
  /// @return Pointer to the list element referred to by \p Node.
  static SimpleType getSimplifiedValue(const iterator &Node) { return &*Node; }
};
/// Const ilist_iterator_w_bits simplify_type forwarding to the mutable one.
template <class OptionsT, bool IsConst>
struct simplify_type<const ilist_iterator_w_bits<OptionsT, false, IsConst>>
    : simplify_type<ilist_iterator_w_bits<OptionsT, false, IsConst>> {};

} // end namespace llvm

#endif // LLVM_ADT_ILIST_ITERATOR_H
