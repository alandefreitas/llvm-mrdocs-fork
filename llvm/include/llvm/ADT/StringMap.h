//===- StringMap.h - String Hash table map interface ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the StringMap class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STRINGMAP_H
#define LLVM_ADT_STRINGMAP_H

#include "llvm/ADT/EpochTracker.h"
#include "llvm/ADT/StringMapEntry.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/AllocatorBase.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <initializer_list>
#include <iterator>
#include <type_traits>

namespace llvm {

template <typename ValueTy, bool IsConst> class StringMapIterBase;
template <typename ValueTy> class StringMapKeyIterator;

/// StringMapImpl - This is the base class of StringMap that is shared among
/// all of its instantiations.
class StringMapImpl : public DebugEpochBase {
protected:
  // Array of NumBuckets pointers to entries, null pointers are holes.
  // TheTable[NumBuckets] contains a sentinel value for easy iteration. Followed
  // by an array of the actual hash values as unsigned integers.
  /// Hash table of entry pointers; null slots are empty probe holes.
  StringMapEntryBase **TheTable = nullptr;
  /// Number of hash buckets currently allocated.
  unsigned NumBuckets = 0;
  /// Number of live key/value entries in the map.
  unsigned NumItems = 0;
  /// Size in bytes of each StringMapEntry specialization.
  unsigned ItemSize;

protected:
  /// Construct an empty map with the given per-entry size.
  explicit StringMapImpl(unsigned itemSize) : ItemSize(itemSize) {}
  /// Move-construct, leaving \p RHS empty.
  StringMapImpl(StringMapImpl &&RHS)
      : TheTable(RHS.TheTable), NumBuckets(RHS.NumBuckets),
        NumItems(RHS.NumItems), ItemSize(RHS.ItemSize) {
    RHS.TheTable = nullptr;
    RHS.NumBuckets = 0;
    RHS.NumItems = 0;
  }

  /// Construct with an initial bucket count and per-entry size.
  LLVM_ABI StringMapImpl(unsigned InitSize, unsigned ItemSize);
  /// Free the hash table storage.
  ~StringMapImpl() { free(TheTable); }
  /// Grow and rehash the table; return the new bucket for \p BucketNo.
  LLVM_ABI unsigned RehashTable(unsigned BucketNo = 0);

  /// LookupBucketFor - Look up the bucket that the specified string should end
  /// up in.  If it already exists as a key in the map, the Item pointer for the
  /// specified bucket will be non-null.  Otherwise, it will be null.  In either
  /// case, the FullHashValue field of the bucket will be set to the hash value
  /// of the string.
  unsigned LookupBucketFor(StringRef Key) {
    return LookupBucketFor(Key, hash(Key));
  }

  /// Overload that explicitly takes precomputed hash(Key).
  LLVM_ABI unsigned LookupBucketFor(StringRef Key, uint32_t FullHashValue);

  /// Return the bucket index for \p Key, or -1 if absent.
  ///
  /// Does not modify the map.
  int FindKey(StringRef Key) const { return FindKey(Key, hash(Key)); }

  /// Overload that explicitly takes precomputed hash(Key).
  LLVM_ABI int FindKey(StringRef Key, uint32_t FullHashValue) const;

  /// RemoveKey - Remove the specified StringMapEntry from the table, but do not
  /// delete it.  This aborts if the value isn't in the table.
  LLVM_ABI void RemoveKey(StringMapEntryBase *V);

  /// RemoveKey - Remove the StringMapEntry for the specified key from the
  /// table, returning it.  If the key is not in the table, this returns null.
  LLVM_ABI StringMapEntryBase *RemoveKey(StringRef Key);

  /// Remove the entry pointer at the given (live) bucket without destroying
  /// the entry, and close the hole via Algorithm R backward shifting.
  LLVM_ABI void removeBucket(unsigned Bucket);

  /// Allocate the table with the specified number of buckets and otherwise
  /// setup the map as empty.
  LLVM_ABI void init(unsigned Size);

