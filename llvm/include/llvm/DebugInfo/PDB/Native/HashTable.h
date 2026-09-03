//===- HashTable.h - PDB Hash Table -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_HASHTABLE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_HASHTABLE_H

#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace llvm {

namespace pdb {

/// Read a SparseBitVector from a PDB binary stream.
///
/// \param Stream The reader positioned at the serialized bit vector.
/// \param V On success, receives the deserialized bit vector.
///
/// \returns Success, or an error if the stream is truncated or corrupt.
LLVM_ABI Error readSparseBitVector(BinaryStreamReader &Stream,
                                   SparseBitVector<> &V);

/// Write a SparseBitVector to a PDB binary stream.
///
/// \param Writer The writer that receives the serialized bit vector.
/// \param Vec The bit vector to serialize.
///
/// \returns Success, or an error if the write fails.
LLVM_ABI Error writeSparseBitVector(BinaryStreamWriter &Writer,
                                    SparseBitVector<> &Vec);

template <typename ValueT> class HashTable;

/// Forward iterator over the present entries of a PDB \c HashTable.
template <typename ValueT>
class HashTableIterator
    : public iterator_facade_base<HashTableIterator<ValueT>,
                                  std::forward_iterator_tag,
                                  const std::pair<uint32_t, ValueT>> {
  using BaseT = typename HashTableIterator::iterator_facade_base;
  friend HashTable<ValueT>;

  HashTableIterator(const HashTable<ValueT> &Map, uint32_t Index,
                    bool IsEnd)
      : Map(&Map), Index(Index), IsEnd(IsEnd) {}

public:
  /// Construct an iterator at the first present entry of \p Map.
  ///
  /// \param Map The hash table to iterate.
  HashTableIterator(const HashTable<ValueT> &Map) : Map(&Map) {
    int I = Map.Present.find_first();
    if (I == -1) {
      Index = 0;
      IsEnd = true;
    } else {
      Index = static_cast<uint32_t>(I);
      IsEnd = false;
    }
  }

  /// Copy-construct an iterator.
  ///
  /// \param R The iterator to copy.
  HashTableIterator(const HashTableIterator &R) = default;

  /// Copy-assign an iterator.
  ///
  /// \param R The iterator to copy from.
  ///
  /// \returns A reference to this iterator.
  HashTableIterator &operator=(const HashTableIterator &R) {
    Map = R.Map;
    return *this;
  }

  /// Compare two iterators for equality.
  ///
  /// \param R The iterator to compare against.
  ///
  /// \returns True if both are end, or both refer to the same table index.
  bool operator==(const HashTableIterator &R) const {
    if (IsEnd && R.IsEnd)
      return true;
    if (IsEnd != R.IsEnd)
      return false;

    return (Map == R.Map) && (Index == R.Index);
  }

  /// Dereference the iterator to the current key/value pair.
  ///
  /// \returns A const reference to the \c (key, value) pair at this position.
  const std::pair<uint32_t, ValueT> &operator*() const {
    assert(Map->Present.test(Index));
    return Map->Buckets[Index];
  }

  /// Bring the postfix increment operator into scope from the iterator facade.
  ///
  /// Implement postfix op++ in terms of prefix op++ by using the superclass
  /// implementation.
  using BaseT::operator++;

  /// Advance to the next present entry in the table.
  ///
  /// \returns A reference to this iterator after advancing.
  HashTableIterator &operator++() {
    while (Index < Map->Buckets.size()) {
      ++Index;
      if (Map->Present.test(Index))
        return *this;
    }

    IsEnd = true;
    return *this;
  }

private:
  bool isEnd() const { return IsEnd; }
  uint32_t index() const { return Index; }

  const HashTable<ValueT> *Map;
  uint32_t Index;
  bool IsEnd;
};

/// Closed hash table used by the native PDB format to map storage keys to
/// values.
template <typename ValueT>
class HashTable {
  struct Header {
    support::ulittle32_t Size;
    support::ulittle32_t Capacity;
  };

  using BucketList = std::vector<std::pair<uint32_t, ValueT>>;

public:
  /// Const forward iterator over present hash-table entries.
  using const_iterator = HashTableIterator<ValueT>;
  friend const_iterator;

  /// Construct an empty hash table with a small default capacity.
  HashTable() { Buckets.resize(8); }

  /// Construct an empty hash table with the given capacity.
  ///
  /// \param Capacity The number of bucket slots to allocate.
  explicit HashTable(uint32_t Capacity) {
    Buckets.resize(Capacity);
  }

