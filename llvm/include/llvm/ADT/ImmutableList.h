//==--- ImmutableList.h - Immutable (functional) list interface --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the ImmutableList class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_IMMUTABLELIST_H
#define LLVM_ADT_IMMUTABLELIST_H

#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/Allocator.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <new>

namespace llvm {

template <typename T> class ImmutableListFactory;

/// Cons-cell node for an immutable list: a head element and a tail pointer.
///
/// Nodes are uniqued by \c ImmutableListFactory and live in a folding set.
template <typename T>
class ImmutableListImpl : public FoldingSetNode {
  friend class ImmutableListFactory<T>;

  T Head;
  const ImmutableListImpl* Tail;

  template <typename ElemT>
  ImmutableListImpl(ElemT &&head, const ImmutableListImpl *tail = nullptr)
    : Head(std::forward<ElemT>(head)), Tail(tail) {}

public:
  /// Copy construction is deleted; nodes are uniquely owned by the factory.
  ImmutableListImpl(const ImmutableListImpl &) = delete;
  /// Copy assignment is deleted; nodes are uniquely owned by the factory.
  ImmutableListImpl &operator=(const ImmutableListImpl &) = delete;

  /// Return the element stored at the head of this cons cell.
  const T& getHead() const { return Head; }
  /// Return the tail list node, or null if this is the last element.
  const ImmutableListImpl* getTail() const { return Tail; }

  /// Profile head \p H and tail \p L into folding-set ID \p ID for uniquing.
  static inline void Profile(FoldingSetNodeID& ID, const T& H,
                             const ImmutableListImpl* L){
    ID.AddPointer(L);
    ID.Add(H);
  }

  /// Profile this node's head and tail into folding-set ID \p ID.
  void Profile(FoldingSetNodeID& ID) {
    Profile(ID, Head, Tail);
  }
};

/// This class represents an immutable (functional) list. It is implemented as a
/// smart pointer (wraps ImmutableListImpl), so it is intended to always be
/// copied by value as if it were a pointer. This interface matches ImmutableSet
/// and ImmutableMap. ImmutableList objects should almost never be created
/// directly, and instead should be created by ImmutableListFactory objects that
/// manage the lifetime of a group of lists. When the factory object is
/// reclaimed, all lists created by that factory are released as well.
template <typename T>
class ImmutableList {
public:
  /// Element type stored in the list.
  using value_type = T;
  /// Factory type that allocates and uniques lists of this element type.
  using Factory = ImmutableListFactory<T>;

  static_assert(std::is_trivially_destructible<T>::value,
                "T must be trivially destructible!");

private:
  const ImmutableListImpl<T>* X;

public:
  /// Wrap internal node pointer \p x; normally only called by the factory.
  ///
  /// There may be cases, however, when one needs to extract the internal
  /// pointer and reconstruct a list object from that pointer.
  ImmutableList(const ImmutableListImpl<T>* x = nullptr) : X(x) {}

  /// Return the underlying cons-cell pointer used for uniquing and identity.
  const ImmutableListImpl<T>* getInternalPointer() const {
    return X;
  }

  /// Forward iterator over the elements of an ImmutableList.
  class iterator {
    const ImmutableListImpl<T>* L = nullptr;

  public:
    /// Iterator category tag (forward iterator).
    using iterator_category = std::forward_iterator_tag;
    /// Element type yielded by the iterator.
    using value_type = std::remove_reference_t<T>;
    /// Signed type used for iterator distances.
    using difference_type = std::ptrdiff_t;
    /// Pointer to a const element.
    using pointer = const value_type *;
    /// Reference to a const element.
    using reference = const value_type &;

    /// Construct a past-the-end iterator.
    iterator() = default;
    /// Construct an iterator at the head of list \p l.
    iterator(ImmutableList l) : L(l.getInternalPointer()) {}

    /// Advance to the next element and return this iterator.
    iterator& operator++() { L = L->getTail(); return *this; }
    /// Advance to the next element and return the previous iterator value.
    iterator operator++(int) {
      iterator Tmp = *this;
      ++*this;
      return Tmp;
    }
    /// Return true if both iterators refer to the same list node.
    bool operator==(const iterator& I) const { return L == I.L; }
    /// Return true if the iterators refer to different list nodes.
    bool operator!=(const iterator& I) const { return L != I.L; }
    /// Return a const reference to the current element.
    const value_type& operator*() const { return L->getHead(); }
    /// Return a pointer to the current element.
    const std::remove_reference_t<value_type> *operator->() const {
      return &L->getHead();
    }

    /// Return the ImmutableList starting at the current position.
    ImmutableList getList() const { return L; }
  };

  /// Returns an iterator referring to the head of the list, or an iterator
  /// denoting the end of the list if the list is empty.
  iterator begin() const { return iterator(X); }

  /// Returns an iterator denoting the end of the list. This iterator does not
  /// refer to a valid list element.
  iterator end() const { return iterator(); }

  /// Returns true if the list is empty.
  bool isEmpty() const { return !X; }

