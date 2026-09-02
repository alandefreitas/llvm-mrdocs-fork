//===- ScopedHashTable.h - A simple scoped hash table -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements an efficient scoped hash table, which is useful for
// things like dominator-based optimizations.  This allows clients to do things
// like this:
//
//  ScopedHashTable<int, int> HT;
//  {
//    ScopedHashTableScope<int, int> Scope1(HT);
//    HT.insert(0, 0);
//    HT.insert(1, 1);
//    {
//      ScopedHashTableScope<int, int> Scope2(HT);
//      HT.insert(0, 42);
//    }
//  }
//
// Looking up the value for "0" in the Scope2 block will return 42.  Looking
// up the value for 0 before 42 is inserted or after Scope2 is popped will
// return 0.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SCOPEDHASHTABLE_H
#define LLVM_ADT_SCOPEDHASHTABLE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/AllocatorBase.h"
#include <cassert>
#include <new>

namespace llvm {

template <typename K, typename V, typename KInfo = DenseMapInfo<K>,
          typename AllocatorTy = MallocAllocator>
class ScopedHashTable;

/// One key/value binding stored in a ScopedHashTable scope.
template <typename K, typename V>
class ScopedHashTableVal {
  ScopedHashTableVal *NextInScope;
  ScopedHashTableVal *NextForKey;
  ScopedHashTableVal *PreInScope;
  K Key;
  V Val;

  ScopedHashTableVal(const K &key, const V &val) : Key(key), Val(val) {}

public:
  /// Return the key for this binding.
  const K &getKey() const { return Key; }
  /// Return the bound value (const).
  const V &getValue() const { return Val; }
  /// Return the bound value.
  V &getValue() { return Val; }

  /// Next older binding for the same key, or null.
  ScopedHashTableVal *getNextForKey() { return NextForKey; }
  /// Next older binding for the same key, or null (const).
  const ScopedHashTableVal *getNextForKey() const { return NextForKey; }
  /// Next binding inserted in the same scope, or null.
  ScopedHashTableVal *getNextInScope() { return NextInScope; }
  /// Previous binding inserted in the same scope, or null.
  ScopedHashTableVal *getPreInScope() { return PreInScope; }

  /// Allocate and link a new binding into the scope and per-key chains.
  template <typename AllocatorTy>
  static ScopedHashTableVal *Create(ScopedHashTableVal *nextInScope,
                                    ScopedHashTableVal *nextForKey,
                                    const K &key, const V &val,
                                    AllocatorTy &Allocator) {
    ScopedHashTableVal *New = Allocator.template Allocate<ScopedHashTableVal>();
    // Set up the value.
    new (New) ScopedHashTableVal(key, val);
    New->NextInScope = nextInScope;
    New->NextForKey = nextForKey;
    New->PreInScope = nullptr;
    if (nextInScope)
      nextInScope->PreInScope = New;
    return New;
  }

  /// Destroy this binding and return its storage to \p Allocator.
  template <typename AllocatorTy> void Destroy(AllocatorTy &Allocator) {
    // Free memory referenced by the item.
    this->~ScopedHashTableVal();
    Allocator.Deallocate(this);
  }

  /// Unlink \p ThisEntry from its scope list, advance the caller's pointer to
  /// the next per-key binding, and destroy the removed node.
  template <typename AllocatorTy>
  static void erase(ScopedHashTableVal<K, V> *&ThisEntry,
                    AllocatorTy &Allocator) {
    ScopedHashTableVal<K, V> *ToDestroy = ThisEntry;
    ScopedHashTableVal<K, V> *NextInScope = ThisEntry->NextInScope;
    ScopedHashTableVal<K, V> *PrevInScope = ThisEntry->PreInScope;
    if (PrevInScope)
      PrevInScope->NextInScope = NextInScope;
    if (NextInScope)
      NextInScope->PreInScope = PrevInScope;
    ThisEntry = ThisEntry->NextForKey;
    ToDestroy->Destroy(Allocator);
  }
};

/// RAII scope that shadows an outer ScopedHashTable and pops its bindings on
/// destruction.
template <typename K, typename V, typename KInfo = DenseMapInfo<K>,
          typename AllocatorTy = MallocAllocator>
class ScopedHashTableScope {
  /// HT - The hashtable that we are active for.
  ScopedHashTable<K, V, KInfo, AllocatorTy> &HT;

  /// PrevScope - This is the scope that we are shadowing in HT.
  ScopedHashTableScope *PrevScope;

