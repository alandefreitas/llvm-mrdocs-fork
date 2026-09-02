//==-- llvm/ADT/ilist.h - Intrusive Linked List Template ---------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines classes to implement an intrusive doubly linked list class
/// (i.e. each node of the list must contain a next and previous field for the
/// list.
///
/// The ilist class itself should be a plug in replacement for list.  This list
/// replacement does not provide a constant time size() method, so be careful to
/// use empty() when you really want to know if it's empty.
///
/// The ilist class is implemented as a circular list.  The list itself contains
/// a sentinel node, whose Next points at begin() and whose Prev points at
/// rbegin().  The sentinel node itself serves as end() and rend().
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ILIST_H
#define LLVM_ADT_ILIST_H

#include "llvm/ADT/simple_ilist.h"
#include <cassert>
#include <cstddef>
#include <iterator>

namespace llvm {

/// Use delete by default for iplist and ilist.
///
/// Specialize this to get different behaviour for ownership-related API.  (If
/// you really want ownership semantics, consider using std::list or building
/// something like \a BumpPtrList.)
///
/// \see ilist_noalloc_traits
template <typename NodeTy> struct ilist_alloc_traits {
  /// Delete node \p V with the default \c delete operator.
  /// @param V Node owned by the list.
  static void deleteNode(NodeTy *V) { delete V; }
};

/// Custom traits to do nothing on deletion.
///
/// Specialize ilist_alloc_traits to inherit from this to disable the
/// non-intrusive deletion in iplist (which implies ownership).
///
/// If you want purely intrusive semantics with no callbacks, consider using \a
/// simple_ilist instead.
///
/// \code
/// template <>
/// struct ilist_alloc_traits<MyType> : ilist_noalloc_traits<MyType> {};
/// \endcode
template <typename NodeTy> struct ilist_noalloc_traits {
  /// No-op deleter for nodes not owned by the list.
  /// @param V Node that would otherwise be deleted.
  static void deleteNode(NodeTy *V) {}
};

/// Callbacks do nothing by default in iplist and ilist.
///
/// Specialize this for to use callbacks for when nodes change their list
/// membership.
template <typename NodeTy> struct ilist_callback_traits {
  /// Called when a node is inserted into the list (default: no-op).
  void addNodeToList(NodeTy *) {}
  /// Called when a node is removed from the list (default: no-op).
  void removeNodeFromList(NodeTy *) {}

  /// Callback before transferring nodes to this list. The nodes may already be
  /// in this same list.
  template <class Iterator>
  void transferNodesFromList(ilist_callback_traits &OldList, Iterator /*first*/,
                             Iterator /*last*/) {
    (void)OldList;
  }
};

/// A fragment for template traits for intrusive list that provides default
/// node related operations.
///
/// TODO: Remove this layer of indirection.  It's not necessary.
template <typename NodeTy>
struct ilist_node_traits : ilist_alloc_traits<NodeTy>,
                           ilist_callback_traits<NodeTy> {};

/// Template traits for intrusive list.
///
/// Customize callbacks and allocation semantics.
template <typename NodeTy>
struct ilist_traits : public ilist_node_traits<NodeTy> {};

/// Const traits should never be instantiated.
template <typename Ty> struct ilist_traits<const Ty> {};

//===----------------------------------------------------------------------===//
//
/// A wrapper around an intrusive list with callbacks and non-intrusive
/// ownership.
///
/// This wraps a purely intrusive list (like simple_ilist) with a configurable
/// traits class.  The traits can implement callbacks and customize the
/// ownership semantics.
///
/// This is a subset of ilist functionality that can safely be used on nodes of
/// polymorphic types, i.e. a heterogeneous list with a common base class that
/// holds the next/prev pointers.  The only state of the list itself is an
/// ilist_sentinel, which holds pointers to the first and last nodes in the
/// list.
template <class IntrusiveListT, class TraitsT>
class iplist_impl : public TraitsT, IntrusiveListT {
  using base_list_type = IntrusiveListT;

public:
  /// Mutable pointer to a list element.
  using pointer = typename base_list_type::pointer;
  /// Const pointer to a list element.
  using const_pointer = typename base_list_type::const_pointer;
  /// Mutable reference to a list element.
  using reference = typename base_list_type::reference;
  /// Const reference to a list element.
  using const_reference = typename base_list_type::const_reference;
  /// Element type stored in the list.
  using value_type = typename base_list_type::value_type;
  /// Unsigned size type for the list.
  using size_type = typename base_list_type::size_type;
  /// Signed distance between iterators.
  using difference_type = typename base_list_type::difference_type;
  /// Mutable bidirectional iterator.
  using iterator = typename base_list_type::iterator;
  /// Const bidirectional iterator.
  using const_iterator = typename base_list_type::const_iterator;
  /// Mutable reverse bidirectional iterator.
  using reverse_iterator = typename base_list_type::reverse_iterator;
  /// Const reverse bidirectional iterator.
  using const_reverse_iterator =
      typename base_list_type::const_reverse_iterator;

private:
  static bool op_less(const_reference L, const_reference R) { return L < R; }
  static bool op_equal(const_reference L, const_reference R) { return L == R; }

public:
  /// Construct an empty intrusive list wrapper.
  iplist_impl() = default;

