//===--- ImmutableMap.h - Immutable (functional) map interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the ImmutableMap class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_IMMUTABLEMAP_H
#define LLVM_ADT_IMMUTABLEMAP_H

#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableSet.h"
#include "llvm/Support/Allocator.h"
#include <utility>

namespace llvm {

/// Traits class used by ImmutableMap.
///
/// While both the first and second elements in a pair are used to generate
/// profile information, only the first element (the key) is used by isEqual
/// and isLess.
template <typename T, typename S>
struct ImutKeyValueInfo {
  /// The type of a stored map entry (key and value pair).
  using value_type = const std::pair<T,S>;
  /// A reference to a stored map entry.
  using value_type_ref = const value_type&;
  /// The key type used for ordering and equality.
  using key_type = const T;
  /// A reference to a map key.
  using key_type_ref = const T&;
  /// The type of a stored map value (second element of the pair).
  using data_type = const S;
  /// A reference to a map value.
  using data_type_ref = const S&;

  /// Returns the key (first element) of a stored map entry.
  /// @param V Map entry whose key is extracted.
  /// @return The key of \p V.
  static inline key_type_ref KeyOfValue(value_type_ref V) {
    return V.first;
  }

  /// Returns the value (second element) of a stored map entry.
  /// @param V Map entry whose value is extracted.
  /// @return The value of \p V.
  static inline data_type_ref DataOfValue(value_type_ref V) {
    return V.second;
  }

  /// Returns true if \p L and \p R are equal keys.
  /// @param L First key.
  /// @param R Second key.
  /// @return True if \p L and \p R are equal.
  static inline bool isEqual(key_type_ref L, key_type_ref R) {
    return ImutContainerInfo<T>::isEqual(L,R);
  }
  /// Returns true if \p L is ordered before \p R in the tree.
  /// @param L First key.
  /// @param R Second key.
  /// @return True if \p L is ordered before \p R.
  static inline bool isLess(key_type_ref L, key_type_ref R) {
    return ImutContainerInfo<T>::isLess(L,R);
  }

  /// Returns true if the stored values \p L and \p R are equal.
  /// @param L First value.
  /// @param R Second value.
  /// @return True if \p L and \p R are equal.
  static inline bool isDataEqual(data_type_ref L, data_type_ref R) {
    return ImutContainerInfo<S>::isEqual(L,R);
  }

  /// Adds profile data for a map entry to \p ID, using both key and value.
  /// @param ID Folding-set ID to populate.
  /// @param V Map entry whose key and value are profiled.
  static inline void Profile(FoldingSetNodeID& ID, value_type_ref V) {
    ImutContainerInfo<T>::Profile(ID, V.first);
    ImutContainerInfo<S>::Profile(ID, V.second);
  }
};

/// Persistent map of \p KeyT to \p ValT backed by an immutable AVL tree.
///
/// Updates produce new map values that share unchanged subtrees with the
/// previous map. Prefer constructing maps through \c Factory rather than
/// manually from tree roots.
template <typename KeyT, typename ValT,
          typename ValInfo = ImutKeyValueInfo<KeyT, ValT>,
          bool Canonicalize = true>
class ImmutableMap {
public:
  /// The type of a stored map entry (key and value pair).
  using value_type = typename ValInfo::value_type;
  /// A reference to a stored map entry.
  using value_type_ref = typename ValInfo::value_type_ref;
  /// The key type used for ordering and equality.
  using key_type = typename ValInfo::key_type;
  /// A reference to a map key.
  using key_type_ref = typename ValInfo::key_type_ref;
  /// The type of a stored map value.
  using data_type = typename ValInfo::data_type;
  /// A reference to a map value.
  using data_type_ref = typename ValInfo::data_type_ref;
  /// The underlying AVL tree type.
  using TreeTy = ImutAVLTree<ValInfo, Canonicalize>;

protected:
  /// The root of the underlying AVL tree.
  IntrusiveRefCntPtr<TreeTy> Root;

public:
  /// Construct from tree root \p R.
  ///
  /// Prefer Factory methods except when a public constructor is useful.
  /// @param R Tree root pointer, or null for empty.
  explicit ImmutableMap(const TreeTy *R) : Root(const_cast<TreeTy *>(R)) {}

  /// Factory for creating and updating immutable maps.
  ///
  /// Allocates tree nodes from an internal bump-pointer allocator and, when
  /// \c Canonicalize is true, canonicalizes trees so equal maps share storage.
  class Factory {
    typename TreeTy::Factory F;