  /// Load a hash table from a binary stream.
  ///
  /// \param Stream The reader positioned at a serialized hash table.
  ///
  /// \returns Success, or an error if the stream is truncated or corrupt.
  Error load(BinaryStreamReader &Stream) {
    const Header *H;
    if (auto EC = Stream.readObject(H))
      return EC;
    if (H->Capacity == 0)
      return make_error<RawError>(raw_error_code::corrupt_file,
                                  "Invalid Hash Table Capacity");
    if (H->Size > maxLoad(H->Capacity))
      return make_error<RawError>(raw_error_code::corrupt_file,
                                  "Invalid Hash Table Size");

    Buckets.resize(H->Capacity);

    if (auto EC = readSparseBitVector(Stream, Present))
      return EC;
    if (Present.count() != H->Size)
      return make_error<RawError>(raw_error_code::corrupt_file,
                                  "Present bit vector does not match size!");

    if (auto EC = readSparseBitVector(Stream, Deleted))
      return EC;
    if (Present.intersects(Deleted))
      return make_error<RawError>(raw_error_code::corrupt_file,
                                  "Present bit vector intersects deleted!");

    for (uint32_t P : Present) {
      if (auto EC = Stream.readInteger(Buckets[P].first))
        return EC;
      const ValueT *Value;
      if (auto EC = Stream.readObject(Value))
        return EC;
      Buckets[P].second = *Value;
    }

    return Error::success();
  }

  /// Return the number of bytes required to serialize this table.
  ///
  /// \returns The serialized size in bytes, including header and bit vectors.
  uint32_t calculateSerializedLength() const {
    uint32_t Size = sizeof(Header);

    constexpr int BitsPerWord = 8 * sizeof(uint32_t);

    int NumBitsP = Present.find_last() + 1;
    int NumBitsD = Deleted.find_last() + 1;

    uint32_t NumWordsP = alignTo(NumBitsP, BitsPerWord) / BitsPerWord;
    uint32_t NumWordsD = alignTo(NumBitsD, BitsPerWord) / BitsPerWord;

    // Present bit set number of words (4 bytes), followed by that many actual
    // words (4 bytes each).
    Size += sizeof(uint32_t);
    Size += NumWordsP * sizeof(uint32_t);

    // Deleted bit set number of words (4 bytes), followed by that many actual
    // words (4 bytes each).
    Size += sizeof(uint32_t);
    Size += NumWordsD * sizeof(uint32_t);

    // One (Key, ValueT) pair for each entry Present.
    Size += (sizeof(uint32_t) + sizeof(ValueT)) * size();

    return Size;
  }

  /// Serialize this hash table to a binary stream.
  ///
  /// \param Writer The writer that receives the serialized table.
  ///
  /// \returns Success, or an error if the write fails.
  Error commit(BinaryStreamWriter &Writer) const {
    Header H;
    H.Size = size();
    H.Capacity = capacity();
    if (auto EC = Writer.writeObject(H))
      return EC;

    if (auto EC = writeSparseBitVector(Writer, Present))
      return EC;

    if (auto EC = writeSparseBitVector(Writer, Deleted))
      return EC;

    for (const auto &Entry : *this) {
      if (auto EC = Writer.writeInteger(Entry.first))
        return EC;
      if (auto EC = Writer.writeObject(Entry.second))
        return EC;
    }
    return Error::success();
  }

  /// Remove all entries and reset to the default capacity.
  void clear() {
    Buckets.resize(8);
    Present.clear();
    Deleted.clear();
  }

  /// Return true if the table contains no present entries.
  ///
  /// \returns True when \c size() is zero.
  bool empty() const { return size() == 0; }

  /// Return the number of bucket slots in the table.
  ///
  /// \returns The current capacity.
  uint32_t capacity() const { return Buckets.size(); }

  /// Return the number of present entries in the table.
  ///
  /// \returns The count of set bits in the present bit vector.
  uint32_t size() const { return Present.count(); }

  /// Return an iterator to the first present entry.
  ///
  /// \returns A const iterator at the beginning of the table.
  const_iterator begin() const { return const_iterator(*this); }

  /// Return a past-the-end iterator.
  ///
  /// \returns A const iterator that compares equal to end.
  const_iterator end() const { return const_iterator(*this, 0, true); }