  /// LastValInScope - This is the last value that was inserted for this scope
  /// or null if none have been inserted yet.
  ScopedHashTableVal<K, V> *LastValInScope;

public:
  ScopedHashTableScope(ScopedHashTable<K, V, KInfo, AllocatorTy> &HT);
  /// Copy construction is deleted; scopes are not shareable.
  ScopedHashTableScope(ScopedHashTableScope &) = delete;
  /// Copy assignment is deleted.
  ScopedHashTableScope &operator=(ScopedHashTableScope &) = delete;
  /// Pop this scope and destroy all bindings inserted into it.
  ~ScopedHashTableScope();

  /// Return the enclosing parent scope, or null at the outermost scope.
  ScopedHashTableScope *getParentScope() { return PrevScope; }
  /// Return the enclosing parent scope, or null at the outermost scope (const).
  const ScopedHashTableScope *getParentScope() const { return PrevScope; }
  void erase(const K &key);

private:
  friend class ScopedHashTable<K, V, KInfo, AllocatorTy>;

  ScopedHashTableVal<K, V> *getLastValInScope() {
    return LastValInScope;
  }

  void setLastValInScope(ScopedHashTableVal<K, V> *Val) {
    LastValInScope = Val;
  }
};

/// Iterator over successive shadowed values for a single key.
template <typename K, typename V, typename KInfo = DenseMapInfo<K>>
class ScopedHashTableIterator {
  ScopedHashTableVal<K, V> *Node;

public:
  /// Construct an iterator positioned at binding \p node.
  ScopedHashTableIterator(ScopedHashTableVal<K, V> *node) : Node(node) {}

  /// Dereference to the current bound value.
  V &operator*() const {
    assert(Node && "Dereference end()");
    return Node->getValue();
  }
  /// Access the current bound value through a pointer.
  V *operator->() const {
    return &Node->getValue();
  }

  /// True if both iterators refer to the same binding node.
  bool operator==(const ScopedHashTableIterator &RHS) const {
    return Node == RHS.Node;
  }
  /// True if the iterators refer to different binding nodes.
  bool operator!=(const ScopedHashTableIterator &RHS) const {
    return Node != RHS.Node;
  }

  /// Advance to the next older binding for this key.
  inline ScopedHashTableIterator& operator++() {          // Preincrement
    assert(Node && "incrementing past end()");
    Node = Node->getNextForKey();
    return *this;
  }
  /// Post-increment; return the previous position.
  ScopedHashTableIterator operator++(int) {        // Postincrement
    ScopedHashTableIterator tmp = *this; ++*this; return tmp;
  }
};

/// Hash table whose mappings are nested in RAII scopes and restored when those
/// scopes end.
template <typename K, typename V, typename KInfo, typename AllocatorTy>
class ScopedHashTable : detail::AllocatorHolder<AllocatorTy> {
  using AllocTy = detail::AllocatorHolder<AllocatorTy>;

public:
  /// ScopeTy - A type alias for easy access to the name of the scope for this
  /// hash table.
  using ScopeTy = ScopedHashTableScope<K, V, KInfo, AllocatorTy>;
  /// Unsigned type used by \c count.
  using size_type = unsigned;

private:
  friend class ScopedHashTableScope<K, V, KInfo, AllocatorTy>;

  using ValTy = ScopedHashTableVal<K, V>;

  DenseMap<K, ValTy*, KInfo> TopLevelMap;
  ScopeTy *CurScope = nullptr;

public:
  /// Construct an empty table with a default allocator.
  ScopedHashTable() = default;
  /// Construct an empty table that uses allocator \p A.
  ScopedHashTable(AllocatorTy A) : AllocTy(A) {}
  /// Copy construction is deleted.
  ScopedHashTable(const ScopedHashTable &) = delete;
  /// Copy assignment is deleted.
  ScopedHashTable &operator=(const ScopedHashTable &) = delete;

  /// Destroy the table; all scopes must already have been popped.
  ~ScopedHashTable() {
    assert(!CurScope && TopLevelMap.empty() && "Scope imbalance!");
  }

  /// Access to the allocator.
  using AllocTy::getAllocator;

  /// Return 1 if the specified key is in the table, 0 otherwise.
  size_type count(const K &Key) const {
    return TopLevelMap.count(Key);
  }

  /// Return the value currently bound to \p Key, or a default-constructed
  /// \c V if unbound.
  V lookup(const K &Key) const {
    auto I = TopLevelMap.find(Key);
    if (I != TopLevelMap.end())
      return I->second->getValue();

    return V();
  }

