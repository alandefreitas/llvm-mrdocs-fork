//===- ValueMap.h - Safe map from Values to data ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ValueMap class.  ValueMap maps Value* or any subclass
// to an arbitrary other type.  It provides the DenseMap interface but updates
// itself to remain safe when keys are RAUWed or deleted.  By default, when a
// key is RAUWed from V1 to V2, the old mapping V1->target is removed, and a new
// mapping V2->target is added.  If V2 already existed, its old target is
// overwritten.  When a key is deleted, its mapping is removed.
//
// You can override a ValueMap's Config parameter to control exactly what
// happens on RAUW and destruction and to get called back on each event.  It's
// legal to call back into the ValueMap from a Config's callbacks.  Config
// parameters should inherit from ValueMapConfig<KeyT> to get default
// implementations of all the methods ValueMap uses.  See ValueMapConfig for
// documentation of the functions you can override.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_VALUEMAP_H
#define LLVM_IR_VALUEMAP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/IR/TrackingMDRef.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Mutex.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace llvm {

template <typename KeyT, typename ValueT, typename Config>
class ValueMapCallbackVH;
template <typename DenseMapT, typename KeyT, bool IsConst>
class ValueMapIteratorImpl;

/// Default configuration for configurable aspects of ValueMap.
///
/// User Configs should inherit from this class to be as compatible as possible
/// with future versions of ValueMap.
template <typename KeyT, typename MutexT = sys::Mutex> struct ValueMapConfig {
  /// Mutex type used when Config requests locking around CallbackVH updates.
  using mutex_type = MutexT;

  /// Compile-time flags that control how ValueMap reacts to key changes.
  enum {
    /// Whether the map remaps keys when they are RAUWd.
    ///
    /// If true, the ValueMap will update mappings on RAUW. If false, the
    /// ValueMap will leave the original mapping in place.
    FollowRAUW = true
  };

  // All methods will be called with a first argument of type ExtraData.  The
  // default implementations in this class take a templated first argument so
  // that users' subclasses can use any type they want without having to
  // override all the defaults.
  /// Extra per-map state passed to Config callbacks; empty by default.
  struct ExtraData {};

  /// Called when a key is RAUWd; default implementation does nothing.
  /// \param Data Extra configuration data for this map.
  /// \param Old The key being replaced.
  /// \param New The key that replaces \p Old.
  template <typename ExtraDataT>
  static void onRAUW([[maybe_unused]] const ExtraDataT &Data,
                     [[maybe_unused]] KeyT Old, [[maybe_unused]] KeyT New) {}
  /// Called when a key is deleted; default implementation does nothing.
  /// \param Data Extra configuration data for this map.
  /// \param Old The key being deleted.
  template <typename ExtraDataT>
  static void onDelete([[maybe_unused]] const ExtraDataT &Data,
                       [[maybe_unused]] KeyT Old) {}

  /// Return a mutex to acquire around CallbackVH-driven map changes, or null.
  ///
  /// This is only acquired from the CallbackVH (and held around calls to onRAUW
  /// and onDelete) and not inside other ValueMap methods.  NULL means that no
  /// mutex is necessary.
  /// \param Data Extra configuration data for this map.
  /// \return A pointer to the mutex, or null if locking is unnecessary.
  template <typename ExtraDataT>
  static mutex_type *getMutex([[maybe_unused]] const ExtraDataT &Data) {
    return nullptr;
  }
};

/// See the file comment.
template <typename KeyT, typename ValueT,
          typename Config = ValueMapConfig<KeyT>>
class ValueMap {
  friend class ValueMapCallbackVH<KeyT, ValueT, Config>;

  using ValueMapCVH = ValueMapCallbackVH<KeyT, ValueT, Config>;
  using MapT = DenseMap<ValueMapCVH, ValueT, DenseMapInfo<ValueMapCVH>>;
  using MDMapT = DenseMap<const Metadata *, TrackingMDRef>;
  /// Map {(InlinedAt, old atom number) -> new atom number}.
  using DMAtomT = SmallDenseMap<std::pair<Metadata *, uint64_t>, uint64_t>;
  using ExtraData = typename Config::ExtraData;

  MapT Map;
  std::optional<MDMapT> MDMap;
  ExtraData Data;

public:
  /// The map's key type.
  using key_type = KeyT;
  /// The map's mapped value type.
  using mapped_type = ValueT;
  /// The map's value type (key/value pair).
  using value_type = std::pair<KeyT, ValueT>;
  /// Unsigned type used for map sizes.
  using size_type = unsigned;

