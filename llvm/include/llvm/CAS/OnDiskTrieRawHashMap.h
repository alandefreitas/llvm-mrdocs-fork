//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file declares interface for OnDiskTrieRawHashMap, a thread-safe and
/// (mostly) lock-free hash map stored as trie and backed by persistent files on
/// disk.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CAS_ONDISKTRIERAWHASHMAP_H
#define LLVM_CAS_ONDISKTRIERAWHASHMAP_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CAS/FileOffset.h"
#include "llvm/Support/Error.h"
#include <optional>

namespace llvm {

class raw_ostream;

namespace cas {

namespace ondisk {
class OnDiskCASLogger;
} // namespace ondisk
/// OnDiskTrieRawHashMap is a persistent trie data structure used as hash maps.
/// The keys are fixed length, and are expected to be binary hashes with a
/// normal distribution.
///
/// - Thread-safety is achieved through the use of atomics within a shared
///   memory mapping. Atomic access does not work on networked filesystems.
/// - Filesystem locks are used, but only sparingly:
///     - during initialization, for creating / opening an existing store;
///     - for the lifetime of the instance, a shared/reader lock is held
///     - during destruction, if there are no concurrent readers, to shrink the
///       files to their minimum size.
/// - Path is used as a directory:
///     - "index" stores the root trie and subtries.
///     - "data" stores (most of) the entries, like a bump-ptr-allocator.
///     - Large entries are stored externally in a file named by the key.
/// - Code is system-dependent and binary format itself is not portable. These
///   are not artifacts that can/should be moved between different systems; they
///   are only appropriate for local storage.
class OnDiskTrieRawHashMap {
public:
  /// Dump a textual representation of the trie to the debug stream.
  LLVM_DUMP_METHOD LLVM_ABI void dump() const;
  /// Print a textual representation of the trie to \p OS.
  LLVM_ABI void
  print(raw_ostream &OS,
        function_ref<void(ArrayRef<char>)> PrintRecordData = nullptr) const;

public:
  /// Const value proxy to access the records stored in TrieRawHashMap.
  struct ConstValueProxy {
    /// Construct an empty const value proxy.
    ConstValueProxy() = default;
    /// Construct a const value proxy from \p Hash and byte array \p Data.
    ConstValueProxy(ArrayRef<uint8_t> Hash, ArrayRef<char> Data)
        : Hash(Hash), Data(Data) {}
    /// Construct a const value proxy from \p Hash and string \p Data.
    ConstValueProxy(ArrayRef<uint8_t> Hash, StringRef Data)
        : Hash(Hash), Data(Data.begin(), Data.size()) {}

    /// Hash key for the stored record.
    ArrayRef<uint8_t> Hash;
    /// Contents of the stored record.
    ArrayRef<char> Data;
  };

  /// Value proxy to access the records stored in TrieRawHashMap.
  struct ValueProxy {
    /// Convert this mutable value proxy to a const view.
    operator ConstValueProxy() const { return ConstValueProxy(Hash, Data); }

    /// Construct an empty value proxy.
    ValueProxy() = default;
    /// Construct a value proxy from \p Hash and mutable \p Data.
    ValueProxy(ArrayRef<uint8_t> Hash, MutableArrayRef<char> Data)
        : Hash(Hash), Data(Data) {}

    /// Hash key for the stored record.
    ArrayRef<uint8_t> Hash;
    /// Mutable contents of the stored record.
    MutableArrayRef<char> Data;
  };

  /// Validate the trie data structure.
  ///
  /// Callback receives the file offset to the data entry and the data stored.
  LLVM_ABI Error validate(
      function_ref<Error(FileOffset, ConstValueProxy)> RecordVerifier) const;

  /// Check the valid range of file offset for OnDiskTrieRawHashMap.
  static bool validOffset(FileOffset Offset) {
    return Offset.get() < (1LL << 48);
  }

public:
  /// Template class to implement a `pointer` type into the trie data structure.
  ///
  /// It provides pointer-like operation, e.g., dereference to get underlying
  /// data. It also reserves the top 16 bits of the pointer value, which can be
  /// used to pack additional information if needed.
  template <class ProxyT> class PointerImpl {
  public:
    /// \returns the file offset of the referenced value within the mapped file.
    FileOffset getOffset() const {
      return FileOffset(OffsetLow32 | (uint64_t)OffsetHigh16 << 32);
    }

    /// Return true if this pointer refers to a value.
    explicit operator bool() const { return IsValue; }

    /// Return a const reference to the referenced value proxy.
    const ProxyT &operator*() const {
      assert(IsValue);
      return Value;
    }
    /// Access members of the referenced value proxy.
    const ProxyT *operator->() const {
      assert(IsValue);
      return &Value;
    }

    /// Construct a null pointer that does not refer to a value.
    PointerImpl() = default;

  protected:
    /// Construct a pointer from \p Value at \p Offset.
    ///
    /// If \p IsValue is false, the pointer is null even when \p Offset is
    /// zero (a valid on-disk offset).
    PointerImpl(ProxyT Value, FileOffset Offset, bool IsValue = true)
        : Value(Value), OffsetLow32(Offset.get()),
          OffsetHigh16(Offset.get() >> 32), IsValue(IsValue) {
      if (IsValue)
        assert(validOffset(Offset));
    }

    /// Value proxy for the referenced record.
    ProxyT Value;
    /// Low 32 bits of the file offset into the trie.
    uint32_t OffsetLow32 = 0;
    /// High 16 bits of the file offset into the trie.
    uint16_t OffsetHigh16 = 0;

    /// True if this points to a value (not a null pointer). Stored separately
    /// because zero can be a valid file offset.
    bool IsValue = false;
  };

