//===- llvm/ADT/SortedVectorMap.h - Map backed by SmallVector *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a map backed by a sorted SmallVector. It provides a
/// std::map-like interface with binary search lookup while maintaining
/// contiguous memory layout and dense cache locality.
///
/// SortedVectorMap is intended for:
/// - Small maps where memory footprint is a primary concern. In particular, it
///   avoids the initial bucket overhead of DenseMap (e.g. 64 buckets by
///   default) when only a few elements are stored.
/// - Use cases that require iteration in sorted key order.
///
/// Trade-offs:
/// - Lookups take O(log N) time via binary search rather than O(1) in DenseMap.
/// - Insertions and deletions take O(N) time due to shifting elements in the
///   underlying vector, making it best suited for small N or mostly-read data.
/// - Compared to std::map, elements are stored contiguously, eliminating
///   per-node heap allocations and pointer chasing.
/// - Compared to MapVector, elements are ordered by key rather than insertion
///   order, with zero auxiliary hash table overhead.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SORTEDVECTORMAP_H
#define LLVM_ADT_SORTEDVECTORMAP_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include <functional>
#include <tuple>
#include <utility>

namespace llvm {

/// A map implementation backed by a sorted SmallVector.
/// Key-value pairs are stored in contiguous memory ordered by \p KeyCompare.
template <typename KeyT, typename ValueT, unsigned N = 0,
          typename KeyCompare = std::less<KeyT>>
class SortedVectorMap {
public:
  /// Key type stored in the map.
  using key_type = KeyT;
  /// Mapped value type.
  using mapped_type = ValueT;
  /// Key/value pair type stored in the underlying vector.
  using value_type = std::pair<KeyT, ValueT>;
  /// Contiguous storage type for sorted key/value pairs.
  using VectorType = SmallVector<value_type, N>;
  /// Unsigned size type of the underlying vector.
  using size_type = typename VectorType::size_type;

  /// Mutable iterator over key/value pairs in sorted key order.
  using iterator = typename VectorType::iterator;
  /// Const iterator over key/value pairs in sorted key order.
  using const_iterator = typename VectorType::const_iterator;
  /// Mutable reverse iterator over key/value pairs.
  using reverse_iterator = typename VectorType::reverse_iterator;
  /// Const reverse iterator over key/value pairs.
  using const_reverse_iterator = typename VectorType::const_reverse_iterator;

private:
  VectorType Vector;
  LLVM_NO_UNIQUE_ADDRESS KeyCompare Comp;

  template <typename K1, typename K2>
  bool is_equal(const K1 &A, const K2 &B) const {
    return !Comp(A, B) && !Comp(B, A);
  }

  template <typename K> const_iterator lower_bound(const K &Key) const {
    return llvm::lower_bound(Vector, Key,
                             [this](const value_type &E, const K &KeyVal) {
                               return Comp(E.first, KeyVal);
                             });
  }

  template <typename K>
  std::pair<const_iterator, bool> find_or_insert_location(const K &Key) const {
    if (!Vector.empty() && Comp(Vector.back().first, Key))
      return {Vector.end(), false};
    auto It = lower_bound(Key);
    bool Found = (It != Vector.end() && is_equal(Key, It->first));
    return {It, Found};
  }

  template <typename K>
  std::pair<iterator, bool> find_or_insert_location(const K &Key) {
    auto [ConstIt, Found] = std::as_const(*this).find_or_insert_location(Key);
    return {Vector.begin() + (ConstIt - Vector.begin()), Found};
  }

  template <typename KeyArgT, typename... Ts>
  std::pair<iterator, bool> try_emplace_impl(KeyArgT &&Key, Ts &&...Args) {
    auto [It, Found] = find_or_insert_location(Key);
    if (Found)
      return {It, false};
    It = Vector.insert(
        It, value_type(std::piecewise_construct,
                       std::forward_as_tuple(std::forward<KeyArgT>(Key)),
                       std::forward_as_tuple(std::forward<Ts>(Args)...)));
    return {It, true};
  }

public:
  /// Construct an empty map.
  SortedVectorMap() = default;

  /// Return an iterator to the first key/value pair.
  /// @return Iterator to the first key/value pair.
  iterator begin() { return Vector.begin(); }
  /// Return an iterator past the last key/value pair.
  /// @return Iterator past the last key/value pair.
  iterator end() { return Vector.end(); }
  /// Return a const iterator to the first key/value pair.
  /// @return Const iterator to the first key/value pair.
  const_iterator begin() const { return Vector.begin(); }
  /// Return a const iterator past the last key/value pair.
  /// @return Const iterator past the last key/value pair.
  const_iterator end() const { return Vector.end(); }
  /// Return a const iterator to the first key/value pair.
  /// @return Const iterator to the first key/value pair.
  const_iterator cbegin() const { return Vector.begin(); }
  /// Return a const iterator past the last key/value pair.
  /// @return Const iterator past the last key/value pair.
  const_iterator cend() const { return Vector.end(); }

