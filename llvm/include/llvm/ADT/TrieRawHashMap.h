//===- TrieRawHashMap.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_TRIERAWHASHMAP_H
#define LLVM_ADT_TRIERAWHASHMAP_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Compiler.h"
#include <atomic>
#include <optional>

namespace llvm {

class raw_ostream;

/// Helper used by unit tests to inspect ThreadSafeTrieRawHashMapBase internals.
class TrieRawHashMapTestHelper;

/// TrieRawHashMap - is a lock-free thread-safe trie that is can be used to
/// store/index data based on a hash value. It can be customized to work with
/// any hash algorithm or store any data.
///
/// Data structure:
/// Data node stored in the Trie contains both hash and data:
/// struct {
///    HashT Hash;
///    DataT Data;
/// };
///
/// Data is stored/indexed via a prefix tree, where each node in the tree can be
/// either the root, a sub-trie or a data node. Assuming a 4-bit hash and two
/// data objects {0001, A} and {0100, B}, it can be stored in a trie
/// (assuming Root has 2 bits, SubTrie has 1 bit):
///  +--------+
///  |Root[00]| -> {0001, A}
///  |    [01]| -> {0100, B}
///  |    [10]| (empty)
///  |    [11]| (empty)
///  +--------+
///
/// Inserting a new object {0010, C} will result in:
///  +--------+    +----------+
///  |Root[00]| -> |SubTrie[0]| -> {0001, A}
///  |        |    |       [1]| -> {0010, C}
///  |        |    +----------+
///  |    [01]| -> {0100, B}
///  |    [10]| (empty)
///  |    [11]| (empty)
///  +--------+
/// Note object A is sunk down to a sub-trie during the insertion. All the
/// nodes are inserted through compare-exchange to ensure thread-safe and
/// lock-free.
///
/// To find an object in the trie, walk the tree with prefix of the hash until
/// the data node is found. Then the hash is compared with the hash stored in
/// the data node to see if the is the same object.
///
/// Hash collision is not allowed so it is recommended to use trie with a
/// "strong" hashing algorithm. A well-distributed hash can also result in
/// better performance and memory usage.
///
/// It currently does not support iteration and deletion.

/// Base class for a lock-free thread-safe hash-mapped trie.
class ThreadSafeTrieRawHashMapBase {
public:
  /// Size of the fixed header preceding each content allocation.
  static constexpr size_t TrieContentBaseSize = 4;
  /// Default number of hash bits consumed by the root trie node.
  static constexpr size_t DefaultNumRootBits = 6;
  /// Default number of hash bits consumed by each subtrie node.
  static constexpr size_t DefaultNumSubtrieBits = 4;

private:
  template <class T> struct AllocValueType {
    char Base[TrieContentBaseSize];
    alignas(T) char Content[sizeof(T)];
  };

protected:
  /// Default allocation size for content of type \p T.
  template <class T>
  static constexpr size_t DefaultContentAllocSize = sizeof(AllocValueType<T>);

  /// Default allocation alignment for content of type \p T.
  template <class T>
  static constexpr size_t DefaultContentAllocAlign = alignof(AllocValueType<T>);

  /// Byte offset of content of type \p T within its allocation.
  template <class T>
  static constexpr size_t DefaultContentOffset =
      offsetof(AllocValueType<T>, Content);

public:
  /// Allocate a ThreadSafeTrieRawHashMapBase with the global operator new.
  /// @param Size Number of bytes to allocate.
  /// @return Pointer to the newly allocated memory.
  static void *operator new(size_t Size) { return ::operator new(Size); }
  /// Deallocate memory previously obtained from operator new.
  /// @param Ptr Pointer returned by operator new.
  void operator delete(void *Ptr) { ::operator delete(Ptr); }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump a debug representation of the trie to stderr.
  LLVM_DUMP_METHOD void dump() const;
#endif

  /// Print a debug representation of the trie to \p OS.
  /// @param OS Stream to write to.
  LLVM_ABI void print(raw_ostream &OS) const;

protected:
  /// Result of a lookup, suitable as an insertion hint.
  ///
  /// Maybe could be expanded into an iterator of sorts, but likely not useful
  /// (visiting everything in the trie should probably be done some way other
  /// than through an iterator pattern).
  class PointerBase {
  protected:
    /// Return the content pointer when this is a data result, else null.
    /// @return Content pointer for a data result, or null otherwise.
    void *get() const { return I == -2u ? P : nullptr; }