  /// Copy construction is deleted; nodes have unique list membership.
  iplist_impl(const iplist_impl &) = delete;
  /// Copy assignment is deleted; nodes have unique list membership.
  iplist_impl &operator=(const iplist_impl &) = delete;

  /// Move-construct by transferring traits and node links from \p X.
  iplist_impl(iplist_impl &&X)
      : TraitsT(std::move(static_cast<TraitsT &>(X))),
        IntrusiveListT(std::move(static_cast<IntrusiveListT &>(X))) {}
  /// Move-assign by transferring traits and node links from \p X.
  iplist_impl &operator=(iplist_impl &&X) {
    *static_cast<TraitsT *>(this) = std::move(static_cast<TraitsT &>(X));
    *static_cast<IntrusiveListT *>(this) =
        std::move(static_cast<IntrusiveListT &>(X));
    return *this;
  }

  /// Destroy the list, erasing and deleting all owned nodes.
  ~iplist_impl() { clear(); }

  /// Return the theoretical maximum number of elements.
  size_type max_size() const { return size_type(-1); }

  using base_list_type::begin;
  using base_list_type::end;
  using base_list_type::rbegin;
  using base_list_type::rend;
  using base_list_type::empty;
  using base_list_type::front;
  using base_list_type::back;

  /// Exchange contents with \p RHS (currently asserts; traits-unsafe).
  void swap(iplist_impl &RHS) {
    assert(0 && "Swap does not use list traits callback correctly yet!");
    base_list_type::swap(RHS);
  }

  /// Insert owned node \p New before \p where and notify traits.
  iterator insert(iterator where, pointer New) {
    this->addNodeToList(New); // Notify traits that we added a node...
    return base_list_type::insert(where, *New);
  }

  /// Copy-construct a node from \p New and insert it before \p where.
  iterator insert(iterator where, const_reference New) {
    return this->insert(where, new value_type(New));
  }

  /// Insert \p New immediately after \p where (or at begin if empty).
  iterator insertAfter(iterator where, pointer New) {
    if (empty())
      return insert(begin(), New);
    else
      return insert(++where, New);
  }

  /// Clone another list.
  template <class Cloner> void cloneFrom(const iplist_impl &L2, Cloner clone) {
    clear();
    for (const_reference V : L2)
      push_back(clone(V));
  }

  /// Unlink the node at \p IT without deleting it; advance \p IT past it.
  pointer remove(iterator &IT) {
    pointer Node = &*IT++;
    this->removeNodeFromList(Node); // Notify traits that we removed a node...
    base_list_type::remove(*Node);
    return Node;
  }

  /// Unlink the node at const iterator \p IT without deleting it.
  pointer remove(const iterator &IT) {
    iterator MutIt = IT;
    return remove(MutIt);
  }

  /// Unlink node pointed to by \p IT without deleting it.
  pointer remove(pointer IT) { return remove(iterator(IT)); }
  /// Unlink node referred to by \p IT without deleting it.
  pointer remove(reference IT) { return remove(iterator(IT)); }

  /// Remove the node at \p where and delete it via traits.
  iterator erase(iterator where) {
    this->deleteNode(remove(where));
    return where;
  }

  /// Erase and delete the node pointed to by \p IT.
  iterator erase(pointer IT) { return erase(iterator(IT)); }
  /// Erase and delete the node referred to by \p IT.
  ///
  /// @param IT A reference to the node to erase.
  /// @return An iterator to the node after \p IT.
  iterator erase(reference IT) { return erase(iterator(IT)); }

  /// Remove all nodes from the list like clear(), but do not call
  /// removeNodeFromList() or deleteNode().
  ///
  /// This should only be used immediately before freeing nodes in bulk to
  /// avoid traversing the list and bringing all the nodes into cache.
  void clearAndLeakNodesUnsafely() { base_list_type::clear(); }

private:
  // transfer - The heart of the splice function.  Move linked list nodes from
  // [first, last) into position.
  //
  void transfer(iterator position, iplist_impl &L2, iterator first, iterator last) {
    if (position == last)
      return;

    // Notify traits we moved the nodes...
    this->transferNodesFromList(L2, first, last);

    base_list_type::splice(position, L2, first, last);
  }

public:
  //===----------------------------------------------------------------------===
  // Functionality derived from other functions defined above...
  //

