//===- llvm/ADT/MapVector.h - Map w/ deterministic value order --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a map that provides insertion order iteration. The
/// interface is purposefully minimal. The key is assumed to be cheap to copy
/// and 2 copies are kept, one for indexing in a DenseMap, one for iteration in
/// a SmallVector.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_MAPVECTOR_H
#define LLVM_ADT_MAPVECTOR_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace llvm {

/// This class implements a map that also provides access to all stored values in a deterministic order.
///
/// The values are kept in a SmallVector<*, 0> and the mapping is done with DenseMap from Keys to indexes in that vector.
template <typename KeyT, typename ValueT,
          typename MapType = DenseMap<KeyT, unsigned>,
          typename VectorType = SmallVector<std::pair<KeyT, ValueT>, 0>,
          unsigned N = 0>
class MapVector {
public:
  /// Key type stored in the map.
  using key_type = KeyT;
  /// Key/value pair type stored in insertion order.
  using value_type = typename VectorType::value_type;
  /// Unsigned size type for element counts.
  using size_type = typename VectorType::size_type;

  /// Mutable iterator over key/value pairs in insertion order.
  using iterator = typename VectorType::iterator;
  /// Const iterator over key/value pairs in insertion order.
  using const_iterator = typename VectorType::const_iterator;
  /// Mutable reverse iterator over key/value pairs.
  using reverse_iterator = typename VectorType::reverse_iterator;
  /// Const reverse iterator over key/value pairs.
  using const_reverse_iterator = typename VectorType::const_reverse_iterator;

  /// Clear the MapVector and return the underlying vector.
  [[nodiscard]] VectorType takeVector() {
    Map.clear();
    return std::move(Vector);
  }

  /// Returns an array reference of the underlying vector.
  [[nodiscard]] ArrayRef<value_type> getArrayRef() const { return Vector; }

  /// Return the number of key/value pairs.
  [[nodiscard]] size_type size() const { return Vector.size(); }

  /// Grow the MapVector so that it can contain at least \p NumEntries items
  /// before resizing again.
  void reserve(size_type NumEntries) {
    Map.reserve(NumEntries);
    Vector.reserve(NumEntries);
  }

  /// Return an iterator to the first inserted pair.
  [[nodiscard]] iterator begin() { return Vector.begin(); }
  /// Return a const iterator to the first inserted pair.
  [[nodiscard]] const_iterator begin() const { return Vector.begin(); }
  /// Return a past-the-end iterator.
  [[nodiscard]] iterator end() { return Vector.end(); }
  /// Return a const past-the-end iterator.
  [[nodiscard]] const_iterator end() const { return Vector.end(); }

  /// Return a reverse iterator to the last inserted pair.
  [[nodiscard]] reverse_iterator rbegin() { return Vector.rbegin(); }
  /// Return a const reverse iterator to the last inserted pair.
  [[nodiscard]] const_reverse_iterator rbegin() const {
    return Vector.rbegin();
  }
  /// Return a reverse past-the-end iterator.
  [[nodiscard]] reverse_iterator rend() { return Vector.rend(); }
  /// Return a const reverse past-the-end iterator.
  [[nodiscard]] const_reverse_iterator rend() const { return Vector.rend(); }

  /// Return true if the MapVector contains no elements.
  [[nodiscard]] bool empty() const { return Vector.empty(); }

  /// Return a reference to the first inserted key/value pair.
  [[nodiscard]] std::pair<KeyT, ValueT> &front() { return Vector.front(); }
  /// Return a const reference to the first inserted key/value pair.
  [[nodiscard]] const std::pair<KeyT, ValueT> &front() const {
    return Vector.front();
  }
  /// Return a reference to the last inserted key/value pair.
  [[nodiscard]] std::pair<KeyT, ValueT> &back() { return Vector.back(); }
  /// Return a const reference to the last inserted key/value pair.
  [[nodiscard]] const std::pair<KeyT, ValueT> &back() const {
    return Vector.back();
  }

  /// Remove all keys and values.
  void clear() {
    Map.clear();
    Vector.clear();
  }

  /// Exchange contents with \p RHS.
  /// @param RHS Other MapVector to swap with.
  void swap(MapVector &RHS) {
    std::swap(Map, RHS.Map);
    std::swap(Vector, RHS.Vector);
  }

  /// Return a reference to the value for \p Key, default-inserting if absent.
  /// @param Key Key to look up or insert.
  ValueT &operator[](const KeyT &Key) {
    return try_emplace_impl(Key).first->second;
  }

  /// Return a range over keys in insertion order.
  [[nodiscard]] auto keys() { return make_first_range(Vector); }
  /// Return a const range over keys in insertion order.
  [[nodiscard]] auto keys() const { return make_first_range(Vector); }
  /// Return a range over values in insertion order.
  [[nodiscard]] auto values() { return make_second_range(Vector); }
  /// Return a const range over values in insertion order.
  [[nodiscard]] auto values() const { return make_second_range(Vector); }