  /// Return a range over the raw bucket pointer array.
  iterator_range<StringMapEntryBase **> buckets() {
    return make_range(TheTable, TheTable + NumBuckets);
  }

public:
  /// Return the number of allocated hash buckets.
  [[nodiscard]] unsigned getNumBuckets() const { return NumBuckets; }
  /// Return the number of live entries.
  [[nodiscard]] unsigned getNumItems() const { return NumItems; }

  /// Return true if the map contains no entries.
  [[nodiscard]] bool empty() const { return NumItems == 0; }
  /// Return the number of live entries.
  [[nodiscard]] unsigned size() const { return NumItems; }

  /// Return the hash value used for \p Key.
  ///
  /// Callers may precompute this and pass it to overloads that accept a hash.
  /// The algorithm is not guaranteed to be stable across LLVM versions.
  [[nodiscard]] LLVM_ABI static uint32_t hash(StringRef Key);

  /// Exchange the contents of this map with \p Other.
  void swap(StringMapImpl &Other) {
    incrementEpoch();
    Other.incrementEpoch();
    std::swap(TheTable, Other.TheTable);
    std::swap(NumBuckets, Other.NumBuckets);
    std::swap(NumItems, Other.NumItems);
  }
};

/// StringMap - This is an unconventional map that is specialized for handling
/// keys that are "strings", which are basically ranges of bytes. This does some
/// funky memory allocation and hashing things to make it extremely efficient,
/// storing the string data *after* the value in the map.
template <typename ValueTy, typename AllocatorTy = MallocAllocator>
class LLVM_ALLOCATORHOLDER_EMPTYBASE StringMap
    : public StringMapImpl,
      private detail::AllocatorHolder<AllocatorTy> {
  using AllocTy = detail::AllocatorHolder<AllocatorTy>;

public:
  /// Entry type storing a key and mapped value together.
  using MapEntryTy = StringMapEntry<ValueTy>;

  /// Construct an empty map with the default allocator.
  StringMap() : StringMapImpl(static_cast<unsigned>(sizeof(MapEntryTy))) {}

  /// Construct an empty map with roughly \p InitialSize buckets.
  explicit StringMap(unsigned InitialSize)
      : StringMapImpl(InitialSize, static_cast<unsigned>(sizeof(MapEntryTy))) {}

  /// Construct an empty map that uses allocator \p A.
  explicit StringMap(AllocatorTy A)
      : StringMapImpl(static_cast<unsigned>(sizeof(MapEntryTy))), AllocTy(A) {}

  /// Construct with \p InitialSize buckets and allocator \p A.
  StringMap(unsigned InitialSize, AllocatorTy A)
      : StringMapImpl(InitialSize, static_cast<unsigned>(sizeof(MapEntryTy))),
        AllocTy(A) {}

  /// Construct from an initializer list of key/value pairs.
  StringMap(std::initializer_list<std::pair<StringRef, ValueTy>> List)
      : StringMapImpl(List.size(), static_cast<unsigned>(sizeof(MapEntryTy))) {
    insert(List);
  }

  /// Move-construct, taking ownership of \p RHS's table and allocator.
  StringMap(StringMap &&RHS)
      : StringMapImpl(std::move(RHS)), AllocTy(std::move(RHS.getAllocator())) {}

  /// Deep-copy construct from \p RHS.
  StringMap(const StringMap &RHS)
      : StringMapImpl(static_cast<unsigned>(sizeof(MapEntryTy))),
        AllocTy(RHS.getAllocator()) {
    if (RHS.empty())
      return;

    // Allocate TheTable of the same size as RHS's TheTable, and set the
    // sentinel appropriately (and NumBuckets).
    init(RHS.NumBuckets);
    unsigned *HashTable = (unsigned *)(TheTable + NumBuckets + 1),
             *RHSHashTable = (unsigned *)(RHS.TheTable + NumBuckets + 1);

    NumItems = RHS.NumItems;
    // Copy the bucket layout verbatim.  Preserving each entry's slot keeps
    // the probe-sequence invariant intact without re-probing.
    for (unsigned I = 0, E = NumBuckets; I != E; ++I) {
      StringMapEntryBase *Bucket = RHS.TheTable[I];
      if (!Bucket)
        continue;

      TheTable[I] = MapEntryTy::create(
          static_cast<MapEntryTy *>(Bucket)->getKey(), getAllocator(),
          static_cast<MapEntryTy *>(Bucket)->getValue());
      HashTable[I] = RHSHashTable[I];
    }
  }

  /// Copy-assign by swapping with a copy of \p RHS.
  StringMap &operator=(StringMap RHS) {
    StringMapImpl::swap(RHS);
    std::swap(getAllocator(), RHS.getAllocator());
    return *this;
  }

  /// Destroy all entries and free the table.
  ~StringMap() {
    // Delete all the elements in the map, but don't reset the elements
    // to default values.  This is a copy of clear(), but avoids unnecessary
    // work not required in the destructor.
    if (!empty()) {
      for (StringMapEntryBase *Bucket : buckets()) {
        if (Bucket) {
          static_cast<MapEntryTy *>(Bucket)->Destroy(getAllocator());
        }
      }
    }
  }

  using AllocTy::getAllocator;

  /// Key type exposed for STL compatibility (internally a C string pointer).
  using key_type = const char *;
  /// Mapped value type.
  using mapped_type = ValueTy;
  /// Entry type combining key and value.
  using value_type = StringMapEntry<ValueTy>;
  /// Unsigned type used for sizes.
  using size_type = size_t;

  /// Const iterator over map entries.
  using const_iterator = StringMapIterBase<ValueTy, true>;
  /// Mutable iterator over map entries.
  using iterator = StringMapIterBase<ValueTy, false>;

  /// Return an iterator to the first live entry.
  [[nodiscard]] iterator begin() {
    return iterator(this, TheTable, NumBuckets != 0);
  }
  /// Return an iterator past the last entry.
  [[nodiscard]] iterator end() { return iterator(this, TheTable + NumBuckets); }
  /// Return a const iterator to the first live entry.
  [[nodiscard]] const_iterator begin() const {
    return const_iterator(this, TheTable, NumBuckets != 0);
  }
  /// Return a const iterator past the last entry.
  [[nodiscard]] const_iterator end() const {
    return const_iterator(this, TheTable + NumBuckets);
  }

  /// Return a range that yields each key as a StringRef.
  [[nodiscard]] iterator_range<StringMapKeyIterator<ValueTy>> keys() const {
    return make_range(StringMapKeyIterator<ValueTy>(begin()),
                      StringMapKeyIterator<ValueTy>(end()));
  }

  /// Find the entry for \p Key, or end() if absent.
  [[nodiscard]] iterator find(StringRef Key) { return find(Key, hash(Key)); }

  /// Find the entry for \p Key using a precomputed hash.
  [[nodiscard]] iterator find(StringRef Key, uint32_t FullHashValue) {
    int Bucket = FindKey(Key, FullHashValue);
    if (Bucket == -1)
      return end();
    return iterator(this, TheTable + Bucket);
  }

  /// Find the entry for \p Key, or end() if absent.
  [[nodiscard]] const_iterator find(StringRef Key) const {
    return find(Key, hash(Key));
  }

  /// Find the entry for \p Key using a precomputed hash.
  [[nodiscard]] const_iterator find(StringRef Key,
                                    uint32_t FullHashValue) const {
    int Bucket = FindKey(Key, FullHashValue);
    if (Bucket == -1)
      return end();
    return const_iterator(this, TheTable + Bucket);
  }

  /// lookup - Return the entry for the specified key, or a default
  /// constructed value if no such entry exists.
  [[nodiscard]] ValueTy lookup(StringRef Key) const {
    const_iterator Iter = find(Key);
    if (Iter != end())
      return Iter->second;
    return ValueTy();
  }

  /// at - Return the entry for the specified key, or abort if no such
  /// entry exists.
  [[nodiscard]] const ValueTy &at(StringRef Val) const {
    auto Iter = this->find(Val);
    assert(Iter != this->end() && "StringMap::at failed due to a missing key");
    return Iter->second;
  }

  /// Lookup the ValueTy for the \p Key, or create a default constructed value
  /// if the key is not in the map.
  ValueTy &operator[](StringRef Key) { return try_emplace(Key).first->second; }

  /// contains - Return true if the element is in the map, false otherwise.
  [[nodiscard]] bool contains(StringRef Key) const {
    return find(Key) != end();
  }

  /// count - Return 1 if the element is in the map, 0 otherwise.
  [[nodiscard]] size_type count(StringRef Key) const {
    return contains(Key) ? 1 : 0;
  }

  /// Return 1 if \p MapEntry's key is present, otherwise 0.
  template <typename InputTy>
  [[nodiscard]] size_type count(const StringMapEntry<InputTy> &MapEntry) const {
    return count(MapEntry.getKey());
  }

  /// equal - check whether both of the containers are equal.
  [[nodiscard]] bool operator==(const StringMap &RHS) const {
    if (size() != RHS.size())
      return false;

    for (const auto &KeyValue : *this) {
      auto FindInRHS = RHS.find(KeyValue.getKey());

      if (FindInRHS == RHS.end())
        return false;

      if constexpr (!std::is_same_v<ValueTy, EmptyStringSetTag>) {
        if (!(KeyValue.getValue() == FindInRHS->getValue()))
          return false;
      }
    }

    return true;
  }

  /// Return true if the maps differ in keys or values.
  [[nodiscard]] bool operator!=(const StringMap &RHS) const {
    return !(*this == RHS);
  }

  /// insert - Insert the specified key/value pair into the map.  If the key
  /// already exists in the map, return false and ignore the request, otherwise
  /// insert it and return true.
  bool insert(MapEntryTy *KeyValue) {
    unsigned BucketNo = LookupBucketFor(KeyValue->getKey());
    StringMapEntryBase *&Bucket = TheTable[BucketNo];
    if (Bucket)
      return false; // Already exists in map.

    incrementEpoch();
    Bucket = KeyValue;
    ++NumItems;
    assert(NumItems <= NumBuckets);

    RehashTable();
    return true;
  }

  /// Insert \p KV if its key is not already present.
  ///
  /// @param KV Key/value pair to insert.
  /// @return An iterator to the entry and whether insertion occurred.
  std::pair<iterator, bool> insert(std::pair<StringRef, ValueTy> KV) {
    return try_emplace_with_hash(KV.first, hash(KV.first),
                                 std::move(KV.second));
  }

  /// Insert \p KV using a precomputed hash if the key is absent.
  std::pair<iterator, bool> insert(std::pair<StringRef, ValueTy> KV,
                                   uint32_t FullHashValue) {
    return try_emplace_with_hash(KV.first, FullHashValue, std::move(KV.second));
  }

  /// Inserts elements from range [first, last). If multiple elements in the
  /// range have keys that compare equivalent, it is unspecified which element
  /// is inserted .
  template <typename InputIt> void insert(InputIt First, InputIt Last) {
    for (InputIt It = First; It != Last; ++It)
      insert(*It);
  }

  ///  Inserts elements from initializer list ilist. If multiple elements in
  /// the range have keys that compare equivalent, it is unspecified which
  /// element is inserted
  void insert(std::initializer_list<std::pair<StringRef, ValueTy>> List) {
    insert(List.begin(), List.end());
  }

  /// Inserts an element or assigns to the current element if the key already
  /// exists. The return type is the same as try_emplace.
  template <typename V>
  std::pair<iterator, bool> insert_or_assign(StringRef Key, V &&Val) {
    auto Ret = try_emplace(Key, std::forward<V>(Val));
    if (!Ret.second)
      Ret.first->second = std::forward<V>(Val);
    return Ret;
  }

  /// Emplace a value for \p Key if the key is not already present.
  ///
  /// @param Key Key to insert or look up.
  /// @param Args Constructor arguments for the mapped value.
  /// @return An iterator to the entry and whether insertion occurred.
  template <typename... ArgsTy>
  std::pair<iterator, bool> try_emplace(StringRef Key, ArgsTy &&...Args) {
    return try_emplace_with_hash(Key, hash(Key), std::forward<ArgsTy>(Args)...);
  }

  /// Emplace a value for \p Key using a precomputed hash if absent.
  template <typename... ArgsTy>
  std::pair<iterator, bool> try_emplace_with_hash(StringRef Key,
                                                  uint32_t FullHashValue,
                                                  ArgsTy &&...Args) {
    unsigned BucketNo = LookupBucketFor(Key, FullHashValue);
    StringMapEntryBase *&Bucket = TheTable[BucketNo];
    if (Bucket)
      return {iterator(this, TheTable + BucketNo), false}; // Already in map.

    incrementEpoch();
    Bucket =
        MapEntryTy::create(Key, getAllocator(), std::forward<ArgsTy>(Args)...);
    ++NumItems;
    assert(NumItems <= NumBuckets);

    BucketNo = RehashTable(BucketNo);
    return {iterator(this, TheTable + BucketNo), true};
  }

  /// Remove all entries from the map.
  void clear() {
    incrementEpoch();
    if (empty())
      return;

    // Zap all values, resetting the keys back to non-present, which is safe
    // because we're removing all elements.
    for (StringMapEntryBase *&Bucket : buckets()) {
      if (Bucket) {
        static_cast<MapEntryTy *>(Bucket)->Destroy(getAllocator());
      }
      Bucket = nullptr;
    }

    NumItems = 0;
  }

  /// remove - Remove the specified key/value pair from the map, but do not
  /// erase it.  This aborts if the key is not in the map.
  void remove(MapEntryTy *KeyValue) {
    incrementEpoch();
    RemoveKey(KeyValue);
  }

  /// Erase the entry referred to by iterator \p I.
  void erase(iterator I) {
    MapEntryTy &V = *I;
    remove(&V);
    V.Destroy(getAllocator());
  }

  /// Erase the entry with key \p Key if present.
  /// @param Key String key to remove.
  /// @return True if an entry was erased, false if \p Key was not found.
  bool erase(StringRef Key) {
    iterator I = find(Key);
    if (I == end())
      return false;
    erase(I);
    return true;
  }

  /// Remove every entry for which \p Pred returns true.
  ///
  /// \p Pred is invoked with a reference to each live entry and must not access
  /// the map being modified. This is the safe replacement for
  /// erase-while-iterating.
  ///
  /// Returns whether anything was removed. If so, all iterators and references
  /// into the map are invalidated.
  template <typename Predicate> bool remove_if(Predicate Pred) {
    bool Removed = false;
    for (unsigned I = 0; I != NumBuckets;) {
      StringMapEntryBase *Bucket = TheTable[I];
      if (!Bucket) {
        ++I;
        continue;
      }
      auto *Entry = static_cast<MapEntryTy *>(Bucket);
      if (!Pred(*Entry)) {
        ++I;
        continue;
      }
      Entry->Destroy(getAllocator());
      // This may relocate a following entry into this slot to close the hole,
      // so re-examine the same index rather than advancing past it.
      removeBucket(I);
      Removed = true;
    }
    if (Removed)
      incrementEpoch();
    return Removed;
  }
};

/// Forward iterator over StringMap entries.
template <typename ValueTy, bool IsConst>
class StringMapIterBase : DebugEpochBase::HandleBase {
  friend class StringMapIterBase<ValueTy, true>;
  friend class StringMapIterBase<ValueTy, false>;