  /// Insert \p Key / \p Val into the current scope.
  void insert(const K &Key, const V &Val) {
    insertIntoScope(CurScope, Key, Val);
  }

  /// Iterator over shadowed values for a key.
  using iterator = ScopedHashTableIterator<K, V, KInfo>;

  /// Past-the-end iterator for per-key walks.
  iterator end() { return iterator(nullptr); }

  /// Begin iterating shadowed values for \p Key, newest first.
  iterator begin(const K &Key) {
    auto I = TopLevelMap.find(Key);
    if (I == TopLevelMap.end()) return end();
    return iterator(I->second);
  }

  /// Return the active scope, or null if none is installed.
  ScopeTy *getCurScope() { return CurScope; }
  /// Return the active scope, or null if none is installed (const).
  const ScopeTy *getCurScope() const { return CurScope; }

  /// Insert \p Key / \p Val into scope \p S.
  ///
  /// \p S may differ from the current scope, but the new binding must not be
  /// inserted underneath an existing value for \p Key.
  void insertIntoScope(ScopeTy *S, const K &Key, const V &Val) {
    assert(S && "No scope active!");
    ScopedHashTableVal<K, V> *&KeyEntry = TopLevelMap[Key];
    KeyEntry = ValTy::Create(S->getLastValInScope(), KeyEntry, Key, Val,
                             getAllocator());
    S->setLastValInScope(KeyEntry);
  }

  /// Undo the newest binding of \p key in the current scope.
  void erase(const K &key) { CurScope->erase(key); }
};

/// ScopedHashTableScope ctor - Install this as the current scope for the hash
/// table.
template <typename K, typename V, typename KInfo, typename Allocator>
ScopedHashTableScope<K, V, KInfo, Allocator>::
  ScopedHashTableScope(ScopedHashTable<K, V, KInfo, Allocator> &ht) : HT(ht) {
  PrevScope = HT.CurScope;
  HT.CurScope = this;
  LastValInScope = nullptr;
}

template <typename K, typename V, typename KInfo, typename Allocator>
ScopedHashTableScope<K, V, KInfo, Allocator>::~ScopedHashTableScope() {
  assert(HT.CurScope == this && "Scope imbalance!");
  HT.CurScope = PrevScope;

  // Pop and delete all values corresponding to this scope.
  while (ScopedHashTableVal<K, V> *ThisEntry = LastValInScope) {
    // Pop this value out of the TopLevelMap.
    if (!ThisEntry->getNextForKey()) {
      assert(HT.TopLevelMap[ThisEntry->getKey()] == ThisEntry &&
             "Scope imbalance!");
      HT.TopLevelMap.erase(ThisEntry->getKey());
    } else {
      ScopedHashTableVal<K, V> *&KeyEntry = HT.TopLevelMap[ThisEntry->getKey()];
      assert(KeyEntry == ThisEntry && "Scope imbalance!");
      KeyEntry = ThisEntry->getNextForKey();
    }

    // Pop this value out of the scope.
    LastValInScope = ThisEntry->getNextInScope();

    // Delete this entry.
    ThisEntry->Destroy(HT.getAllocator());
  }
}

/// Undo the latest binding of \p Key, restoring any shadowed older value.
///
/// In the example at the beginning of this file, if we execute `HT.erase(0)`
/// immediately after `HT.insert(0, 42);`, then the value associated with key
/// "0" reverts to 0. This value is owned by "Scope1(HT)".
template <typename K, typename V, typename KInfo, typename Allocator>
void ScopedHashTableScope<K, V, KInfo, Allocator>::erase(const K &Key) {
  auto It = HT.TopLevelMap.find(Key);
  if (It == HT.TopLevelMap.end())
    return;
  ScopedHashTableVal<K, V> *&ThisEntry = It->second;

  // `ThisEntry` may be the LastValInScope of a parent scope rather than the
  // current scope. We iterate through the scope chain to find the scope
  // that owns ThisEntry as its LastValInScope and update it accordingly.
  auto *S = this;
  while (S) {
    if (ThisEntry == S->LastValInScope) {
      S->LastValInScope = ThisEntry->getNextInScope();
      break;
    }
    S = S->PrevScope;
  }
  if (ThisEntry->getNextForKey() == nullptr)
    HT.TopLevelMap.erase(It);
  ScopedHashTableVal<K, V>::erase(ThisEntry, HT.getAllocator());
}
} // end namespace llvm

#endif // LLVM_ADT_SCOPEDHASHTABLE_H