  public:
    /// Constructs a factory with a default allocator.
    Factory() = default;

    /// Constructs a factory that allocates nodes from \p Alloc.
    /// @param Alloc Bump-pointer allocator used for tree nodes.
    Factory(BumpPtrAllocator &Alloc) : F(Alloc) {}

    /// Factories are not copy-constructible; each owns its allocator and node pool.
    /// @param RHS Ignored; copy construction is not supported.
    Factory(const Factory &RHS) = delete;
    /// Factories are not copy-assignable; each owns its allocator and node pool.
    /// @param RHS Ignored; copy assignment is not supported.
    Factory &operator=(const Factory &RHS) = delete;

    /// Returns an immutable map that contains no elements.
    /// @return An empty immutable map.
    ImmutableMap getEmptyMap() { return ImmutableMap(F.getEmptyTree()); }

    /// Creates a new map with \p K mapped to \p D.
    ///
    /// Contains all entries of \p Old with \p K mapped to \p D. If \p Old
    /// already maps \p K to an equal value, \p Old is returned and no memory is
    /// allocated. The time and space complexity of this operation is
    /// logarithmic in the size of \p Old.
    /// @param Old Original map to update.
    /// @param K Key to insert or update.
    /// @param D Value to associate with \p K.
    /// @return A map with \p K mapped to \p D, or \p Old if unchanged.
    [[nodiscard]] ImmutableMap add(ImmutableMap Old, key_type_ref K,
                                   data_type_ref D) {
      TreeTy *T = F.add(Old.Root.get(), std::pair<key_type, data_type>(K, D));
      if constexpr (Canonicalize)
        return ImmutableMap(F.getCanonicalTree(T));
      else
        return ImmutableMap(T);
    }

    /// Merges maps \p A and \p B in a single traversal.
    ///
    /// Shares subtrees the two maps do not overlap (see
    /// ImutAVLFactory::mergeTrees). \p Combine(AElem, BElem) yields the stored
    /// element for a key; \p KeepUnmatched governs keys unique to one side.
    /// \p SkipShared returns a pointer-identical subtree unchanged in O(1);
    /// only pass true when the merge is idempotent (Combine(a, a) == a, e.g. a
    /// lattice join). This is more efficient than repeatedly adding \p B's
    /// entries to \p A when \p B is large. Only valid for non-canonicalizing
    /// factories (the bulk path does not canonicalize the nodes it creates).
    /// Does not short-circuit equal/empty operands or reorder by size; callers
    /// wanting those apply them first.
    /// @param A First map to merge.
    /// @param B Second map to merge.
    /// @param Combine Function combining values for keys present in both maps.
    /// @param KeepUnmatched Whether to keep keys unique to one side.
    /// @param SkipShared Whether to share pointer-identical subtrees in O(1).
    /// @return The map resulting from merging \p A and \p B.
    template <typename CombineFn>
    [[nodiscard]] ImmutableMap mergeWith(ImmutableMap A, ImmutableMap B,
                                         CombineFn Combine, bool KeepUnmatched,
                                         bool SkipShared = false) {
      static_assert(!Canonicalize,
                    "mergeWith does not canonicalize the nodes it creates");
      return ImmutableMap(F.mergeTrees(A.Root.get(), B.Root.get(), Combine,
                                       KeepUnmatched, SkipShared));
    }

    /// Creates a new map without the entry for key \p K.
    ///
    /// Contains all entries of \p Old except the entry for \p K. If \p Old did
    /// not contain \p K, \p Old is returned and no memory is allocated. The
    /// time and space complexity of this operation is logarithmic in the size
    /// of \p Old.
    /// @param Old Original map to update.
    /// @param K Key to remove.
    /// @return A map without \p K, or \p Old if \p K was absent.
    [[nodiscard]] ImmutableMap remove(ImmutableMap Old, key_type_ref K) {
      TreeTy *T = F.remove(Old.Root.get(), K);
      if constexpr (Canonicalize)
        return ImmutableMap(F.getCanonicalTree(T));
      else
        return ImmutableMap(T);
    }

    /// Returns the underlying AVL-tree factory used to allocate and balance nodes.
    /// @return Pointer to the AVL-tree factory owned by this factory.
    typename TreeTy::Factory *getTreeFactory() const {
      return const_cast<typename TreeTy::Factory *>(&F);
    }
  };

  /// Returns true if the map contains an entry with key \p K.
  /// @param K Key to test for membership.
  /// @return True if an entry with key \p K exists.
  [[nodiscard]] bool contains(key_type_ref K) const {
    return Root ? Root->contains(K) : false;
  }