  StringMapEntryBase **Ptr = nullptr;

public:
  /// Category tag identifying this as a forward iterator.
  using iterator_category = std::forward_iterator_tag;
  /// Entry type referred to by the iterator.
  using value_type = StringMapEntry<ValueTy>;
  /// Signed distance type between iterators.
  using difference_type = std::ptrdiff_t;
  /// Pointer to an entry, constness matching \p IsConst.
  using pointer = std::conditional_t<IsConst, const value_type *, value_type *>;
  /// Reference to an entry, constness matching \p IsConst.
  using reference =
      std::conditional_t<IsConst, const value_type &, value_type &>;

  /// Construct a singular (unusable) iterator.
  StringMapIterBase() = default;

  /// Construct an iterator at \p Bucket, optionally skipping empty slots.
  explicit StringMapIterBase(const DebugEpochBase *Epoch,
                             StringMapEntryBase **Bucket, bool Advance = false)
      : DebugEpochBase::HandleBase(Epoch), Ptr(Bucket) {
    if (Advance)
      AdvancePastEmptyBuckets();
  }

  // Converting ctor from non-const to const iterators. SFINAE'd out for const
  // sources so it doesn't shadow the implicit copy constructor.
  /// Convert a mutable iterator into a const iterator.
  template <bool IsConstSrc,
            typename = std::enable_if_t<!IsConstSrc && IsConst>>
  StringMapIterBase(const StringMapIterBase<ValueTy, IsConstSrc> &I)
      : DebugEpochBase::HandleBase(I), Ptr(I.Ptr) {}