  /// Construct an empty map with \p NumInitBuckets initial buckets.
  /// \param NumInitBuckets Initial number of hash buckets.
  explicit ValueMap(unsigned NumInitBuckets = 64)
      : Map(NumInitBuckets), Data() {}
  /// Construct an empty map with extra config data and \p NumInitBuckets.
  /// \param Data Extra configuration data stored in the map.
  /// \param NumInitBuckets Initial number of hash buckets.
  explicit ValueMap(const ExtraData &Data, unsigned NumInitBuckets = 64)
      : Map(NumInitBuckets), Data(Data) {}
  // ValueMap can't be copied nor moved, because the callbacks store pointer to
  // it.
  /// Copy construction is deleted; CallbackVH keys store a pointer to the map.
  /// \param Other Unused; copy construction is deleted.
  ValueMap(const ValueMap &Other) = delete;
  /// Move construction is deleted; CallbackVH keys store a pointer to the map.
  /// \param Other Unused; move construction is deleted.
  ValueMap(ValueMap &&Other) = delete;
  /// Copy assignment is deleted; CallbackVH keys store a pointer to the map.
  /// \param Other Unused; copy assignment is deleted.
  ValueMap &operator=(const ValueMap &Other) = delete;
  /// Move assignment is deleted; CallbackVH keys store a pointer to the map.
  /// \param Other Unused; move assignment is deleted.
  ValueMap &operator=(ValueMap &&Other) = delete;

  /// Return true if this map has a metadata map.
  /// \return True if a metadata map is present.
  bool hasMD() const { return bool(MDMap); }
  /// Return the metadata map, creating it if absent.
  /// \return A reference to the metadata map.
  MDMapT &MD() {
    if (!MDMap)
      MDMap.emplace();
    return *MDMap;
  }
  /// Return a reference to the optional metadata map.
  /// \return A reference to the optional metadata map.
  std::optional<MDMapT> &getMDMap() { return MDMap; }
  /// Map {(InlinedAt, old atom number) -> new atom number}.
  DMAtomT AtomMap;

  /// Get the mapped metadata, if it's in the map.
  /// \param MD Metadata key to look up.
  /// \return The mapped metadata, or std::nullopt if absent.
  std::optional<Metadata *> getMappedMD(const Metadata *MD) const {
    if (!MDMap)
      return std::nullopt;
    auto Where = MDMap->find(MD);
    if (Where == MDMap->end())
      return std::nullopt;
    return Where->second.get();
  }

  /// Mutable iterator over map entries.
  using iterator = ValueMapIteratorImpl<MapT, KeyT, false>;
  /// Const iterator over map entries.
  using const_iterator = ValueMapIteratorImpl<MapT, KeyT, true>;

  /// Return an iterator to the beginning of the map.
  /// \return A mutable iterator to the first entry.
  inline iterator begin() { return iterator(Map.begin()); }
  /// Return an iterator to the end of the map.
  /// \return A mutable iterator past the last entry.
  inline iterator end() { return iterator(Map.end()); }
  /// Return a const iterator to the beginning of the map.
  /// \return A const iterator to the first entry.
  inline const_iterator begin() const { return const_iterator(Map.begin()); }
  /// Return a const iterator to the end of the map.
  /// \return A const iterator past the last entry.
  inline const_iterator end() const { return const_iterator(Map.end()); }

  /// Return true if the map contains no entries.
  /// \return True if the map is empty.
  bool empty() const { return Map.empty(); }
  /// Return the number of entries in the map.
  /// \return The number of key/value entries.
  size_type size() const { return Map.size(); }

  /// Grow the map so that it has at least \p Size buckets. Does not shrink.
  /// \param Size Minimum number of buckets to reserve.
  void reserve(size_t Size) { Map.reserve(Size); }

  /// Remove all entries from the map, including metadata and atom maps.
  void clear() {
    Map.clear();
    MDMap.reset();
    AtomMap.clear();
  }

  /// Return 1 if the specified key is in the map, 0 otherwise.
  /// \param Val Key to look up.
  /// \return 1 if \p Val is present, otherwise 0.
  size_type count(const KeyT &Val) const {
    return Map.find_as(Val) == Map.end() ? 0 : 1;
  }