  public:
    /// Construct a null / empty lookup result.
    PointerBase() noexcept = default;

  private:
    friend class ThreadSafeTrieRawHashMapBase;
    explicit PointerBase(void *Content) : P(Content), I(-2u) {}
    PointerBase(void *P, unsigned I, unsigned B) : P(P), I(I), B(B) {}

    bool isHint() const { return I != -1u && I != -2u; }

    void *P = nullptr;
    unsigned I = -1u;
    unsigned B = 0;
  };

  /// Find the stored content with hash.
  /// @param Hash Hash bytes identifying the entry.
  /// @return Pointer base to the found content, or empty if absent.
  LLVM_ABI PointerBase find(ArrayRef<uint8_t> Hash) const;

  /// Insert and return the stored content.
  /// @param Hint Optional result of a prior find for the same hash.
  /// @param Hash Hash bytes identifying the entry.
  /// @param Constructor Callback that builds content into \p Mem if inserted.
  /// @return Pointer base to the existing or newly inserted content.
  LLVM_ABI PointerBase
  insert(PointerBase Hint, ArrayRef<uint8_t> Hash,
         function_ref<const uint8_t *(void *Mem, ArrayRef<uint8_t> Hash)>
             Constructor);

  /// Default construction is deleted; subclasses must provide sizing.
  ThreadSafeTrieRawHashMapBase() = delete;

  /// Construct a trie with the given content layout and optional bit widths.
  /// @param ContentAllocSize Size of each content allocation.
  /// @param ContentAllocAlign Alignment of each content allocation.
  /// @param ContentOffset Offset of the typed content within the allocation.
  /// @param NumRootBits Optional override for root hash bits.
  /// @param NumSubtrieBits Optional override for subtrie hash bits.
  LLVM_ABI ThreadSafeTrieRawHashMapBase(
      size_t ContentAllocSize, size_t ContentAllocAlign, size_t ContentOffset,
      std::optional<size_t> NumRootBits = std::nullopt,
      std::optional<size_t> NumSubtrieBits = std::nullopt);

  /// Destructor, which asserts if there's anything to do. Subclasses should
  /// call \a destroyImpl().
  ///
  /// \pre \a destroyImpl() was already called.
  LLVM_ABI ~ThreadSafeTrieRawHashMapBase();
  /// Destroy all stored values and free trie nodes.
  /// @param Destructor Optional callback invoked on each value's memory.
  LLVM_ABI void destroyImpl(function_ref<void(void *ValueMem)> Destructor);

  /// Move-construct, transferring ownership of the underlying implementation.
  /// @param RHS Trie to move from; left empty.
  LLVM_ABI ThreadSafeTrieRawHashMapBase(ThreadSafeTrieRawHashMapBase &&RHS);

  // Move assignment is not supported as it is not thread-safe.
  /// Move assignment is deleted because it is not thread-safe.
  /// @param RHS Unused; move assignment is not supported.
  ThreadSafeTrieRawHashMapBase &
  operator=(ThreadSafeTrieRawHashMapBase &&RHS) = delete;

  // No copy.
  /// Copy construction is deleted.
  /// @param RHS Unused; copy construction is not supported.
  ThreadSafeTrieRawHashMapBase(const ThreadSafeTrieRawHashMapBase &RHS) =
      delete;
  /// Copy assignment is deleted.
  /// @param RHS Unused; copy assignment is not supported.
  ThreadSafeTrieRawHashMapBase &
  operator=(const ThreadSafeTrieRawHashMapBase &RHS) = delete;