  /// Find the entry whose key has the specified hash value, using the specified
  /// traits defining hash function and equality.
  ///
  /// \param K The lookup key to search for.
  /// \param Traits Traits that hash and compare keys against storage keys.
  ///
  /// \returns An iterator to the matching entry, or a probe-hint end iterator
  ///     when no match is found.
  template <typename Key, typename TraitsT>
  const_iterator find_as(const Key &K, TraitsT &Traits) const {
    uint32_t H = Traits.hashLookupKey(K) % capacity();
    uint32_t I = H;
    std::optional<uint32_t> FirstUnused;
    do {
      if (isPresent(I)) {
        if (Traits.storageKeyToLookupKey(Buckets[I].first) == K)
          return const_iterator(*this, I, false);
      } else {
        if (!FirstUnused)
          FirstUnused = I;
        // Insertion occurs via linear probing from the slot hint, and will be
        // inserted at the first empty / deleted location.  Therefore, if we are
        // probing and find a location that is neither present nor deleted, then
        // nothing must have EVER been inserted at this location, and thus it is
        // not possible for a matching value to occur later.
        if (!isDeleted(I))
          break;
      }
      I = (I + 1) % capacity();
    } while (I != H);

    // The only way FirstUnused would not be set is if every single entry in the
    // table were Present.  But this would violate the load factor constraints
    // that we impose, so it should never happen.
    assert(FirstUnused);
    return const_iterator(*this, *FirstUnused, true);
  }

  /// Set the entry using a key type that the specified Traits can convert
  /// from a real key to an internal key.
  ///
  /// \param K The lookup key identifying the entry.
  /// \param V The value to store.
  /// \param Traits Traits that convert between lookup and storage keys.
  ///
  /// \returns True if a new entry was inserted; false if an existing entry was
  ///     updated.
  template <typename Key, typename TraitsT>
  bool set_as(const Key &K, ValueT V, TraitsT &Traits) {
    return set_as_internal(K, std::move(V), Traits, std::nullopt);
  }

  /// Return the value associated with the given lookup key.
  ///
  /// \param K The lookup key to search for.
  /// \param Traits Traits that hash and compare keys against storage keys.
  ///
  /// \returns The value stored for \p K. The key must be present.
  template <typename Key, typename TraitsT>
  ValueT get(const Key &K, TraitsT &Traits) const {
    auto Iter = find_as(K, Traits);
    assert(Iter != end());
    return (*Iter).second;
  }

protected:
  /// Return whether the bucket at index \p K holds a present entry.
  ///
  /// \param K The bucket index to test.
  ///
  /// \returns True if the present bit for \p K is set.
  bool isPresent(uint32_t K) const { return Present.test(K); }

  /// Return whether the bucket at index \p K is marked deleted.
  ///
  /// \param K The bucket index to test.
  ///
  /// \returns True if the deleted bit for \p K is set.
  bool isDeleted(uint32_t K) const { return Deleted.test(K); }

  /// Bucket storage of \c (storage key, value) pairs.
  BucketList Buckets;

  /// Bit vector of buckets that currently hold a present entry.
  mutable SparseBitVector<> Present;

  /// Bit vector of buckets that are marked as deleted.
  mutable SparseBitVector<> Deleted;

private:
  /// Set the entry using a key type that the specified Traits can convert
  /// from a real key to an internal key.
  template <typename Key, typename TraitsT>
  bool set_as_internal(const Key &K, ValueT V, TraitsT &Traits,
                       std::optional<uint32_t> InternalKey) {
    auto Entry = find_as(K, Traits);
    if (Entry != end()) {
      assert(isPresent(Entry.index()));
      assert(Traits.storageKeyToLookupKey(Buckets[Entry.index()].first) == K);
      // We're updating, no need to do anything special.
      Buckets[Entry.index()].second = V;
      return false;
    }

    auto &B = Buckets[Entry.index()];
    assert(!isPresent(Entry.index()));
    assert(Entry.isEnd());
    B.first = InternalKey ? *InternalKey : Traits.lookupKeyToStorageKey(K);
    B.second = V;
    Present.set(Entry.index());
    Deleted.reset(Entry.index());

    grow(Traits);

    assert((find_as(K, Traits)) != end());
    return true;
  }

  static uint32_t maxLoad(uint32_t capacity) { return capacity * 2 / 3 + 1; }

  template <typename TraitsT>
  void grow(TraitsT &Traits) {
    uint32_t S = size();
    uint32_t MaxLoad = maxLoad(capacity());
    if (S < maxLoad(capacity()))
      return;
    assert(capacity() != UINT32_MAX && "Can't grow Hash table!");

    uint32_t NewCapacity = (capacity() <= INT32_MAX) ? MaxLoad * 2 : UINT32_MAX;

    // Growing requires rebuilding the table and re-hashing every item.  Make a
    // copy with a larger capacity, insert everything into the copy, then swap
    // it in.
    HashTable NewMap(NewCapacity);
    for (auto I : Present) {
      auto LookupKey = Traits.storageKeyToLookupKey(Buckets[I].first);
      NewMap.set_as_internal(LookupKey, Buckets[I].second, Traits,
                             Buckets[I].first);
    }

    Buckets.swap(NewMap.Buckets);
    std::swap(Present, NewMap.Present);
    std::swap(Deleted, NewMap.Deleted);
    assert(capacity() == NewCapacity);
    assert(size() == S);
  }
};

} // end namespace pdb

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_HASHTABLE_H