  /// Return a copy of the value for \p Key, or a default-constructed value.
  ///
  /// Only allowed if ValueT is copyable.
  /// @param Key Key to look up.
  [[nodiscard]] ValueT lookup(const KeyT &Key) const {
    static_assert(std::is_copy_constructible_v<ValueT>,
                  "Cannot call lookup() if ValueT is not copyable.");
    auto I = find(Key);
    return I == end() ? ValueT() : I->second;
  }

  /// Insert a default-constructed value for \p Key if absent, constructing
  /// with \p Args when inserting.
  /// @param Key Key to insert or look up.
  /// @param Args Arguments forwarded to the value constructor on insert.
  /// @return Pair of iterator to the element and whether it was inserted.
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(const KeyT &Key, Ts &&...Args) {
    return try_emplace_impl(Key, std::forward<Ts>(Args)...);
  }
  /// Insert a default-constructed value for a moved \p Key if absent.
  /// @param Key Key to insert or look up.
  /// @param Args Arguments forwarded to the value constructor on insert.
  /// @return Pair of iterator to the element and whether it was inserted.
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(KeyT &&Key, Ts &&...Args) {
    return try_emplace_impl(std::move(Key), std::forward<Ts>(Args)...);
  }

  /// Insert \p KV if the key is not already present.
  /// @param KV Key/value pair to insert.
  /// @return Pair of iterator to the element and whether it was inserted.
  std::pair<iterator, bool> insert(const std::pair<KeyT, ValueT> &KV) {
    return try_emplace_impl(KV.first, KV.second);
  }
  /// Insert a moved \p KV if the key is not already present.
  /// @param KV Key/value pair to insert.
  /// @return Pair of iterator to the element and whether it was inserted.
  std::pair<iterator, bool> insert(std::pair<KeyT, ValueT> &&KV) {
    return try_emplace_impl(std::move(KV.first), std::move(KV.second));
  }

  /// Insert or overwrite the value for \p Key with \p Val.
  /// @param Key Key to insert or assign.
  /// @param Val Value to store.
  /// @return Pair of iterator to the element and whether it was newly inserted.
  template <typename V>
  std::pair<iterator, bool> insert_or_assign(const KeyT &Key, V &&Val) {
    auto Ret = try_emplace(Key, std::forward<V>(Val));
    if (!Ret.second)
      Ret.first->second = std::forward<V>(Val);
    return Ret;
  }
  /// Insert or overwrite the value for a moved \p Key with \p Val.
  /// @param Key Key to insert or assign.
  /// @param Val Value to store.
  /// @return Pair of iterator to the element and whether it was newly inserted.
  template <typename V>
  std::pair<iterator, bool> insert_or_assign(KeyT &&Key, V &&Val) {
    auto Ret = try_emplace(std::move(Key), std::forward<V>(Val));
    if (!Ret.second)
      Ret.first->second = std::forward<V>(Val);
    return Ret;
  }

  /// Return true if \p Key is present.
  /// @param Key Key to look up.
  [[nodiscard]] bool contains(const KeyT &Key) const {
    return find(Key) != end();
  }

  /// Return 1 if \p Key is present, otherwise 0.
  /// @param Key Key to look up.
  [[nodiscard]] size_type count(const KeyT &Key) const {
    return contains(Key) ? 1 : 0;
  }

  /// Return an iterator to \p Key, or end() if absent.
  /// @param Key Key to look up.
  [[nodiscard]] iterator find(const KeyT &Key) {
    if constexpr (canBeSmall())
      if (isSmall())
        return findInVector(Vector, Key);

    typename MapType::const_iterator Pos = Map.find(Key);
    return Pos == Map.end() ? Vector.end() : (Vector.begin() + Pos->second);
  }

  /// Return a const iterator to \p Key, or end() if absent.
  /// @param Key Key to look up.
  [[nodiscard]] const_iterator find(const KeyT &Key) const {
    if constexpr (canBeSmall())
      if (isSmall())
        return findInVector(Vector, Key);

    typename MapType::const_iterator Pos = Map.find(Key);
    return Pos == Map.end() ? Vector.end() : (Vector.begin() + Pos->second);
  }

  /// at - Return the entry for the specified key, or abort if no such
  /// entry exists.
  [[nodiscard]] ValueT &at(const KeyT &Key) {
    auto I = find(Key);
    assert(I != end() && "MapVector::at failed due to a missing key");
    return I->second;
  }

  /// at - Return the entry for the specified key, or abort if no such
  /// entry exists.
  [[nodiscard]] const ValueT &at(const KeyT &Key) const {
    auto I = find(Key);
    assert(I != end() && "MapVector::at failed due to a missing key");
    return I->second;
  }

  /// Remove the last element from the vector.
  void pop_back() {
    if constexpr (canBeSmall())
      if (isSmall()) {
        Vector.pop_back();
        return;
      }

    typename MapType::iterator Pos = Map.find(Vector.back().first);
    Map.erase(Pos);
    Vector.pop_back();
  }

