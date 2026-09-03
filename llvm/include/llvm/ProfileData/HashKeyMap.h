//===--- HashKeyMap.h - Wrapper for maps using hash value key ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// Defines HashKeyMap template.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_HASHKEYMAP_H
#define LLVM_PROFILEDATA_HASHKEYMAP_H

#include "llvm/ADT/Hashing.h"
#include <utility>

namespace llvm {

namespace sampleprof {

/// Map that stores values under the hash of the original key.
///
/// This class is a wrapper to associative container MapT<KeyT, ValueT> using
/// the hash value of the original key as the new key. This greatly improves the
/// performance of insert and query operations especially when hash values of
/// keys are available a priori, and reduces memory usage if KeyT has a large
/// size.
/// All keys with the same hash value are considered equivalent (i.e. hash
/// collision is silently ignored). Given such feature this class should only be
/// used where it does not affect compilation correctness, for example, when
/// loading a sample profile. The original key is not stored, so if the user
/// needs to preserve it, it should be stored in the mapped type.
/// Assuming the hashing algorithm is uniform, we use the formula
/// 1 - Permute(n, k) / n ^ k where n is the universe size and k is number of
/// elements chosen at random to calculate the probability of collision. With
/// 1,000,000 entries the probability is negligible:
/// 1 - (2^64)!/((2^64-1000000)!*(2^64)^1000000) ~= 3*10^-8.
/// Source: https://en.wikipedia.org/wiki/Birthday_problem
///
/// \param MapT The underlying associative container type.
/// \param KeyT The original key type, which requires the implementation of
///   llvm::hash_value(KeyT).
/// \param ValueT The original mapped type, which has the same requirement as
///   the underlying container.
/// \param MapTArgs Additional template parameters passed to the underlying
///   container.
template <template <typename, typename, typename...> typename MapT,
          typename KeyT, typename ValueT, typename... MapTArgs>
class HashKeyMap :
    public MapT<decltype(hash_value(KeyT())), ValueT, MapTArgs...> {
public:
  /// Underlying associative container type.
  using base_type = MapT<decltype(hash_value(KeyT())), ValueT, MapTArgs...>;
  /// Hash value type used as the map key.
  using key_type = decltype(hash_value(KeyT()));
  /// Original key type before hashing.
  using original_key_type = KeyT;
  /// Mapped value type.
  using mapped_type = ValueT;
  /// Key/value pair type of the underlying container.
  using value_type = typename base_type::value_type;

  /// Mutable iterator over map entries.
  using iterator = typename base_type::iterator;
  /// Constant iterator over map entries.
  using const_iterator = typename base_type::const_iterator;

  /// Insert a value for \p Hash if absent, using \p Key only for validation.
  /// \param Hash Hash of \p Key used as the map key.
  /// \param Key Original key; must hash to \p Hash.
  /// \param Args Arguments forwarded to construct the mapped value.
  /// \returns A pair of an iterator to the entry and whether it was inserted.
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(const key_type &Hash,
                                        const original_key_type &Key,
                                        Ts &&...Args) {
    assert(Hash == hash_value(Key));
    return base_type::try_emplace(Hash, std::forward<Ts>(Args)...);
  }

  /// Insert a value for the hash of \p Key if absent.
  /// \param Key Original key whose hash is used as the map key.
  /// \param Args Arguments forwarded to construct the mapped value.
  /// \returns A pair of an iterator to the entry and whether it was inserted.
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(const original_key_type &Key,
                                        Ts &&...Args) {
    return try_emplace(hash_value(Key), Key, std::forward<Ts>(Args)...);
  }

  /// Emplace a value by forwarding \p Args to try_emplace.
  /// \param Args Arguments forwarded to try_emplace.
  /// \returns A pair of an iterator to the entry and whether it was inserted.
  template <typename... Ts> std::pair<iterator, bool> emplace(Ts &&...Args) {
    return try_emplace(std::forward<Ts>(Args)...);
  }

  /// Return a reference to the value for \p Key, inserting a default if needed.
  /// \param Key Original key whose hash is used as the map key.
  /// \returns A reference to the mapped value for \p Key.
  mapped_type &operator[](const original_key_type &Key) {
    return try_emplace(Key, mapped_type()).first->second;
  }

  /// Find the entry for \p Key.
  /// \param Key Original key whose hash is looked up.
  /// \returns An iterator to the entry, or end() if not found.
  iterator find(const original_key_type &Key) {
    auto It = base_type::find(hash_value(Key));
    if (It != base_type::end())
      return It;
    return base_type::end();
  }

  /// Find the entry for \p Key.
  /// \param Key Original key whose hash is looked up.
  /// \returns A const iterator to the entry, or end() if not found.
  const_iterator find(const original_key_type &Key) const {
    auto It = base_type::find(hash_value(Key));
    if (It != base_type::end())
      return It;
    return base_type::end();
  }

  /// Return the value for \p Key, or a default-constructed value if absent.
  /// \param Key Original key whose hash is looked up.
  /// \returns The mapped value for \p Key, or a default-constructed value.
  mapped_type lookup(const original_key_type &Key) const {
    auto It = base_type::find(hash_value(Key));
    if (It != base_type::end())
      return It->second;
    return mapped_type();
  }

  /// Return 1 if \p Key is present, otherwise 0.
  /// \param Key Original key whose hash is looked up.
  /// \returns 1 if \p Key is present, otherwise 0.
  size_t count(const original_key_type &Key) const {
    return base_type::count(hash_value(Key));
  }

  /// Erase the entry for \p Ctx if present.
  /// \param Ctx Original key whose hash identifies the entry to remove.
  /// \returns 1 if an entry was erased, otherwise 0.
  size_t erase(const original_key_type &Ctx) {
    auto It = find(Ctx);
    if (It != base_type::end()) {
      base_type::erase(It);
      return 1;
    }
    return 0;
  }

  /// Erase the entry at \p It.
  /// \param It Iterator to the entry to remove.
  /// \returns An iterator following the erased entry.
  iterator erase(const_iterator It) {
    return base_type::erase(It);
  }

  /// Remove entries that match the given predicate.
  ///
  /// \p Pred is invoked with a reference to each entry.
  /// \param Pred Predicate invoked with each entry; entries for which it
  ///   returns true are erased.
  /// \returns True if any entry was removed.
  template <typename Predicate> bool remove_if(Predicate Pred) {
    bool Removed = false;
    for (auto It = base_type::begin(), E = base_type::end(); It != E;) {
      if (Pred(*It)) {
        It = base_type::erase(It);
        Removed = true;
      } else {
        ++It;
      }
    }
    return Removed;
  }
};

}

}

#endif // LLVM_PROFILEDATA_HASHKEYMAP_H