  // Debug functions. Implementation details and not guaranteed to be
  // thread-safe.
  /// Return a pointer base referring to the root trie node.
  /// @return Pointer base for the root trie node.
  LLVM_ABI PointerBase getRoot() const;
  /// Return the starting hash bit index for the trie node referred to by \p P.
  /// @param P Pointer base identifying a trie node.
  /// @return Starting hash bit index for that trie node.
  LLVM_ABI unsigned getStartBit(PointerBase P) const;
  /// Return how many hash bits the trie node referred to by \p P consumes.
  /// @param P Pointer base identifying a trie node.
  /// @return Number of hash bits consumed by that trie node.
  LLVM_ABI unsigned getNumBits(PointerBase P) const;
  /// Return how many slots are occupied in the trie node referred to by \p P.
  /// @param P Pointer base identifying a trie node.
  /// @return Number of occupied slots in that trie node.
  LLVM_ABI unsigned getNumSlotUsed(PointerBase P) const;
  /// Return the hash prefix for the trie node referred to by \p P as a string.
  /// @param P Pointer base identifying a trie node.
  /// @return String representation of that trie node's hash prefix.
  LLVM_ABI std::string getTriePrefixAsString(PointerBase P) const;
  /// Return the number of trie nodes currently allocated.
  /// @return Count of allocated trie nodes.
  LLVM_ABI unsigned getNumTries() const;
  // Visit next trie in the allocation chain.
  /// Return the next trie node after \p P in the allocation chain.
  /// @param P Pointer base identifying a trie node.
  /// @return Pointer base for the next trie node in the allocation chain.
  LLVM_ABI PointerBase getNextTrie(PointerBase P) const;

private:
  friend class TrieRawHashMapTestHelper;
  const unsigned short ContentAllocSize;
  const unsigned short ContentAllocAlign;
  const unsigned short ContentOffset;
  unsigned short NumRootBits;
  unsigned short NumSubtrieBits;
  class ImplType;
  // ImplPtr is owned by ThreadSafeTrieRawHashMapBase and needs to be freed in
  // destroyImpl.
  std::atomic<ImplType *> ImplPtr;
  ImplType &getOrCreateImpl();
  ImplType *getImpl() const;
};

/// Lock-free thread-safe hash-mapped trie.
template <class T, size_t NumHashBytes>
class ThreadSafeTrieRawHashMap : public ThreadSafeTrieRawHashMapBase {
public:
  /// Fixed-size byte array holding the hash key.
  using HashT = std::array<uint8_t, NumHashBytes>;

  class LazyValueConstructor;
  /// Hash/value pair stored at a leaf of the trie.
  struct value_type {
    /// Hash bytes that locate this entry in the trie.
    const HashT Hash;
    /// User data associated with \c Hash.
    T Data;

    /// Move-construct a value_type.
    /// @param RHS Value to move from.
    value_type(value_type &&RHS) = default;
    /// Copy-construct a value_type.
    /// @param RHS Value to copy from.
    value_type(const value_type &RHS) = default;

    /// Construct from hash bytes and a copy of \p Data.
    /// @param Hash Hash bytes of length NumHashBytes.
    /// @param Data Value to store.
    value_type(ArrayRef<uint8_t> Hash, const T &Data)
        : Hash(makeHash(Hash)), Data(Data) {}
    /// Construct from hash bytes and a moved \p Data.
    /// @param Hash Hash bytes of length NumHashBytes.
    /// @param Data Value to move into storage.
    value_type(ArrayRef<uint8_t> Hash, T &&Data)
        : Hash(makeHash(Hash)), Data(std::move(Data)) {}

  private:
    friend class LazyValueConstructor;

    struct EmplaceTag {};
    template <class... ArgsT>
    value_type(ArrayRef<uint8_t> Hash, EmplaceTag, ArgsT &&...Args)
        : Hash(makeHash(Hash)), Data(std::forward<ArgsT>(Args)...) {}

    static HashT makeHash(ArrayRef<uint8_t> HashRef) {
      HashT Hash;
      std::copy(HashRef.begin(), HashRef.end(), Hash.data());
      return Hash;
    }
  };

  /// Inherit the base class's operator delete.
  using ThreadSafeTrieRawHashMapBase::operator delete;
  /// Alias for the fixed-size hash key type.
  using HashType = HashT;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Inherit the base class's dump() debug helper.
  using ThreadSafeTrieRawHashMapBase::dump;
#endif

  /// Inherit the base class's print() debug helper.
  using ThreadSafeTrieRawHashMapBase::print;

private:
  template <class ValueT> class PointerImpl : PointerBase {
    friend class ThreadSafeTrieRawHashMap;

    ValueT *get() const {
      return reinterpret_cast<ValueT *>(PointerBase::get());
    }

  public:
    /// Dereference the pointed-to value; asserts if null.
    /// @return Reference to the pointed-to value.
    ValueT &operator*() const {
      assert(get());
      return *get();
    }
    /// Access a member of the pointed-to value; asserts if null.
    /// @return Pointer to the pointed-to value.
    ValueT *operator->() const {
      assert(get());
      return get();
    }
    /// Return true if this pointer refers to a stored value.
    /// @return True if this pointer refers to a stored value.
    explicit operator bool() const { return get(); }

    /// Construct a null content pointer.
    PointerImpl() = default;

  protected:
    /// Construct from a base-class lookup/insert result.
    /// @param Result PointerBase produced by find or insert.
    PointerImpl(PointerBase Result) : PointerBase(Result) {}
  };

public:
  class pointer;
  class const_pointer;
  /// Mutable smart pointer to a stored hash/value pair.
  class pointer : public PointerImpl<value_type> {
    friend class ThreadSafeTrieRawHashMap;
    friend class const_pointer;