  /// Find \p Val in the map, or return end() if absent.
  /// \param Val Key to look up.
  /// \return An iterator to the entry, or end() if not found.
  iterator find(const KeyT &Val) { return iterator(Map.find_as(Val)); }
  /// Find \p Val in the map, or return end() if absent.
  /// \param Val Key to look up.
  /// \return A const iterator to the entry, or end() if not found.
  const_iterator find(const KeyT &Val) const {
    return const_iterator(Map.find_as(Val));
  }

  /// Return the entry for the specified key, or a default-constructed value.
  /// \param Val Key to look up.
  /// \return The mapped value, or a default-constructed ValueT if absent.
  ValueT lookup(const KeyT &Val) const {
    typename MapT::const_iterator I = Map.find_as(Val);
    return I != Map.end() ? I->second : ValueT();
  }

  /// Insert \p KV if its key is not already in the map.
  /// If the key is already in the map, it returns false and doesn't update the
  /// value.
  /// \param KV Key/value pair to insert.
  /// \return A pair of an iterator to the entry and whether insertion occurred.
  std::pair<iterator, bool> insert(const std::pair<KeyT, ValueT> &KV) {
    return Map.insert(std::make_pair(Wrap(KV.first), KV.second));
  }

  /// Insert \p KV if its key is not already in the map.
  /// If the key is already in the map, it returns false and doesn't update the
  /// value.
  /// \param KV Key/value pair to insert.
  /// \return A pair of an iterator to the entry and whether insertion occurred.
  std::pair<iterator, bool> insert(std::pair<KeyT, ValueT> &&KV) {
    return Map.insert(std::make_pair(Wrap(KV.first), std::move(KV.second)));
  }

  /// Insert the key/value pairs in the range [\p I, \p E).
  /// \param I Beginning of the range of pairs to insert.
  /// \param E End of the range of pairs to insert.
  template <typename InputIt> void insert(InputIt I, InputIt E) {
    for (; I != E; ++I)
      insert(*I);
  }

  /// Erase the entry for \p Val if present; return whether an entry was removed.
  /// \param Val Key to erase.
  /// \return True if an entry was removed.
  bool erase(const KeyT &Val) {
    typename MapT::iterator I = Map.find_as(Val);
    if (I == Map.end())
      return false;

    Map.erase(I);
    return true;
  }
  /// Erase the entry at iterator \p I.
  /// \param I Iterator to the entry to erase.
  void erase(iterator I) { return Map.erase(I.base()); }

  /// Return a reference to the entry for \p Key, inserting a default if absent.
  /// \param Key Key to find or default-construct.
  /// \return A reference to the key/value entry for \p Key.
  value_type &FindAndConstruct(const KeyT &Key) {
    return Map.FindAndConstruct(Wrap(Key));
  }

  /// Return a reference to the value for \p Key, inserting a default if absent.
  /// \param Key Key to find or default-construct.
  /// \return A reference to the mapped value for \p Key.
  ValueT &operator[](const KeyT &Key) { return Map[Wrap(Key)]; }

  /// Return true if \p Ptr points into this map's buckets array.
  ///
  /// That is, either to a key or value in the ValueMap.
  /// \param Ptr Pointer that may point into the buckets array.
  /// \return True if \p Ptr points into the buckets array.
  bool isPointerIntoBucketsArray(const void *Ptr) const {
    return Map.isPointerIntoBucketsArray(Ptr);
  }

  /// Return an opaque pointer into the buckets array.
  ///
  /// In conjunction with the previous method, this can be used to determine
  /// whether an insertion caused the ValueMap to reallocate.
  /// \return An opaque pointer into the buckets array.
  const void *getPointerIntoBucketsArray() const {
    return Map.getPointerIntoBucketsArray();
  }

private:
  // Takes a key being looked up in the map and wraps it into a
  // ValueMapCallbackVH, the actual key type of the map.  We use a helper
  // function because ValueMapCVH is constructed with a second parameter.
  ValueMapCVH Wrap(KeyT key) const {
    // The only way the resulting CallbackVH could try to modify *this (making
    // the const_cast incorrect) is if it gets inserted into the map.  But then
    // this function must have been called from a non-const method, making the
    // const_cast ok.
    return ValueMapCVH(key, const_cast<ValueMap *>(this));
  }
};