  using base_list_type::size;

  /// Erase and delete every node in [\p first, \p last).
  iterator erase(iterator first, iterator last) {
    while (first != last)
      first = erase(first);
    return last;
  }

  /// Erase and delete every node in the list.
  void clear() { erase(begin(), end()); }

  /// Insert \p val at the front of the list.
  void push_front(pointer val) { insert(begin(), val); }
  /// Insert \p val at the back of the list.
  void push_back(pointer val) { insert(end(), val); }
  /// Erase and delete the first node.
  void pop_front() {
    assert(!empty() && "pop_front() on empty list!");
    erase(begin());
  }
  /// Erase and delete the last node.
  void pop_back() {
    assert(!empty() && "pop_back() on empty list!");
    iterator t = end(); erase(--t);
  }

  /// Insert copies of [\p first, \p last) before \p where.
  template<class InIt> void insert(iterator where, InIt first, InIt last) {
    for (; first != last; ++first) insert(where, *first);
  }

  /// Move all nodes from \p L2 to before \p where.
  void splice(iterator where, iplist_impl &L2) {
    if (!L2.empty())
      transfer(where, L2, L2.begin(), L2.end());
  }
  /// Move the single node at \p first from \p L2 to before \p where.
  void splice(iterator where, iplist_impl &L2, iterator first) {
    iterator last = first; ++last;
    if (where == first || where == last) return; // No change
    transfer(where, L2, first, last);
  }
  /// Move [\p first, \p last) from \p L2 to before \p where.
  void splice(iterator where, iplist_impl &L2, iterator first, iterator last) {
    if (first != last) transfer(where, L2, first, last);
  }
  /// Move the single node \p N from \p L2 to before \p where.
  void splice(iterator where, iplist_impl &L2, reference N) {
    splice(where, L2, iterator(N));
  }
  /// Move the single node \p N from \p L2 to before \p where.
  void splice(iterator where, iplist_impl &L2, pointer N) {
    splice(where, L2, iterator(N));
  }

  /// Merge sorted list \p Right into this list using comparator \p comp.
  template <class Compare>
  void merge(iplist_impl &Right, Compare comp) {
    if (this == &Right)
      return;
    this->transferNodesFromList(Right, Right.begin(), Right.end());
    base_list_type::merge(Right, comp);
  }
  /// Merge sorted list \p Right into this list using operator<.
  /// @param Right Sorted list to merge from.
  void merge(iplist_impl &Right) { return merge(Right, op_less); }

  using base_list_type::sort;

  /// Get the previous node, or \c nullptr for the list head.
  pointer getPrevNode(reference N) const {
    auto I = N.getIterator();
    if (I == begin())
      return nullptr;
    return &*std::prev(I);
  }
  /// Get the previous node, or \c nullptr for the list head.
  const_pointer getPrevNode(const_reference N) const {
    return getPrevNode(const_cast<reference >(N));
  }

  /// Get the next node, or \c nullptr for the list tail.
  pointer getNextNode(reference N) const {
    auto Next = std::next(N.getIterator());
    if (Next == end())
      return nullptr;
    return &*Next;
  }
  /// Get the next node, or \c nullptr for the list tail.
  const_pointer getNextNode(const_reference N) const {
    return getNextNode(const_cast<reference >(N));
  }
};

/// An intrusive list with ownership and callbacks specified/controlled by
/// ilist_traits, only with API safe for polymorphic types.
///
/// The \p Options parameters are the same as those for \a simple_ilist.  See
/// there for a description of what's available.
template <class T, class... Options>
class iplist
    : public iplist_impl<simple_ilist<T, Options...>, ilist_traits<T>> {
  using iplist_impl_type = typename iplist::iplist_impl;

public:
  /// Construct an empty intrusive list.
  iplist() = default;

  /// Copy construction is deleted; nodes have unique list membership.
  iplist(const iplist &X) = delete;
  /// Copy assignment is deleted; nodes have unique list membership.
  iplist &operator=(const iplist &X) = delete;

  /// Move-construct by taking ownership of nodes from \p X.
  iplist(iplist &&X) : iplist_impl_type(std::move(X)) {}
  /// Move-assign by transferring nodes from \p X.
  iplist &operator=(iplist &&X) {
    *static_cast<iplist_impl_type *>(this) = std::move(X);
    return *this;
  }
};

/// Intrusive list alias equivalent to \c iplist with the same options.
template <class T, class... Options> using ilist = iplist<T, Options...>;

} // end namespace llvm

namespace std {

  // Ensure that swap uses the fast list swap...
  template<class Ty>
  void swap(llvm::iplist<Ty> &Left, llvm::iplist<Ty> &Right) {
    Left.swap(Right);
  }

} // end namespace std

#endif // LLVM_ADT_ILIST_H