  public:
    /// Construct a null pointer.
    pointer() = default;

  private:
    pointer(PointerBase Result) : pointer::PointerImpl(Result) {}
  };

  /// Const smart pointer to a stored hash/value pair.
  class const_pointer : public PointerImpl<const value_type> {
    friend class ThreadSafeTrieRawHashMap;

  public:
    /// Construct a null const pointer.
    const_pointer() = default;
    /// Convert from a mutable pointer.
    /// @param P Mutable pointer to the same entry.
    const_pointer(const pointer &P) : const_pointer::PointerImpl(P) {}

  private:
    const_pointer(PointerBase Result) : const_pointer::PointerImpl(Result) {}
  };

  /// Callback argument used by insertLazy to construct a value in-place.
  class LazyValueConstructor {
  public:
    /// Construct the stored value by moving from \p RHS.
    /// @param RHS Data to move into the new entry.
    /// @return Reference to the constructed value_type.
    value_type &operator()(T &&RHS) {
      assert(Mem && "Constructor already called, or moved away");
      return assign(::new (Mem) value_type(Hash, std::move(RHS)));
    }
    /// Construct the stored value by copying \p RHS.
    /// @param RHS Data to copy into the new entry.
    /// @return Reference to the constructed value_type.
    value_type &operator()(const T &RHS) {
      assert(Mem && "Constructor already called, or moved away");
      return assign(::new (Mem) value_type(Hash, RHS));
    }
    /// Emplace the stored value with constructor arguments \p Args.
    /// @param Args Arguments forwarded to T's constructor.
    /// @return Reference to the constructed value_type.
    template <class... ArgsT> value_type &emplace(ArgsT &&...Args) {
      assert(Mem && "Constructor already called, or moved away");
      return assign(::new (Mem)
                        value_type(Hash, typename value_type::EmplaceTag{},
                                   std::forward<ArgsT>(Args)...));
    }

    /// Move-construct; the source may no longer be used to construct.
    /// @param RHS Constructor to take ownership from.
    LazyValueConstructor(LazyValueConstructor &&RHS)
        : Mem(RHS.Mem), Result(RHS.Result), Hash(RHS.Hash) {
      RHS.Mem = nullptr; // Moved away, cannot call.
    }
    /// Assert that a value was constructed before destruction.
    ~LazyValueConstructor() { assert(!Mem && "Constructor never called!"); }

  private:
    value_type &assign(value_type *V) {
      Mem = nullptr;
      Result = V;
      return *V;
    }
    friend class ThreadSafeTrieRawHashMap;
    LazyValueConstructor() = delete;
    LazyValueConstructor(void *Mem, value_type *&Result, ArrayRef<uint8_t> Hash)
        : Mem(Mem), Result(Result), Hash(Hash) {
      assert(Hash.size() == sizeof(HashT) && "Invalid hash");
      assert(Mem && "Invalid memory for construction");
    }
    void *Mem;
    value_type *&Result;
    ArrayRef<uint8_t> Hash;
  };

  /// Insert lazily with a hint, constructing via \p OnConstruct if needed.
  ///
  /// Default-constructed hint will work, but it's recommended to start with a
  /// lookup to avoid overhead in object creation if it already exists.
  /// @param Hint Optional result of a prior find for the same hash.
  /// @param Hash Hash bytes identifying the entry.
  /// @param OnConstruct Callback that builds the value if insertion occurs.
  /// @return Pointer to the existing or newly inserted entry.
  pointer insertLazy(const_pointer Hint, ArrayRef<uint8_t> Hash,
                     function_ref<void(LazyValueConstructor)> OnConstruct) {
    return pointer(ThreadSafeTrieRawHashMapBase::insert(
        Hint, Hash, [&](void *Mem, ArrayRef<uint8_t> Hash) {
          value_type *Result = nullptr;
          OnConstruct(LazyValueConstructor(Mem, Result, Hash));
          return Result->Hash.data();
        }));
  }