  /// Return a reference to the current entry.
  [[nodiscard]] reference operator*() const {
    assert(isHandleInSync() && "invalid iterator access!");
    return *static_cast<value_type *>(*Ptr);
  }
  /// Return a pointer to the current entry.
  [[nodiscard]] pointer operator->() const {
    assert(isHandleInSync() && "invalid iterator access!");
    return static_cast<value_type *>(*Ptr);
  }

  /// Advance to the next live entry and return this iterator.
  StringMapIterBase &operator++() { // Preincrement
    assert(isHandleInSync() && "invalid iterator access!");
    ++Ptr;
    AdvancePastEmptyBuckets();
    return *this;
  }

  /// Advance to the next live entry and return the previous iterator value.
  StringMapIterBase operator++(int) { // Post-increment
    StringMapIterBase Tmp(*this);
    ++*this;
    return Tmp;
  }

  /// Return true if both iterators refer to the same bucket.
  friend bool operator==(const StringMapIterBase &LHS,
                         const StringMapIterBase &RHS) {
    assert((!LHS.getEpochAddress() || LHS.isHandleInSync()) &&
           "handle not in sync!");
    assert((!RHS.getEpochAddress() || RHS.isHandleInSync()) &&
           "handle not in sync!");
    return LHS.Ptr == RHS.Ptr;
  }