  class OnDiskPtr;
  /// Const pointer into a value stored in the on-disk trie.
  class ConstOnDiskPtr : public PointerImpl<ConstValueProxy> {
  public:
    /// Construct a null const on-disk pointer.
    ConstOnDiskPtr() = default;

  private:
    friend class OnDiskPtr;
    friend class OnDiskTrieRawHashMap;
    using ConstOnDiskPtr::PointerImpl::PointerImpl;
  };

  /// Mutable pointer into a value stored in the on-disk trie.
  class OnDiskPtr : public PointerImpl<ValueProxy> {
  public:
    /// Convert to a const on-disk pointer referring to the same value.
    operator ConstOnDiskPtr() const {
      return ConstOnDiskPtr(Value, getOffset(), IsValue);
    }

    /// Construct a null on-disk pointer.
    OnDiskPtr() = default;

  private:
    friend class OnDiskTrieRawHashMap;
    using OnDiskPtr::PointerImpl::PointerImpl;
  };

  /// Find the value from hash.
  ///
  /// \returns pointer to the value if exists, otherwise returns a non-value
  /// pointer that evaluates to `false` when convert to boolean.
  LLVM_ABI ConstOnDiskPtr find(ArrayRef<uint8_t> Hash) const;

  /// Helper function to recover a pointer into the trie from file offset.
  LLVM_ABI Expected<ConstOnDiskPtr>
  recoverFromFileOffset(FileOffset Offset) const;

  /// Callback to fill a tentatively allocated value before it is published.
  using LazyInsertOnConstructCB =
      function_ref<void(FileOffset TentativeOffset, ValueProxy TentativeValue)>;
  /// Callback invoked when a tentative insert is abandoned after a race.
  using LazyInsertOnLeakCB =
      function_ref<void(FileOffset TentativeOffset, ValueProxy TentativeValue,
                        FileOffset FinalOffset, ValueProxy FinalValue)>;

  /// Insert lazily.
  ///
  /// \p OnConstruct is called when ready to insert a value, after allocating
  /// space for the data. It is called at most once.
  ///
  /// \p OnLeak is called only if \p OnConstruct has been called and a race
  /// occurred before insertion, causing the tentative offset and data to be
  /// abandoned. This allows clients to clean up other results or update any
  /// references.
  ///
  /// NOTE: Does *not* guarantee that \p OnConstruct is only called on success.
  /// The in-memory \a TrieRawHashMap uses LazyAtomicPointer to synchronize
  /// simultaneous writes, but that seems dangerous to use in a memory-mapped
  /// file in case a process crashes in the busy state.
  LLVM_ABI Expected<OnDiskPtr>
  insertLazy(ArrayRef<uint8_t> Hash,
             LazyInsertOnConstructCB OnConstruct = nullptr,
             LazyInsertOnLeakCB OnLeak = nullptr);

  /// Insert \p Value by copying its data into a newly allocated record.
  Expected<OnDiskPtr> insert(const ConstValueProxy &Value) {
    return insertLazy(Value.Hash, [&](FileOffset, ValueProxy Allocated) {
      assert(Allocated.Hash == Value.Hash);
      assert(Allocated.Data.size() == Value.Data.size());
      llvm::copy(Value.Data, Allocated.Data.begin());
    });
  }

  /// \returns the number of records currently stored in the trie.
  LLVM_ABI size_t size() const;
  /// Return the size of the mapped file region in bytes.
  LLVM_ABI size_t capacity() const;

  /// Gets or creates a file at \p Path with a hash-mapped trie named \p
  /// TrieName. The hash size is \p NumHashBits (in bits) and the records store
  /// data of size \p DataSize (in bytes).
  ///
  /// \p MaxFileSize controls the maximum file size to support, limiting the
  /// size of the \a mapped_file_region. \p NewFileInitialSize is the starting
  /// size if a new file is created.
  ///
  /// \p NewTableNumRootBits and \p NewTableNumSubtrieBits are hints to
  /// configure the trie, if it doesn't already exist.
  ///
  /// \pre NumHashBits is a multiple of 8 (byte-aligned).
  LLVM_ABI static Expected<OnDiskTrieRawHashMap>
  create(const Twine &Path, const Twine &TrieName, size_t NumHashBits,
         uint64_t DataSize, uint64_t MaxFileSize,
         std::optional<uint64_t> NewFileInitialSize,
         std::shared_ptr<ondisk::OnDiskCASLogger> Logger = nullptr,
         std::optional<size_t> NewTableNumRootBits = std::nullopt,
         std::optional<size_t> NewTableNumSubtrieBits = std::nullopt);

  /// Move-construct a map from \p RHS.
  LLVM_ABI OnDiskTrieRawHashMap(OnDiskTrieRawHashMap &&RHS);
  /// Move-assign this map from \p RHS.
  LLVM_ABI OnDiskTrieRawHashMap &operator=(OnDiskTrieRawHashMap &&RHS);
  /// Destroy the map and release resources.
  LLVM_ABI ~OnDiskTrieRawHashMap();

private:
  struct ImplType;
  explicit OnDiskTrieRawHashMap(std::unique_ptr<ImplType> Impl);
  std::unique_ptr<ImplType> Impl;
};

} // namespace cas
} // namespace llvm

#endif // LLVM_CAS_ONDISKTRIERAWHASHMAP_H