  /// Return a reverse iterator to the last key/value pair.
  /// @return Reverse iterator to the last key/value pair.
  reverse_iterator rbegin() { return Vector.rbegin(); }
  /// Return a reverse iterator before the first key/value pair.
  /// @return Reverse iterator before the first key/value pair.
  reverse_iterator rend() { return Vector.rend(); }
  /// Return a const reverse iterator to the last key/value pair.
  /// @return Const reverse iterator to the last key/value pair.
  const_reverse_iterator rbegin() const { return Vector.rbegin(); }
  /// Return a const reverse iterator before the first key/value pair.
  /// @return Const reverse iterator before the first key/value pair.
  const_reverse_iterator rend() const { return Vector.rend(); }
  /// Return a const reverse iterator to the last key/value pair.
  /// @return Const reverse iterator to the last key/value pair.
  const_reverse_iterator crbegin() const { return Vector.rbegin(); }
  /// Return a const reverse iterator before the first key/value pair.
  /// @return Const reverse iterator before the first key/value pair.
  const_reverse_iterator crend() const { return Vector.rend(); }

  /// Return true if the map contains no entries.
  /// @return True if the map contains no entries.
  [[nodiscard]] bool empty() const { return Vector.empty(); }
  /// Return the number of key/value pairs.
  /// @return The number of key/value pairs.
  size_type size() const { return Vector.size(); }
  /// Return the capacity of the underlying vector.
  /// @return The capacity of the underlying vector.
  size_type capacity() const { return Vector.capacity(); }
  /// Reserve capacity for at least \p Cap key/value pairs.
  /// @param Cap Minimum number of key/value pairs to reserve space for.
  void reserve(size_type Cap) { Vector.reserve(Cap); }

  /// Find \p Key, or return \c end() if it is absent.
  /// @param Key Key to look up.
  /// @return Iterator to the matching entry, or \c end() if absent.
  template <typename K> const_iterator find(const K &Key) const {
    auto [It, Found] = find_or_insert_location(Key);
    return Found ? It : Vector.end();
  }

  /// Find \p Key, or return \c end() if it is absent.
  /// @param Key Key to look up.
  /// @return Iterator to the matching entry, or \c end() if absent.
  template <typename K> iterator find(const K &Key) {
    auto [It, Found] = find_or_insert_location(Key);
    return Found ? It : Vector.end();
  }

  /// Insert a value constructed from \p Args under \p Key if absent.
  /// @param Key Key to insert or look up.
  /// @param Args Arguments forwarded to the value constructor on insert.
  /// @return Pair of iterator to the entry and whether insertion occurred.
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(const KeyT &Key, Ts &&...Args) {
    return try_emplace_impl(Key, std::forward<Ts>(Args)...);
  }

  /// Insert a value constructed from \p Args under moved \p Key if absent.
  /// @param Key Key to insert or look up.
  /// @param Args Arguments forwarded to the value constructor on insert.
  /// @return Pair of iterator to the entry and whether insertion occurred.
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(KeyT &&Key, Ts &&...Args) {
    return try_emplace_impl(std::move(Key), std::forward<Ts>(Args)...);
  }

  /// Insert \p KV if its key is not already present.
  /// @param KV Key/value pair to insert.
  /// @return Pair of iterator to the entry and whether insertion occurred.
  std::pair<iterator, bool> insert(const value_type &KV) {
    return try_emplace_impl(KV.first, KV.second);
  }

  /// Insert moved \p KV if its key is not already present.
  /// @param KV Key/value pair to insert.
  /// @return Pair of iterator to the entry and whether insertion occurred.
  std::pair<iterator, bool> insert(value_type &&KV) {
    return try_emplace_impl(std::move(KV.first), std::move(KV.second));
  }

  /// Return a reference to the value for \p Key, default-inserting if needed.
  /// @param Key Key to look up or insert.
  /// @return Reference to the value for \p Key.
  ValueT &operator[](const KeyT &Key) {
    return try_emplace_impl(Key).first->second;
  }

  /// Return a reference to the value for moved \p Key, default-inserting if needed.
  /// @param Key Key to look up or insert.
  /// @return Reference to the value for \p Key.
  ValueT &operator[](KeyT &&Key) {
    return try_emplace_impl(std::move(Key)).first->second;
  }

  /// Erase the element at \p Pos and return an iterator to the following element.
  /// @param Pos Iterator to the element to erase.
  /// @return Iterator to the element following the erased one.
  iterator erase(iterator Pos) { return Vector.erase(Pos); }
  /// Erase the element at \p Pos and return an iterator to the following element.
  /// @param Pos Iterator to the element to erase.
  /// @return Iterator to the element following the erased one.
  iterator erase(const_iterator Pos) { return Vector.erase(Pos); }

  /// Return true if this map and \p Other have identical contents.
  /// @param Other Map to compare against.
  /// @return True if both maps have identical contents.
  bool operator==(const SortedVectorMap &Other) const {
    return Vector == Other.Vector;
  }
};
} // namespace llvm

#endif // LLVM_ADT_SORTEDVECTORMAP_H