  /// Compares two maps for equality.
  ///
  /// For a canonicalizing factory, maps with equal contents share the same
  /// tree, so this is an O(1) pointer comparison (like ImmutableList); only
  /// maps created by the same factory may be compared. Otherwise it is a
  /// structural comparison.
  /// @param RHS Other map to compare against.
  /// @return True if this map and \p RHS are equal.
  [[nodiscard]] bool operator==(const ImmutableMap &RHS) const {
    if constexpr (Canonicalize)
      return Root == RHS.Root;
    else
      return Root && RHS.Root ? Root->isEqual(*RHS.Root.get())
                              : Root == RHS.Root;
  }

  /// Compares two maps for inequality.
  ///
  /// For a canonicalizing factory this is an O(1) pointer comparison; otherwise
  /// it is a structural comparison.
  /// @param RHS Other map to compare against.
  /// @return True if this map and \p RHS are not equal.
  [[nodiscard]] bool operator!=(const ImmutableMap &RHS) const {
    if constexpr (Canonicalize)
      return Root != RHS.Root;
    else
      return Root && RHS.Root ? Root->isNotEqual(*RHS.Root.get())
                              : Root != RHS.Root;
  }

  /// Returns a pointer to the tree root, incrementing its reference count.
  /// @return The tree root, or null if the map is empty.
  [[nodiscard]] TreeTy *getRoot() const {
    if (Root) { Root->retain(); }
    return Root.get();
  }

  /// Return the tree root without incrementing its reference count.
  /// @return The tree root, or null if the map is empty.
  [[nodiscard]] TreeTy *getRootWithoutRetain() const { return Root.get(); }

  /// Increments the reference count of the tree root.
  void manualRetain() {
    if (Root) Root->retain();
  }

  /// Decrements the reference count of the tree root, destroying the tree if it
  /// reaches zero.
  void manualRelease() {
    if (Root) Root->release();
  }

  /// Returns true if the map contains no entries.
  /// @return True if the map contains no entries.
  [[nodiscard]] bool isEmpty() const { return !Root; }

public:
  //===--------------------------------------------------===//
  // For testing.
  //===--------------------------------------------------===//

  /// Checks that the AVL balancing and ordering invariants hold for this map.
  void verify() const { if (Root) Root->verify(); }

  //===--------------------------------------------------===//
  // Iterators.
  //===--------------------------------------------------===//

  /// In-order iterator over the key-value pairs of the map.
  class iterator : public ImutAVLValueIterator<ImmutableMap> {
    friend class ImmutableMap;

    iterator() = default;
    explicit iterator(TreeTy *Tree) : iterator::ImutAVLValueIterator(Tree) {}

  public:
    /// Returns the key (first element) of the current key-value pair.
    /// @return The key of the current entry.
    key_type_ref getKey() const { return (*this)->first; }
    /// Returns the value (second element) of the current key-value pair.
    /// @return The value of the current entry.
    data_type_ref getData() const { return (*this)->second; }
  };

  /// Returns an iterator to the first key-value pair in in-order traversal.
  /// @return Iterator to the first entry, or end if empty.
  [[nodiscard]] iterator begin() const { return iterator(Root.get()); }
  /// Returns the end iterator for in-order traversal.
  /// @return The end iterator.
  [[nodiscard]] iterator end() const { return iterator(); }

  /// Finds the value associated with \p K.
  ///
  /// Returns a pointer to the stored value, or nullptr if the key is not
  /// present.
  /// @param K Key to look up.
  /// @return Pointer to the stored value, or nullptr if \p K is absent.
  [[nodiscard]] data_type *lookup(key_type_ref K) const {
    if (Root) {
      TreeTy* T = Root->find(K);
      if (T) return &T->getValue().second;
    }

    return nullptr;
  }

  /// Returns the <key,value> pair in the ImmutableMap for which key is the
  /// highest in the ordering of keys in the map. This method returns NULL if
  /// the map is empty.
  /// @return Pointer to the maximum key-value pair, or nullptr if empty.
  [[nodiscard]] value_type *getMaxElement() const {
    return Root ? &(Root->getMaxElement()->getValue()) : nullptr;
  }

  //===--------------------------------------------------===//
  // Utility methods.
  //===--------------------------------------------------===//

  /// Returns the height of the underlying AVL tree, or 0 if the map is empty.
  /// @return The AVL tree height, or 0 if empty.
  [[nodiscard]] unsigned getHeight() const {
    return Root ? Root->getHeight() : 0;
  }

