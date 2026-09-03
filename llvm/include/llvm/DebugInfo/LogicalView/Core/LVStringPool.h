//===-- LVStringPool.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVStringPool class, which is used to implement a
// basic string pool table.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSTRINGPOOL_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSTRINGPOOL_H

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <iomanip>
#include <vector>

namespace llvm {
namespace logicalview {

/// Interned string table that maps unique strings to stable indices.
class LVStringPool {
  static constexpr size_t BadIndex = std::numeric_limits<size_t>::max();
  using TableType = StringMap<size_t, BumpPtrAllocator>;
  using ValueType = TableType::value_type;
  BumpPtrAllocator Allocator;
  TableType StringTable;
  std::vector<ValueType *> Entries;

public:
  /// Construct an empty string pool with the empty string at index zero.
  LVStringPool() { getIndex(""); }
  /// Copy construction is not allowed.
  LVStringPool(LVStringPool const &other) = delete;
  /// Move construction is not allowed.
  LVStringPool(LVStringPool &&other) = delete;
  /// Destroy the string pool.
  ~LVStringPool() = default;

  /// Return whether \p Index is a valid pool index.
  /// \param Index Candidate index to validate.
  /// \returns True when \p Index is not the sentinel bad index.
  bool isValidIndex(size_t Index) const { return Index != BadIndex; }

  /// Return the number of non-empty strings in the pool.
  ///
  /// The empty string is allocated at slot zero. One is subtracted so the
  /// count excludes that empty entry.
  /// \returns Count of interned strings, excluding the empty string at index
  /// zero.
  size_t getSize() const { return Entries.size() - 1; }

  /// Return the index of \p Key if it is already in the pool.
  /// \param Key String to look up.
  /// \returns Existing index, or the sentinel bad index when absent.
  size_t findIndex(StringRef Key) const {
    TableType::const_iterator Iter = StringTable.find(Key);
    if (Iter != StringTable.end())
      return Iter->second;
    return BadIndex;
  }

  /// Return the index for \p Key, inserting it when not already present.
  /// \param Key String to look up or intern.
  /// \returns Stable index for \p Key in the pool.
  size_t getIndex(StringRef Key) {
    size_t Index = findIndex(Key);
    if (isValidIndex(Index))
      return Index;
    size_t Value = Entries.size();
    ValueType *Entry = ValueType::create(Key, Allocator, Value);
    StringTable.insert(Entry);
    Entries.push_back(Entry);
    return Value;
  }

  /// Return the string stored at \p Index.
  /// \param Index Pool index previously returned by getIndex or findIndex.
  /// \returns Corresponding string, or an empty StringRef when out of range.
  StringRef getString(size_t Index) const {
    return (Index >= Entries.size()) ? StringRef() : Entries[Index]->getKey();
  }

  /// Print the contents of the string pool to \p OS.
  /// \param OS Stream that receives the printed pool entries.
  void print(raw_ostream &OS) const {
    if (!Entries.empty()) {
      OS << "\nString Pool:\n";
      for (const ValueType *Entry : Entries)
        OS << "Index: " << Entry->getValue() << ", "
           << "Key: '" << Entry->getKey() << "'\n";
    }
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this string pool to the debug stream.
  void dump() const { print(dbgs()); }
#endif
};

} // namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSTRINGPOOL_H
