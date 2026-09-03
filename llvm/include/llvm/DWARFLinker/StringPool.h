//===- StringPool.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_STRINGPOOL_H
#define LLVM_DWARFLINKER_STRINGPOOL_H

#include "llvm/ADT/ConcurrentHashtable.h"
#include "llvm/CodeGen/DwarfStringPoolEntry.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/PerThreadBumpPtrAllocator.h"
#include <string_view>

namespace llvm {
namespace dwarf_linker {

/// StringEntry keeps data of the string: the length, external offset
/// and a string body which is placed right after StringEntry.
using StringEntry = StringMapEntry<EmptyStringSetTag>;

/// Hash/equality/create policy for DWARF linker string pool entries.
class StringPoolEntryInfo {
public:
  /// Compute the hash of \p Key.
  ///
  /// \param Key Value whose hash code is computed.
  /// \returns Hash value for the specified \p Key.
  static inline uint64_t getHashValue(const StringRef &Key) {
    return xxh3_64bits(Key);
  }

  /// Return whether \p LHS and \p RHS compare equal.
  ///
  /// \param LHS Left-hand key to compare.
  /// \param RHS Right-hand key to compare.
  /// \returns true if both \p LHS and \p RHS are equal.
  static inline bool isEqual(const StringRef &LHS, const StringRef &RHS) {
    return LHS == RHS;
  }

  /// Extract the lookup key from stored value \p KeyData.
  ///
  /// \param KeyData Stored table value from which the key is taken.
  /// \returns key for the specified \p KeyData.
  static inline StringRef getKey(const StringEntry &KeyData) {
    return KeyData.getKey();
  }

  /// Allocate a new \c StringEntry for \p Key using \p Allocator.
  ///
  /// \param Key Key used to construct the new value.
  /// \param Allocator Allocator that owns the new \c StringEntry.
  /// \returns newly created object of KeyDataTy type.
  static inline StringEntry *
  create(const StringRef &Key,
         llvm::parallel::PerThreadBumpPtrAllocator &Allocator) {
    return StringEntry::create(Key, Allocator);
  }
};

/// Concurrent string pool used by the DWARF linker.
class StringPool
    : public ConcurrentHashTableByPtr<StringRef, StringEntry,
                                      llvm::parallel::PerThreadBumpPtrAllocator,
                                      StringPoolEntryInfo> {
public:
  /// Construct a string pool with the default table size.
  StringPool()
      : ConcurrentHashTableByPtr<StringRef, StringEntry,
                                 llvm::parallel::PerThreadBumpPtrAllocator,
                                 StringPoolEntryInfo>(Allocator) {}

  /// Construct a string pool sized for about \p InitialSize entries.
  ///
  /// \param InitialSize Approximate number of entries expected in the pool.
  StringPool(size_t InitialSize)
      : ConcurrentHashTableByPtr<StringRef, StringEntry,
                                 llvm::parallel::PerThreadBumpPtrAllocator,
                                 StringPoolEntryInfo>(Allocator, InitialSize) {}

  /// Return a reference to the pool's per-thread bump allocator.
  ///
  /// \returns Reference to the pool's per-thread bump allocator.
  llvm::parallel::PerThreadBumpPtrAllocator &getAllocatorRef() {
    return Allocator;
  }

  /// Reset the pool allocator, releasing all allocated entries.
  void clear() { Allocator.Reset(); }

private:
  llvm::parallel::PerThreadBumpPtrAllocator Allocator;
};

} // namespace dwarf_linker
} // end namespace llvm

#endif // LLVM_DWARFLINKER_STRINGPOOL_H