  /// Remove the element given by Iterator.
  ///
  /// Returns an iterator to the element following the one which was removed,
  /// which may be end().
  ///
  /// \note This is a deceivingly expensive operation (linear time).  It's
  /// usually better to use \a remove_if() if possible.
  typename VectorType::iterator erase(typename VectorType::iterator Iterator) {
    if constexpr (canBeSmall())
      if (isSmall())
        return Vector.erase(Iterator);

    Map.erase(Iterator->first);
    auto Next = Vector.erase(Iterator);
    if (Next == Vector.end())
      return Next;

    // Update indices in the map.
    size_t Index = Next - Vector.begin();
    for (auto &I : Map) {
      assert(I.second != Index && "Index was already erased!");
      if (I.second > Index)
        --I.second;
    }
    return Next;
  }

  /// Remove all elements with the key value Key.
  ///
  /// Returns the number of elements removed.
  size_type erase(const KeyT &Key) {
    auto Iterator = find(Key);
    if (Iterator == end())
      return 0;
    erase(Iterator);
    return 1;
  }

  /// Remove the elements that match the predicate.
  ///
  /// Erase all elements that match \c Pred in a single pass.  Takes linear
  /// time.
  template <class Predicate> void remove_if(Predicate Pred);

  /// Return the approximate size (in bytes) of the data structure.
  /// This is just the raw memory used by MapVector.
  /// If entries are pointers to objects, the size of the referenced objects
  /// are not included.
  [[nodiscard]] size_t getMemorySize() const {
    return capacity_in_bytes(Map) + capacity_in_bytes(Vector);
  }

private:
  template <typename VectorT, typename LookupKeyT>
  [[nodiscard]] static auto findInVector(VectorT &Vec, const LookupKeyT &Key) {
    return find_if(Vec, [&Key](const auto &P) { return P.first == Key; });
  }

  [[nodiscard]] static constexpr bool canBeSmall() { return N != 0; }

  [[nodiscard]] bool isSmall() const { return Map.empty(); }

  void makeBig() {
    if constexpr (canBeSmall()) {
      unsigned Index = 0;
      for (const auto &entry : Vector)
        Map[entry.first] = Index++;
    }
  }

  MapType Map;
  VectorType Vector;

  static_assert(N <= 32, "Small size should be less than or equal to 32!");

  static_assert(
      std::is_integral_v<typename MapType::mapped_type>,
      "The mapped_type of the specified Map must be an integral type");

  template <typename KeyArgT, typename... Ts>
  std::pair<iterator, bool> try_emplace_impl(KeyArgT &&Key, Ts &&...Args) {
    if constexpr (canBeSmall())
      if (isSmall()) {
        auto I = findInVector(Vector, Key);
        if (I != Vector.end())
          return {I, false};
        Vector.emplace_back(std::piecewise_construct,
                            std::forward_as_tuple(std::forward<KeyArgT>(Key)),
                            std::forward_as_tuple(std::forward<Ts>(Args)...));
        if (Vector.size() > N)
          makeBig();
        return {std::prev(end()), true};
      }

    auto [It, Inserted] = Map.try_emplace(Key);
    if (Inserted) {
      It->second = Vector.size();
      Vector.emplace_back(std::piecewise_construct,
                          std::forward_as_tuple(std::forward<KeyArgT>(Key)),
                          std::forward_as_tuple(std::forward<Ts>(Args)...));
      return {std::prev(end()), true};
    }
    return {begin() + It->second, false};
  }
};

template <typename KeyT, typename ValueT, typename MapType, typename VectorType,
          unsigned N>
template <class Function>
void MapVector<KeyT, ValueT, MapType, VectorType, N>::remove_if(Function Pred) {
  if constexpr (canBeSmall())
    if (isSmall()) {
      Vector.erase(llvm::remove_if(Vector, Pred), Vector.end());
      return;
    }

  auto O = Vector.begin();
  for (auto I = O, E = Vector.end(); I != E; ++I) {
    if (Pred(*I)) {
      // Erase from the map.
      Map.erase(I->first);
      continue;
    }

    if (I != O) {
      // Move the value and update the index in the map.
      *O = std::move(*I);
      Map[O->first] = O - Vector.begin();
    }
    ++O;
  }
  // Erase trailing entries in the vector.
  Vector.erase(O, Vector.end());
}

/// A MapVector that performs no allocations if smaller than a certain
/// size.
template <typename KeyT, typename ValueT, unsigned N>
struct SmallMapVector : MapVector<KeyT, ValueT, DenseMap<KeyT, unsigned>,
                                  SmallVector<std::pair<KeyT, ValueT>, N>, N> {
};

/// Return the approximate size in bytes of the MapVector \p X.
/// @param X MapVector whose storage size is queried.
template <typename KeyT, typename ValueT,
          typename MapType = DenseMap<KeyT, unsigned>,
          typename VectorType = SmallVector<std::pair<KeyT, ValueT>, 0>,
          unsigned N = 0>
[[nodiscard]] size_t
capacity_in_bytes(const MapVector<KeyT, ValueT, MapType, VectorType, N> &X) {
  return X.getMemorySize();
}

} // end namespace llvm

#endif // LLVM_ADT_MAPVECTOR_H