  /// Return true if the iterators refer to different buckets.
  friend bool operator!=(const StringMapIterBase &LHS,
                         const StringMapIterBase &RHS) {
    return !(LHS == RHS);
  }

private:
  void AdvancePastEmptyBuckets() {
    while (*Ptr == nullptr)
      ++Ptr;
  }
};

/// Adapter that yields StringRef keys from a StringMap iterator.
template <typename ValueTy>
class StringMapKeyIterator
    : public iterator_adaptor_base<StringMapKeyIterator<ValueTy>,
                                   StringMapIterBase<ValueTy, true>,
                                   std::forward_iterator_tag, StringRef> {
  using base = iterator_adaptor_base<StringMapKeyIterator<ValueTy>,
                                     StringMapIterBase<ValueTy, true>,
                                     std::forward_iterator_tag, StringRef>;

public:
  /// Construct a singular key iterator.
  StringMapKeyIterator() = default;
  /// Construct a key iterator wrapping entry iterator \p Iter.
  explicit StringMapKeyIterator(StringMapIterBase<ValueTy, true> Iter)
      : base(std::move(Iter)) {}

  /// Return the key of the current entry.
  StringRef operator*() const { return this->wrapped()->getKey(); }
};

} // end namespace llvm

#endif // LLVM_ADT_STRINGMAP_H
