//===- llvm/ADT/simple_ilist.h - Simple Intrusive List ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SIMPLE_ILIST_H
#define LLVM_ADT_SIMPLE_ILIST_H

#include "llvm/ADT/ilist_base.h"
#include "llvm/ADT/ilist_iterator.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/ilist_node_options.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>
#include <utility>

namespace llvm {

/// A simple intrusive list implementation.
///
/// This is a simple intrusive list for a \c T that inherits from \c
/// ilist_node<T>.  The list never takes ownership of anything inserted in it.
///
/// Unlike \a iplist<T> and \a ilist<T>, \a simple_ilist<T> never deletes
/// values, and has no callback traits.
///
/// The API for adding nodes include \a push_front(), \a push_back(), and \a
/// insert().  These all take values by reference (not by pointer), except for
/// the range version of \a insert().
///
/// There are three sets of API for discarding nodes from the list: \a
/// remove(), which takes a reference to the node to remove, \a erase(), which
/// takes an iterator or iterator range and returns the next one, and \a
/// clear(), which empties out the container.  All three are constant time
/// operations.  None of these deletes any nodes; in particular, if there is a
/// single node in the list, then these have identical semantics:
/// \li \c L.remove(L.front());
/// \li \c L.erase(L.begin());
/// \li \c L.clear();
///
/// As a convenience for callers, there are parallel APIs that take a \c
/// Disposer (such as \c std::default_delete<T>): \a removeAndDispose(), \a
/// eraseAndDispose(), and \a clearAndDispose().  These have different names
/// because the extra semantic is otherwise non-obvious.  They are equivalent
/// to calling \a std::for_each() on the range to be discarded.
///
/// The currently available \p Options customize the nodes in the list.  The
/// same options must be specified in the \a ilist_node instantiation for
/// compatibility (although the order is irrelevant).
/// \li Use \a ilist_tag to designate which ilist_node for a given \p T this
/// list should use.  This is useful if a type \p T is part of multiple,
/// independent lists simultaneously.
/// \li Use \a ilist_sentinel_tracking to always (or never) track whether a
/// node is a sentinel.  Specifying \c true enables the \a
/// ilist_node::isSentinel() API.  Unlike \a ilist_node::isKnownSentinel(),
/// which is only appropriate for assertions, \a ilist_node::isSentinel() is
/// appropriate for real logic.
///
/// Here are examples of \p Options usage:
/// \li \c simple_ilist<T> gives the defaults.  \li \c
/// simple_ilist<T,ilist_sentinel_tracking<true>> enables the \a
/// ilist_node::isSentinel() API.
/// \li \c simple_ilist<T,ilist_tag<A>,ilist_sentinel_tracking<false>>
/// specifies a tag of A and that tracking should be off (even when
/// LLVM_ENABLE_ABI_BREAKING_CHECKS are enabled).
/// \li \c simple_ilist<T,ilist_sentinel_tracking<false>,ilist_tag<A>> is
/// equivalent to the last.
///
/// See \a is_valid_option for steps on adding a new option.
template <typename T, class... Options>
class simple_ilist
    : ilist_detail::compute_node_options<T, Options...>::type::list_base_type,
      ilist_detail::SpecificNodeAccess<
          typename ilist_detail::compute_node_options<T, Options...>::type> {
  static_assert(ilist_detail::check_options<Options...>::value,
                "Unrecognized node option!");
  using OptionsT =
      typename ilist_detail::compute_node_options<T, Options...>::type;
  using list_base_type = typename OptionsT::list_base_type;
  ilist_sentinel<OptionsT> Sentinel;

public:
  /// Element type stored as intrusive list nodes.
  using value_type = typename OptionsT::value_type;
  /// Mutable pointer to a list element.
  using pointer = typename OptionsT::pointer;
  /// Mutable reference to a list element.
  using reference = typename OptionsT::reference;
  /// Const pointer to a list element.
  using const_pointer = typename OptionsT::const_pointer;
  /// Const reference to a list element.
  using const_reference = typename OptionsT::const_reference;
  /// Mutable forward bidirectional iterator.
  using iterator = ilist_select_iterator_type<OptionsT, false, false>;
  /// Const forward bidirectional iterator.
  using const_iterator = ilist_select_iterator_type<OptionsT, false, true>;
  /// Mutable reverse bidirectional iterator.
  using reverse_iterator = ilist_select_iterator_type<OptionsT, true, false>;
  /// Const reverse bidirectional iterator.
  using const_reverse_iterator =
      ilist_select_iterator_type<OptionsT, true, true>;
  /// Unsigned size type for the list.
  using size_type = size_t;
  /// Signed distance between iterators.
  using difference_type = ptrdiff_t;

  /// Construct an empty intrusive list.
  simple_ilist() = default;
  /// Destroy the list without deleting nodes (intrusive ownership).
  ~simple_ilist() = default;

  /// Copy construction is deleted; nodes have unique list membership.
  /// @param X Unused; copy construction is deleted.
  simple_ilist(const simple_ilist &X) = delete;
  /// Copy assignment is deleted; nodes have unique list membership.
  /// @param X Unused; copy assignment is deleted.
  simple_ilist &operator=(const simple_ilist &X) = delete;

  /// Move-construct by splicing all nodes from \p X.
  /// @param X Source list (left empty).
  simple_ilist(simple_ilist &&X) { splice(end(), X); }
  /// Move-assign by clearing this list and splicing in \p X.
  /// @param X Source list (left empty).
  /// @return Reference to this list after the move.
  simple_ilist &operator=(simple_ilist &&X) {
    clear();
    splice(end(), X);
    return *this;
  }

  /// Return a mutable iterator to the first element.
  /// @return Mutable iterator to the first element.
  iterator begin() { return ++iterator(Sentinel); }
  /// Return a const iterator to the first element.
  /// @return Const iterator to the first element.
  const_iterator begin() const { return ++const_iterator(Sentinel); }
  /// Return a mutable iterator to the sentinel (past-the-end).
  /// @return Mutable past-the-end iterator.
  iterator end() { return iterator(Sentinel); }
  /// Return a const iterator to the sentinel (past-the-end).
  /// @return Const past-the-end iterator.
  const_iterator end() const { return const_iterator(Sentinel); }
  /// Return a reverse iterator to the last element.
  /// @return Mutable reverse iterator to the last element.
  reverse_iterator rbegin() { return ++reverse_iterator(Sentinel); }
  /// Return a const reverse iterator to the last element.
  /// @return Const reverse iterator to the last element.
  const_reverse_iterator rbegin() const {
    return ++const_reverse_iterator(Sentinel);
  }
  /// Return a reverse iterator to the sentinel (past-the-rend).
  /// @return Mutable past-the-rend reverse iterator.
  reverse_iterator rend() { return reverse_iterator(Sentinel); }
  /// Return a const reverse iterator to the sentinel (past-the-rend).
  /// @return Const past-the-rend reverse iterator.
  const_reverse_iterator rend() const {
    return const_reverse_iterator(Sentinel);
  }

  /// Check if the list is empty in constant time.
  /// @return True if the list has no elements.
  [[nodiscard]] bool empty() const { return Sentinel.empty(); }

  /// Calculate the size of the list in linear time.
  /// @return Number of elements in the list.
  [[nodiscard]] size_type size() const { return std::distance(begin(), end()); }

  /// Return a mutable reference to the first element.
  /// @return Mutable reference to the first element.
  reference front() { return *begin(); }
  /// Return a const reference to the first element.
  /// @return Const reference to the first element.
  const_reference front() const { return *begin(); }
  /// Return a mutable reference to the last element.
  /// @return Mutable reference to the last element.
  reference back() { return *rbegin(); }
  /// Return a const reference to the last element.
  /// @return Const reference to the last element.
  const_reference back() const { return *rbegin(); }

  /// Insert a node at the front; never copies.
  /// @param Node Node to insert at the front.
  void push_front(reference Node) { insert(begin(), Node); }

  /// Insert a node at the back; never copies.
  /// @param Node Node to insert at the back.
  void push_back(reference Node) { insert(end(), Node); }

  /// Remove the node at the front; never deletes.
  void pop_front() { erase(begin()); }

  /// Remove the node at the back; never deletes.
  void pop_back() { erase(--end()); }

  /// Swap with another list in place using std::swap.
  /// @param X Other list to swap with.
  void swap(simple_ilist &X) { std::swap(*this, X); }

  /// Insert a node by reference; never copies.
  /// @param I Insertion position.
  /// @param Node Node to insert.
  /// @return Iterator to the inserted node.
  iterator insert(iterator I, reference Node) {
    list_base_type::insertBefore(*I.getNodePtr(), *this->getNodePtr(&Node));
    return iterator(&Node);
  }

  /// Insert a range of nodes; never copies.
  /// @param I Insertion position.
  /// @param First Start of the source range.
  /// @param Last End of the source range.
  template <class Iterator>
  void insert(iterator I, Iterator First, Iterator Last) {
    for (; First != Last; ++First)
      insert(I, *First);
  }

  /// Clone another list.
  /// @param L2 Source list to clone from.
  /// @param clone Callable that clones each element of \p L2.
  /// @param dispose Callable that disposes of existing nodes in this list.
  template <class Cloner, class Disposer>
  void cloneFrom(const simple_ilist &L2, Cloner clone, Disposer dispose) {
    clearAndDispose(dispose);
    for (const_reference V : L2)
      push_back(*clone(V));
  }

  /// Remove a node by reference; never deletes.
  ///
  /// \see \a erase() for removing by iterator.
  /// \see \a removeAndDispose() if the node should be deleted.
  /// @param N Node to remove from the list.
  void remove(reference N) { list_base_type::remove(*this->getNodePtr(&N)); }

  /// Remove a node by reference and dispose of it.
  /// @param N Node to remove from the list.
  /// @param dispose Callable that disposes of \p N.
  template <class Disposer>
  void removeAndDispose(reference N, Disposer dispose) {
    remove(N);
    dispose(&N);
  }

  /// Remove a node by iterator; never deletes.
  ///
  /// \see \a remove() for removing by reference.
  /// \see \a eraseAndDispose() it the node should be deleted.
  /// @param I Iterator to the node to erase.
  /// @return Iterator to the element following the erased node.
  iterator erase(iterator I) {
    assert(I != end() && "Cannot remove end of list!");
    remove(*I++);
    return I;
  }

  /// Remove a range of nodes; never deletes.
  ///
  /// \see \a eraseAndDispose() if the nodes should be deleted.
  /// @param First Start of the range to erase.
  /// @param Last End of the range to erase.
  /// @return Iterator \p Last after the erased range.
  iterator erase(iterator First, iterator Last) {
    list_base_type::removeRange(*First.getNodePtr(), *Last.getNodePtr());
    return Last;
  }

  /// Remove a node by iterator and dispose of it.
  /// @param I Iterator to the node to erase.
  /// @param dispose Callable that disposes of the erased node.
  /// @return Iterator to the element following the erased node.
  template <class Disposer>
  iterator eraseAndDispose(iterator I, Disposer dispose) {
    auto Next = std::next(I);
    erase(I);
    dispose(&*I);
    return Next;
  }

  /// Remove a range of nodes and dispose of them.
  /// @param First Start of the range to erase.
  /// @param Last End of the range to erase.
  /// @param dispose Callable that disposes of each erased node.
  /// @return Iterator \p Last after the disposed range.
  template <class Disposer>
  iterator eraseAndDispose(iterator First, iterator Last, Disposer dispose) {
    while (First != Last)
      First = eraseAndDispose(First, dispose);
    return Last;
  }

  /// Clear the list; never deletes.
  ///
  /// \see \a clearAndDispose() if the nodes should be deleted.
  void clear() { Sentinel.reset(); }

  /// Clear the list and dispose of the nodes.
  /// @param dispose Callable that disposes of each node.
  template <class Disposer> void clearAndDispose(Disposer dispose) {
    eraseAndDispose(begin(), end(), dispose);
  }

  /// Splice in another list.
  /// @param I Insertion position in this list.
  /// @param L2 Source list (emptied).
  void splice(iterator I, simple_ilist &L2) {
    splice(I, L2, L2.begin(), L2.end());
  }

  /// Splice in a node from another list.
  /// @param I Insertion position in this list.
  /// @param L2 Source list.
  /// @param Node Iterator to the node to move.
  void splice(iterator I, simple_ilist &L2, iterator Node) {
    splice(I, L2, Node, std::next(Node));
  }

  /// Splice in a range of nodes from another list.
  /// @param I Insertion position in this list.
  /// @param L2 Source list.
  /// @param First Start of the range to move.
  /// @param Last End of the range to move.
  void splice(iterator I, simple_ilist &L2, iterator First, iterator Last) {
    (void)L2;
    list_base_type::transferBefore(*I.getNodePtr(), *First.getNodePtr(),
                                   *Last.getNodePtr());
  }

  /// Merge in another list.
  ///
  /// \pre \c this and \p RHS are sorted.
  ///@{
  /// Merge sorted list \p RHS into this list using operator<.
  /// @param RHS Sorted list to merge from.
  void merge(simple_ilist &RHS) { merge(RHS, std::less<T>()); }
  /// Merge sorted list \p RHS into this list using comparator \p comp.
  /// @param RHS Sorted list to merge from.
  /// @param comp Ordering predicate.
  template <class Compare> void merge(simple_ilist &RHS, Compare comp);
  ///@}

  /// Sort the list.
  ///@{
  /// Sort the list using operator<.
  void sort() { sort(std::less<T>()); }
  /// Stable-sort the list using comparator \p comp.
  /// @param comp Ordering predicate.
  template <class Compare> void sort(Compare comp);
  ///@}
};

/// Merge sorted list \p RHS into this list using comparator \p comp.
/// @param RHS Sorted list to merge from.
/// @param comp Ordering predicate.
template <class T, class... Options>
template <class Compare>
void simple_ilist<T, Options...>::merge(simple_ilist &RHS, Compare comp) {
  if (this == &RHS || RHS.empty())
    return;
  iterator LI = begin(), LE = end();
  iterator RI = RHS.begin(), RE = RHS.end();
  while (LI != LE) {
    if (comp(*RI, *LI)) {
      // Transfer a run of at least size 1 from RHS to LHS.
      iterator RunStart = RI++;
      RI = std::find_if(RI, RE, [&](reference RV) { return !comp(RV, *LI); });
      splice(LI, RHS, RunStart, RI);
      if (RI == RE)
        return;
    }
    ++LI;
  }
  // Transfer the remaining RHS nodes once LHS is finished.
  splice(LE, RHS, RI, RE);
}

/// Stable-sort the list using comparator \p comp.
/// @param comp Ordering predicate.
template <class T, class... Options>
template <class Compare>
void simple_ilist<T, Options...>::sort(Compare comp) {
  // Vacuously sorted.
  if (empty() || std::next(begin()) == end())
    return;

  // Split the list in the middle.
  iterator Center = begin(), End = begin();
  while (End != end() && ++End != end()) {
    ++Center;
    ++End;
  }
  simple_ilist RHS;
  RHS.splice(RHS.end(), *this, Center, end());

  // Sort the sublists and merge back together.
  sort(comp);
  RHS.sort(comp);
  merge(RHS, comp);
}

} // end namespace llvm

#endif // LLVM_ADT_SIMPLE_ILIST_H