/// CallbackVH that updates its ValueMap when the contained Value changes.
///
/// Behavior follows the user's preferences expressed through the Config object.
template <typename KeyT, typename ValueT, typename Config>
class ValueMapCallbackVH final : public CallbackVH {
  friend class ValueMap<KeyT, ValueT, Config>;
  friend struct DenseMapInfo<ValueMapCallbackVH>;

  using ValueMapT = ValueMap<KeyT, ValueT, Config>;
  using KeySansPointerT = std::remove_pointer_t<KeyT>;

  ValueMapT *Map;

  ValueMapCallbackVH(KeyT Key, ValueMapT *Map)
      : CallbackVH(const_cast<Value *>(static_cast<const Value *>(Key))),
        Map(Map) {}

  // Private constructor used to create empty DenseMap keys.
  ValueMapCallbackVH(Value *V) : CallbackVH(V), Map(nullptr) {}

public:
  /// Return the key as type \c KeyT.
  /// \return The unwrapped key.
  KeyT Unwrap() const { return cast_or_null<KeySansPointerT>(getValPtr()); }

  /// Notify the Config and erase this key when the Value is deleted.
  void deleted() override {
    // Make a copy that won't get changed even when *this is destroyed.
    ValueMapCallbackVH Copy(*this);
    typename Config::mutex_type *M = Config::getMutex(Copy.Map->Data);
    std::unique_lock<typename Config::mutex_type> Guard;
    if (M)
      Guard = std::unique_lock<typename Config::mutex_type>(*M);
    Config::onDelete(Copy.Map->Data, Copy.Unwrap()); // May destroy *this.
    Copy.Map->Map.erase(Copy); // Definitely destroys *this.
  }

  /// Notify the Config and optionally remap this key when RAUWd.
  /// \param new_key The Value that replaces this key.
  void allUsesReplacedWith(Value *new_key) override {
    assert(isa<KeySansPointerT>(new_key) &&
           "Invalid RAUW on key of ValueMap<>");
    // Make a copy that won't get changed even when *this is destroyed.
    ValueMapCallbackVH Copy(*this);
    typename Config::mutex_type *M = Config::getMutex(Copy.Map->Data);
    std::unique_lock<typename Config::mutex_type> Guard;
    if (M)
      Guard = std::unique_lock<typename Config::mutex_type>(*M);

    KeyT typed_new_key = cast<KeySansPointerT>(new_key);
    // Can destroy *this:
    Config::onRAUW(Copy.Map->Data, Copy.Unwrap(), typed_new_key);
    if (Config::FollowRAUW) {
      typename ValueMapT::MapT::iterator I = Copy.Map->Map.find(Copy);
      // I could == Copy.Map->Map.end() if the onRAUW callback already
      // removed the old mapping.
      if (I != Copy.Map->Map.end()) {
        ValueT Target(std::move(I->second));
        Copy.Map->Map.erase(I); // Definitely destroys *this.
        Copy.Map->insert(std::make_pair(typed_new_key, std::move(Target)));
      }
    }
  }
};

/// DenseMapInfo for ValueMapCallbackVH, hashing and comparing by unwrapped key.
template <typename KeyT, typename ValueT, typename Config>
struct DenseMapInfo<ValueMapCallbackVH<KeyT, ValueT, Config>> {
  /// ValueMapCallbackVH specialization used as the DenseMap key type.
  using VH = ValueMapCallbackVH<KeyT, ValueT, Config>;

  /// Return the hash of the unwrapped key in \p Val.
  /// \param Val CallbackVH whose key is hashed.
  /// \return The hash of the unwrapped key.
  static unsigned getHashValue(const VH &Val) {
    return DenseMapInfo<KeyT>::getHashValue(Val.Unwrap());
  }

  /// Return the hash of key \p Val.
  /// \param Val Key to hash.
  /// \return The hash of \p Val.
  static unsigned getHashValue(const KeyT &Val) {
    return DenseMapInfo<KeyT>::getHashValue(Val);
  }

  /// Return true if \p LHS and \p RHS are the same CallbackVH.
  /// \param LHS Left-hand CallbackVH.
  /// \param RHS Right-hand CallbackVH.
  /// \return True if \p LHS and \p RHS are equal.
  static bool isEqual(const VH &LHS, const VH &RHS) { return LHS == RHS; }

  /// Return true if \p LHS equals the Value pointer held by \p RHS.
  /// \param LHS Key to compare.
  /// \param RHS CallbackVH whose Value pointer is compared.
  /// \return True if \p LHS equals the Value pointer held by \p RHS.
  static bool isEqual(const KeyT &LHS, const VH &RHS) {
    return LHS == RHS.getValPtr();
  }
};