  /// Return true if the list contains an element equal to \p V.
  bool contains(const T& V) const {
    for (iterator I = begin(), E = end(); I != E; ++I) {
      if (*I == V)
        return true;
    }
    return false;
  }

  /// Returns true if two lists are equal.  Because all lists created from the
  /// same ImmutableListFactory are uniqued, this has O(1) complexity because it
  /// the contents of the list do not need to be compared. Note that you should
  /// only compare two lists created from the same ImmutableListFactory.
  bool isEqual(const ImmutableList& L) const { return X == L.X; }

  /// Return true if this list is equal to \p L (same uniqued identity).
  bool operator==(const ImmutableList& L) const { return isEqual(L); }

  /// Returns the head of the list.
  const T& getHead() const {
    assert(!isEmpty() && "Cannot get the head of an empty list.");
    return X->getHead();
  }

  /// Returns the tail of the list, which is another (possibly empty)
  /// ImmutableList.
  ImmutableList getTail() const {
    return X ? X->getTail() : nullptr;
  }

  /// Profile this list's identity into folding-set ID \p ID.
  void Profile(FoldingSetNodeID& ID) const {
    ID.AddPointer(X);
  }
};

/// Factory that allocates and uniques immutable lists of element type \c T.
///
/// Owns a bump allocator (or borrows one) and a folding set of list nodes so
/// that structurally identical lists share storage.
template <typename T>
class ImmutableListFactory {
  using ListTy = ImmutableListImpl<T>;
  using CacheTy = FoldingSet<ListTy>;

  CacheTy Cache;
  uintptr_t Allocator;

  bool ownsAllocator() const {
    return (Allocator & 0x1) == 0;
  }

  BumpPtrAllocator& getAllocator() const {
    return *reinterpret_cast<BumpPtrAllocator*>(Allocator & ~0x1);
  }

public:
  /// Construct a factory that owns a fresh bump-pointer allocator.
  ImmutableListFactory()
    : Allocator(reinterpret_cast<uintptr_t>(new BumpPtrAllocator())) {}

  /// Construct a factory that allocates from the caller-owned allocator \p Alloc.
  ImmutableListFactory(BumpPtrAllocator& Alloc)
  : Allocator(reinterpret_cast<uintptr_t>(&Alloc) | 0x1) {}

  /// Destroy the factory and free the owned allocator if any.
  ~ImmutableListFactory() {
    if (ownsAllocator()) delete &getAllocator();
  }

  /// Return the list with \p Head prepended onto \p Tail, uniqued in the cache.
  ///
  /// \param Head New head element (moved or copied into the cons cell).
  /// \param Tail Existing list to become the new list's tail.
  template <typename ElemT>
  [[nodiscard]] ImmutableList<T> concat(ElemT &&Head, ImmutableList<T> Tail) {
    // Profile the new list to see if it already exists in our cache.
    FoldingSetNodeID ID;
    void* InsertPos;

    const ListTy* TailImpl = Tail.getInternalPointer();
    ListTy::Profile(ID, Head, TailImpl);
    ListTy* L = Cache.FindNodeOrInsertPos(ID, InsertPos);

    if (!L) {
      // The list does not exist in our cache.  Create it.
      BumpPtrAllocator& A = getAllocator();
      L = (ListTy*) A.Allocate<ListTy>();
      new (L) ListTy(std::forward<ElemT>(Head), TailImpl);

      // Insert the new list into the cache.
      Cache.InsertNode(L, InsertPos);
    }

    return L;
  }

  /// Prepend \p Data onto list \p L (alias of \c concat).
  template <typename ElemT>
  [[nodiscard]] ImmutableList<T> add(ElemT &&Data, ImmutableList<T> L) {
    return concat(std::forward<ElemT>(Data), L);
  }

  /// Construct a \c T from \p Args and prepend it onto \p Tail.
  template <typename... CtorArgs>
  [[nodiscard]] ImmutableList<T> emplace(ImmutableList<T> Tail,
                                         CtorArgs &&...Args) {
    return concat(T(std::forward<CtorArgs>(Args)...), Tail);
  }

  /// Return the empty list (null internal pointer).
  ImmutableList<T> getEmptyList() const {
    return ImmutableList<T>(nullptr);
  }

  /// Return a singleton list containing only \p Data.
  template <typename ElemT>
  ImmutableList<T> create(ElemT &&Data) {
    return concat(std::forward<ElemT>(Data), getEmptyList());
  }
};

//===----------------------------------------------------------------------===//
// Partially-specialized Traits.
//===----------------------------------------------------------------------===//

template <typename T> struct DenseMapInfo<ImmutableList<T>, void> {
  static unsigned getHashValue(ImmutableList<T> X) {
    return DenseMapInfo<const void *>::getHashValue(X.getInternalPointer());
  }

  static bool isEqual(ImmutableList<T> X1, ImmutableList<T> X2) {
    return X1 == X2;
  }
};

} // end namespace llvm

#endif // LLVM_ADT_IMMUTABLELIST_H
