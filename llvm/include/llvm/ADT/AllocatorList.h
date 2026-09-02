//===- llvm/ADT/AllocatorList.h - Custom allocator list ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ALLOCATORLIST_H
#define LLVM_ADT_ALLOCATORLIST_H

#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/simple_ilist.h"
#include "llvm/Support/Allocator.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace llvm {

/// A linked-list with a custom, local allocator.
///
/// Expose a std::list-like interface that owns and uses a custom LLVM-style
/// allocator (e.g., BumpPtrAllocator), leveraging \a simple_ilist for the
/// implementation details.
///
/// Because this list owns the allocator, calling \a splice() with a different
/// list isn't generally safe.  As such, \a splice has been left out of the
/// interface entirely.
template <class T, class AllocatorT> class AllocatorList : AllocatorT {
  struct Node : ilist_node<Node> {
    Node(Node &&) = delete;
    Node(const Node &) = delete;
    Node &operator=(Node &&) = delete;
    Node &operator=(const Node &) = delete;

    Node(T &&V) : V(std::move(V)) {}
    Node(const T &V) : V(V) {}
    template <class... Ts> Node(Ts &&... Vs) : V(std::forward<Ts>(Vs)...) {}
    T V;
  };

  using list_type = simple_ilist<Node>;

  list_type List;

  AllocatorT &getAlloc() { return *this; }
  const AllocatorT &getAlloc() const { return *this; }

  template <class... ArgTs> Node *create(ArgTs &&... Args) {
    return new (getAlloc()) Node(std::forward<ArgTs>(Args)...);
  }

  struct Cloner {
    AllocatorList &AL;

    Cloner(AllocatorList &AL) : AL(AL) {}

    Node *operator()(const Node &N) const { return AL.create(N.V); }
  };

  struct Disposer {
    AllocatorList &AL;

    Disposer(AllocatorList &AL) : AL(AL) {}

    void operator()(Node *N) const {
      N->~Node();
      AL.getAlloc().Deallocate(N);
    }
  };

public:
  /// Element type stored in the list.
  using value_type = T;
  /// Mutable pointer to an element.
  using pointer = T *;
  /// Mutable reference to an element.
  using reference = T &;
  /// Const pointer to an element.
  using const_pointer = const T *;
  /// Const reference to an element.
  using const_reference = const T &;
  /// Unsigned type used for sizes.
  using size_type = typename list_type::size_type;
  /// Signed type used for iterator distances.
  using difference_type = typename list_type::difference_type;

private:
  template <class ValueT, class IteratorBase>
  class IteratorImpl
      : public iterator_adaptor_base<IteratorImpl<ValueT, IteratorBase>,
                                     IteratorBase,
                                     std::bidirectional_iterator_tag, ValueT> {
    template <class OtherValueT, class OtherIteratorBase>
    friend class IteratorImpl;
    friend AllocatorList;

    using base_type =
        iterator_adaptor_base<IteratorImpl<ValueT, IteratorBase>, IteratorBase,
                              std::bidirectional_iterator_tag, ValueT>;

  public:
    using value_type = ValueT;
    using pointer = ValueT *;
    using reference = ValueT &;

    IteratorImpl() = default;
    IteratorImpl(const IteratorImpl &) = default;
    IteratorImpl &operator=(const IteratorImpl &) = default;

    explicit IteratorImpl(const IteratorBase &I) : base_type(I) {}

    template <class OtherValueT, class OtherIteratorBase>
    IteratorImpl(const IteratorImpl<OtherValueT, OtherIteratorBase> &X,
                 std::enable_if_t<std::is_convertible<
                     OtherIteratorBase, IteratorBase>::value> * = nullptr)
        : base_type(X.wrapped()) {}

    ~IteratorImpl() = default;

    reference operator*() const { return base_type::wrapped()->V; }
    pointer operator->() const { return &operator*(); }
  };

public:
  /// Mutable bidirectional iterator over elements.
  using iterator = IteratorImpl<T, typename list_type::iterator>;
  /// Mutable reverse iterator over elements.
  using reverse_iterator =
      IteratorImpl<T, typename list_type::reverse_iterator>;
  /// Const bidirectional iterator over elements.
  using const_iterator =
      IteratorImpl<const T, typename list_type::const_iterator>;
  /// Const reverse iterator over elements.
  using const_reverse_iterator =
      IteratorImpl<const T, typename list_type::const_reverse_iterator>;

  /// Construct an empty list with a default-constructed allocator.
  AllocatorList() = default;
  /// Move-construct from \p X, taking ownership of its nodes and allocator.
  AllocatorList(AllocatorList &&X)
      : AllocatorT(std::move(X.getAlloc())), List(std::move(X.List)) {}

  /// Copy-construct by cloning nodes from \p X into a fresh allocator.
  AllocatorList(const AllocatorList &X) {
    List.cloneFrom(X.List, Cloner(*this), Disposer(*this));
  }

  /// Move-assign from \p X after disposing of this list's nodes.
  AllocatorList &operator=(AllocatorList &&X) {
    clear(); // Dispose of current nodes explicitly.
    List = std::move(X.List);
    getAlloc() = std::move(X.getAlloc());
    return *this;
  }

  /// Copy-assign by replacing this list's nodes with clones of \p X.
  AllocatorList &operator=(const AllocatorList &X) {
    List.cloneFrom(X.List, Cloner(*this), Disposer(*this));
    return *this;
  }

  /// Destroy the list and dispose of all allocated nodes.
  ~AllocatorList() { clear(); }

  /// Exchange nodes and allocators with \p RHS.
  void swap(AllocatorList &RHS) {
    List.swap(RHS.List);
    std::swap(getAlloc(), RHS.getAlloc());
  }

  /// Return true if the list contains no elements.
  [[nodiscard]] bool empty() const { return List.empty(); }
  /// Return the number of elements in the list.
  [[nodiscard]] size_t size() const { return List.size(); }

  /// Return an iterator to the first element.
  iterator begin() { return iterator(List.begin()); }
  /// Return an iterator past the last element.
  iterator end() { return iterator(List.end()); }
  /// Return a const iterator to the first element.
  const_iterator begin() const { return const_iterator(List.begin()); }
  /// Return a const iterator past the last element.
  const_iterator end() const { return const_iterator(List.end()); }
  /// Return a reverse iterator to the last element.
  reverse_iterator rbegin() { return reverse_iterator(List.rbegin()); }
  /// Return a reverse iterator past the first element.
  reverse_iterator rend() { return reverse_iterator(List.rend()); }
  /// Return a const reverse iterator to the last element.
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(List.rbegin());
  }
  /// Return a const reverse iterator past the first element.
  const_reverse_iterator rend() const {
    return const_reverse_iterator(List.rend());
  }

  /// Return a mutable reference to the last element.
  T &back() { return List.back().V; }
  /// Return a mutable reference to the first element.
  T &front() { return List.front().V; }
  /// Return a const reference to the last element.
  const T &back() const { return List.back().V; }
  /// Return a const reference to the first element.
  const T &front() const { return List.front().V; }

  /// Emplace a new element constructed from \p Vs before iterator \p I.
  ///
  /// \param I Insertion position.
  /// \param Vs Constructor arguments forwarded to \c T.
  /// \return Iterator to the newly inserted element.
  template <class... Ts> iterator emplace(iterator I, Ts &&... Vs) {
    return iterator(List.insert(I.wrapped(), *create(std::forward<Ts>(Vs)...)));
  }

  /// Insert a moved-from value \p V before iterator \p I.
  ///
  /// \param I Insertion position.
  /// \param V Value to insert.
  /// \return Iterator to the newly inserted element.
  iterator insert(iterator I, T &&V) {
    return iterator(List.insert(I.wrapped(), *create(std::move(V))));
  }
  /// Insert a copy of \p V before iterator \p I.
  ///
  /// \param I Insertion position.
  /// \param V Value to insert.
  /// \return Iterator to the newly inserted element.
  iterator insert(iterator I, const T &V) {
    return iterator(List.insert(I.wrapped(), *create(V)));
  }

  /// Insert copies of the range [\p First, \p Last) before iterator \p I.
  ///
  /// \param I Insertion position.
  /// \param First Start of the source range.
  /// \param Last End of the source range.
  template <class Iterator>
  void insert(iterator I, Iterator First, Iterator Last) {
    for (; First != Last; ++First)
      List.insert(I.wrapped(), *create(*First));
  }

  /// Erase the element at \p I and return the following iterator.
  ///
  /// \param I Element to erase.
  iterator erase(iterator I) {
    return iterator(List.eraseAndDispose(I.wrapped(), Disposer(*this)));
  }

  /// Erase the half-open range [\p First, \p Last) and return \p Last.
  ///
  /// \param First Start of the range to erase.
  /// \param Last End of the range to erase.
  iterator erase(iterator First, iterator Last) {
    return iterator(
        List.eraseAndDispose(First.wrapped(), Last.wrapped(), Disposer(*this)));
  }

  /// Erase all elements and dispose of their storage.
  void clear() { List.clearAndDispose(Disposer(*this)); }
  /// Remove and dispose of the last element.
  void pop_back() { List.eraseAndDispose(--List.end(), Disposer(*this)); }
  /// Remove and dispose of the first element.
  void pop_front() { List.eraseAndDispose(List.begin(), Disposer(*this)); }
  /// Append a moved-from value \p V.
  void push_back(T &&V) { insert(end(), std::move(V)); }
  /// Prepend a moved-from value \p V.
  void push_front(T &&V) { insert(begin(), std::move(V)); }
  /// Append a copy of \p V.
  void push_back(const T &V) { insert(end(), V); }
  /// Prepend a copy of \p V.
  void push_front(const T &V) { insert(begin(), V); }
  /// Emplace a new element at the end, constructed from \p Vs.
  template <class... Ts> void emplace_back(Ts &&... Vs) {
    emplace(end(), std::forward<Ts>(Vs)...);
  }
  /// Emplace a new element at the front, constructed from \p Vs.
  template <class... Ts> void emplace_front(Ts &&... Vs) {
    emplace(begin(), std::forward<Ts>(Vs)...);
  }

  /// Reset the underlying allocator.
  ///
  /// \pre \c empty()
  void resetAlloc() {
    assert(empty() && "Cannot reset allocator if not empty");
    getAlloc().Reset();
  }
};

/// AllocatorList specialized to allocate nodes from a bump-pointer allocator.
template <class T> using BumpPtrList = AllocatorList<T, BumpPtrAllocator>;

} // end namespace llvm

#endif // LLVM_ADT_ALLOCATORLIST_H