/// Forward iterator that unwraps ValueMapCallbackVH keys to \c KeyT.
template <typename DenseMapT, typename KeyT, bool IsConst>
class ValueMapIteratorImpl {
  using BaseT = std::conditional_t<IsConst, typename DenseMapT::const_iterator,
                                   typename DenseMapT::iterator>;
  using ValueT = typename DenseMapT::mapped_type;

  BaseT I;

public:
  /// Category tag identifying this as a forward iterator.
  using iterator_category = std::forward_iterator_tag;
  /// Value type referred to by the iterator (unwrapped key and mapped value).
  using value_type = std::pair<KeyT, typename DenseMapT::mapped_type>;
  /// Signed distance type between iterators.
  using difference_type = std::ptrdiff_t;
  /// Pointer to a value_type (not used for actual storage).
  using pointer = value_type *;
  /// Reference to a value_type (not used for actual storage).
  using reference = value_type &;

  /// Construct a singular (unusable) iterator.
  ValueMapIteratorImpl() = default;
  /// Construct an iterator wrapping DenseMap iterator \p I.
  /// \param I Underlying DenseMap iterator.
  ValueMapIteratorImpl(BaseT I) : I(I) {}

  // Allow conversion from iterator to const_iterator.
  /// Convert a mutable iterator into a const iterator.
  /// \param Other Non-const iterator to convert.
  template <bool C = IsConst, typename = std::enable_if_t<C>>
  ValueMapIteratorImpl(
      const ValueMapIteratorImpl<DenseMapT, KeyT, false> &Other)
      : I(Other.base()) {}

  /// Return the underlying DenseMap iterator.
  /// \return The wrapped DenseMap iterator.
  BaseT base() const { return I; }

  /// Proxy that presents an unwrapped key and a reference to the mapped value.
  struct ValueTypeProxy {
    /// Unwrapped map key.
    const KeyT first;
    /// Reference to the mapped value.
    std::conditional_t<IsConst, const ValueT &, ValueT &> second;

    /// Return a pointer to this proxy for `->` access to first/second.
    /// \return A pointer to this proxy.
    ValueTypeProxy *operator->() { return this; }

    /// Convert this proxy to a key/value pair by value.
    /// \return A key/value pair copied from this proxy.
    operator std::pair<KeyT, ValueT>() const {
      return std::make_pair(first, second);
    }
  };

  /// Return a proxy to the current key/value entry.
  /// \return A ValueTypeProxy for the current entry.
  ValueTypeProxy operator*() const {
    ValueTypeProxy Result = {I->first.Unwrap(), I->second};
    return Result;
  }

  /// Return a proxy used for `->` access to the current entry.
  /// \return A ValueTypeProxy for the current entry.
  ValueTypeProxy operator->() const { return operator*(); }

  /// Return true if both iterators refer to the same entry.
  /// \param RHS Iterator to compare against.
  /// \return True if both iterators refer to the same entry.
  bool operator==(const ValueMapIteratorImpl &RHS) const { return I == RHS.I; }
  /// Return true if the iterators refer to different entries.
  /// \param RHS Iterator to compare against.
  /// \return True if the iterators refer to different entries.
  bool operator!=(const ValueMapIteratorImpl &RHS) const { return I != RHS.I; }

  /// Advance to the next entry and return this iterator.
  /// \return A reference to this iterator after advancing.
  inline ValueMapIteratorImpl &operator++() { // Preincrement
    ++I;
    return *this;
  }
  /// Advance to the next entry and return the previous iterator value.
  /// \param Unused Distinguishes postincrement from preincrement.
  /// \return A copy of the iterator before advancing.
  ValueMapIteratorImpl operator++(int Unused) { // Postincrement
    ValueMapIteratorImpl tmp = *this;
    ++*this;
    return tmp;
  }
};

/// Mutable ValueMap iterator alias.
template <typename DenseMapT, typename KeyT>
using ValueMapIterator = ValueMapIteratorImpl<DenseMapT, KeyT, false>;

/// Const ValueMap iterator alias.
template <typename DenseMapT, typename KeyT>
using ValueMapConstIterator = ValueMapIteratorImpl<DenseMapT, KeyT, true>;

} // end namespace llvm

#endif // LLVM_IR_VALUEMAP_H