  /// Insert lazily without a hint, constructing via \p OnConstruct if needed.
  /// @param Hash Hash bytes identifying the entry.
  /// @param OnConstruct Callback that builds the value if insertion occurs.
  /// @return Pointer to the existing or newly inserted entry.
  pointer insertLazy(ArrayRef<uint8_t> Hash,
                     function_ref<void(LazyValueConstructor)> OnConstruct) {
    return insertLazy(const_pointer(), Hash, OnConstruct);
  }

  /// Insert a moved hash/value pair, using \p Hint to speed the search.
  /// @param Hint Optional result of a prior find for the same hash.
  /// @param HashedData Hash and data to insert.
  /// @return Pointer to the existing or newly inserted entry.
  pointer insert(const_pointer Hint, value_type &&HashedData) {
    return insertLazy(Hint, HashedData.Hash, [&](LazyValueConstructor C) {
      C(std::move(HashedData.Data));
    });
  }

  /// Insert a copied hash/value pair, using \p Hint to speed the search.
  /// @param Hint Optional result of a prior find for the same hash.
  /// @param HashedData Hash and data to insert.
  /// @return Pointer to the existing or newly inserted entry.
  pointer insert(const_pointer Hint, const value_type &HashedData) {
    return insertLazy(Hint, HashedData.Hash,
                      [&](LazyValueConstructor C) { C(HashedData.Data); });
  }

  /// Find the entry whose hash equals \p Hash.
  /// @param Hash Hash bytes of length NumHashBytes.
  /// @return Mutable pointer to the entry, or null if absent.
  pointer find(ArrayRef<uint8_t> Hash) {
    assert(Hash.size() == std::tuple_size<HashT>::value);
    return ThreadSafeTrieRawHashMapBase::find(Hash);
  }

  /// Find the entry whose hash equals \p Hash (const overload).
  /// @param Hash Hash bytes of length NumHashBytes.
  /// @return Const pointer to the entry, or null if absent.
  const_pointer find(ArrayRef<uint8_t> Hash) const {
    assert(Hash.size() == std::tuple_size<HashT>::value);
    return ThreadSafeTrieRawHashMapBase::find(Hash);
  }

  /// Construct an empty trie with optional root/subtrie bit widths.
  /// @param NumRootBits Optional override for root hash bits.
  /// @param NumSubtrieBits Optional override for subtrie hash bits.
  ThreadSafeTrieRawHashMap(std::optional<size_t> NumRootBits = std::nullopt,
                           std::optional<size_t> NumSubtrieBits = std::nullopt)
      : ThreadSafeTrieRawHashMapBase(DefaultContentAllocSize<value_type>,
                                     DefaultContentAllocAlign<value_type>,
                                     DefaultContentOffset<value_type>,
                                     NumRootBits, NumSubtrieBits) {}

  /// Destroy all stored values and free trie nodes.
  ~ThreadSafeTrieRawHashMap() {
    if constexpr (std::is_trivially_destructible<value_type>::value)
      this->destroyImpl(nullptr);
    else
      this->destroyImpl(
          [](void *P) { static_cast<value_type *>(P)->~value_type(); });
  }

  // Move constructor okay.
  /// Move-construct, transferring ownership of the underlying trie.
  /// @param RHS Trie to move from; left empty.
  ThreadSafeTrieRawHashMap(ThreadSafeTrieRawHashMap &&RHS) = default;

  // No move assignment or any copy.
  /// Move assignment is deleted because it is not thread-safe.
  /// @param RHS Unused; move assignment is not supported.
  ThreadSafeTrieRawHashMap &
  operator=(ThreadSafeTrieRawHashMap &&RHS) = delete;
  /// Copy construction is deleted.
  /// @param RHS Unused; copy construction is not supported.
  ThreadSafeTrieRawHashMap(const ThreadSafeTrieRawHashMap &RHS) = delete;
  /// Copy assignment is deleted.
  /// @param RHS Unused; copy assignment is not supported.
  ThreadSafeTrieRawHashMap &
  operator=(const ThreadSafeTrieRawHashMap &RHS) = delete;
};

} // namespace llvm

#endif // LLVM_ADT_TRIERAWHASHMAP_H