  /// Adds profile data for \p M to \p ID, keyed by the tree root pointer.
  /// @param ID Folding-set ID to populate.
  /// @param M Map whose root pointer is profiled.
  static inline void Profile(FoldingSetNodeID& ID, const ImmutableMap& M) {
    ID.AddPointer(M.Root.get());
  }

  /// Adds profile data for this map to \p ID, keyed by the tree root pointer.
  /// @param ID Folding-set ID to populate.
  inline void Profile(FoldingSetNodeID& ID) const {
    return Profile(ID,*this);
  }
};

/// Factory-backed mutable view of an immutable map.
///
/// Add and remove go through the associated factory without canonicalizing
/// after each operation. Convert to \c ImmutableMap via \c asImmutableMap when
/// a canonical root is needed.
/// NOTE: This may become the new ImmutableMap implementation someday.
template <typename KeyT, typename ValT,
typename ValInfo = ImutKeyValueInfo<KeyT,ValT>>
class ImmutableMapRef {
public:
  /// The type of a stored map entry (key and value pair).
  using value_type = typename ValInfo::value_type;
  /// A reference to a stored map entry.
  using value_type_ref = typename ValInfo::value_type_ref;
  /// The key type used for ordering and equality.
  using key_type = typename ValInfo::key_type;
  /// A reference to a map key.
  using key_type_ref = typename ValInfo::key_type_ref;
  /// The type of a stored map value.
  using data_type = typename ValInfo::data_type;
  /// A reference to a map value.
  using data_type_ref = typename ValInfo::data_type_ref;
  /// The underlying AVL tree type.
  using TreeTy = ImutAVLTree<ValInfo>;
  /// The factory type used to allocate and update tree nodes.
  using FactoryTy = typename TreeTy::Factory;

protected:
  /// The root of the underlying AVL tree.
  IntrusiveRefCntPtr<TreeTy> Root;
  /// The factory that allocates nodes for this map.
  FactoryTy *Factory;

public:
  /// Construct from tree root \p R and factory \p F.
  ///
  /// Prefer Factory methods except when a public constructor is useful.
  /// @param R Tree root pointer, or null for empty.
  /// @param F Factory that owns node allocation for this map.
  ImmutableMapRef(const TreeTy *R, FactoryTy *F)
      : Root(const_cast<TreeTy *>(R)), Factory(F) {}

  /// Constructs a mutable map reference from an ImmutableMap and its factory.
  ///
  /// Shares the same tree root without retaining it.
  /// @param X Source immutable map whose root is borrowed.
  /// @param F Factory that created \p X and owns node allocation.
  ImmutableMapRef(const ImmutableMap<KeyT, ValT> &X,
                  typename ImmutableMap<KeyT, ValT>::Factory &F)
      : Root(X.getRootWithoutRetain()), Factory(F.getTreeFactory()) {}

  /// Returns an empty map reference backed by \p F.
  /// @param F Factory that will allocate nodes for updates.
  /// @return An empty map reference using \p F.
  static inline ImmutableMapRef getEmptyMap(FactoryTy *F) {
    return ImmutableMapRef(nullptr, F);
  }

  /// Increments the reference count of the tree root.
  void manualRetain() {
    if (Root) Root->retain();
  }

  /// Decrements the reference count of the tree root, destroying the tree if it
  /// reaches zero.
  void manualRelease() {
    if (Root) Root->release();
  }

  /// Returns a new map with \p K mapped to \p D.
  ///
  /// Contains all entries of this map with \p K mapped to \p D. If this map
  /// already maps \p K to an equal value, the original tree is shared and no
  /// new nodes are allocated.
  /// @param K Key to insert or update.
  /// @param D Value to associate with \p K.
  /// @return A map with \p K mapped to \p D.
  ImmutableMapRef add(key_type_ref K, data_type_ref D) const {
    TreeTy *NewT =
        Factory->add(Root.get(), std::pair<key_type, data_type>(K, D));
    return ImmutableMapRef(NewT, Factory);
  }

  /// Returns a new map with \p K removed.
  ///
  /// If the key was absent, the original tree is shared and no new nodes are
  /// allocated.
  /// @param K Key to remove.
  /// @return A map without \p K.
  ImmutableMapRef remove(key_type_ref K) const {
    TreeTy *NewT = Factory->remove(Root.get(), K);
    return ImmutableMapRef(NewT, Factory);
  }

  /// Returns true if the map contains an entry with key \p K.
  /// @param K Key to test for membership.
  /// @return True if an entry with key \p K exists.
  [[nodiscard]] bool contains(key_type_ref K) const {
    return Root ? Root->contains(K) : false;
  }

  /// Convert this view to an \c ImmutableMap, canonicalizing the tree root.
  /// @return An \c ImmutableMap with a canonicalized tree root.
  ImmutableMap<KeyT, ValT> asImmutableMap() const {
    return ImmutableMap<KeyT, ValT>(Factory->getCanonicalTree(Root.get()));
  }

  /// Compares two map references for structural equality.
  /// @param RHS Other map reference to compare against.
  /// @return True if this map and \p RHS are structurally equal.
  [[nodiscard]] bool operator==(const ImmutableMapRef &RHS) const {
    return Root && RHS.Root ? Root->isEqual(*RHS.Root.get()) : Root == RHS.Root;
  }

  /// Compares two map references for structural inequality.
  /// @param RHS Other map reference to compare against.
  /// @return True if this map and \p RHS are structurally unequal.
  [[nodiscard]] bool operator!=(const ImmutableMapRef &RHS) const {
    return Root && RHS.Root ? Root->isNotEqual(*RHS.Root.get())
                            : Root != RHS.Root;
  }

  /// Returns true if the map contains no entries.
  /// @return True if the map contains no entries.
  [[nodiscard]] bool isEmpty() const { return !Root; }

  //===--------------------------------------------------===//
  // For testing.
  //===--------------------------------------------------===//

  /// Checks that the AVL balancing and ordering invariants hold for this map.
  void verify() const {
    if (Root)
      Root->verify();
  }

  //===--------------------------------------------------===//
  // Iterators.
  //===--------------------------------------------------===//

  /// In-order iterator over the key-value pairs of the map.
  class iterator : public ImutAVLValueIterator<ImmutableMapRef> {
    friend class ImmutableMapRef;

    iterator() = default;
    explicit iterator(TreeTy *Tree) : iterator::ImutAVLValueIterator(Tree) {}

  public:
    /// Returns the key (first element) of the current key-value pair.
    /// @return The key of the current entry.
    key_type_ref getKey() const { return (*this)->first; }
    /// Returns the value (second element) of the current key-value pair.
    /// @return The value of the current entry.
    data_type_ref getData() const { return (*this)->second; }
  };

  /// Returns an iterator to the first key-value pair in in-order traversal.
  /// @return Iterator to the first entry, or end if empty.
  [[nodiscard]] iterator begin() const { return iterator(Root.get()); }
  /// Returns the end iterator for in-order traversal.
  /// @return The end iterator.
  [[nodiscard]] iterator end() const { return iterator(); }

  /// Finds the value associated with \p K.
  ///
  /// Returns a pointer to the stored value, or nullptr if the key is not
  /// present.
  /// @param K Key to look up.
  /// @return Pointer to the stored value, or nullptr if \p K is absent.
  [[nodiscard]] data_type *lookup(key_type_ref K) const {
    if (Root) {
      TreeTy* T = Root->find(K);
      if (T) return &T->getValue().second;
    }

    return nullptr;
  }

  /// Returns the <key,value> pair in the ImmutableMap for which key is the
  /// highest in the ordering of keys in the map.  This method returns NULL if
  /// the map is empty.
  /// @return Pointer to the maximum key-value pair, or nullptr if empty.
  [[nodiscard]] value_type *getMaxElement() const {
    return Root ? &(Root->getMaxElement()->getValue()) : nullptr;
  }

  //===--------------------------------------------------===//
  // Utility methods.
  //===--------------------------------------------------===//

  /// Returns the height of the underlying AVL tree, or 0 if the map is empty.
  /// @return The AVL tree height, or 0 if empty.
  [[nodiscard]] unsigned getHeight() const {
    return Root ? Root->getHeight() : 0;
  }

  /// Adds profile data for \p M to \p ID, keyed by the tree root pointer.
  /// @param ID Folding-set ID to populate.
  /// @param M Map reference whose root pointer is profiled.
  static inline void Profile(FoldingSetNodeID &ID, const ImmutableMapRef &M) {
    ID.AddPointer(M.Root.get());
  }

  /// Adds profile data for this map to \p ID, keyed by the tree root pointer.
  /// @param ID Folding-set ID to populate.
  inline void Profile(FoldingSetNodeID &ID) const { return Profile(ID, *this); }
};

} // end namespace llvm

#endif // LLVM_ADT_IMMUTABLEMAP_H
