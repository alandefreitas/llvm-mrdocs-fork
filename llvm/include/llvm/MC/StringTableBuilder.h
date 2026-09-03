//===- StringTableBuilder.h - String table building utility -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_STRINGTABLEBUILDER_H
#define LLVM_MC_STRINGTABLEBUILDER_H

#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Compiler.h"
#include <cstddef>
#include <cstdint>

namespace llvm {

class raw_ostream;

/// Utility for building string tables with deduplicated suffixes.
class StringTableBuilder {
public:
  /// Object-file format that determines string-table layout.
  enum Kind {
    /// ELF string table (leading NUL byte).
    ELF,
    /// Windows COFF string table (little-endian size header).
    WinCOFF,
    /// 32-bit Mach-O string table.
    MachO,
    /// 64-bit Mach-O string table.
    MachO64,
    /// Final-linked 32-bit Mach-O string table (starts with a space).
    MachOLinked,
    /// Final-linked 64-bit Mach-O string table (starts with a space).
    MachO64Linked,
    /// Raw string table without trailing NUL bytes.
    RAW,
    /// DWARF string table with no leading reserved bytes.
    DWARF,
    /// XCOFF string table (big-endian size header).
    XCOFF,
    /// DXContainer string table (leading NUL byte).
    DXContainer
  };

private:
  // Only non-zero priority will be recorded.
  DenseMap<CachedHashStringRef, uint8_t> StringPriorityMap;
  DenseMap<CachedHashStringRef, size_t> StringIndexMap;
  size_t Size = 0;
  Kind K;
  Align Alignment;
  bool Finalized = false;

  void finalizeStringTable(bool Optimize);
  void initSize();

public:
  /// Construct a string table builder for the given format.
  ///
  /// \param K Object-file format that controls string-table layout.
  /// \param Alignment Minimum alignment of each string's offset in the table.
  LLVM_ABI StringTableBuilder(Kind K, Align Alignment = Align(1));
  /// Destroy the builder and release its internal string tables.
  LLVM_ABI ~StringTableBuilder();

  /// Add a string to the builder.
  ///
  /// Returns the position of S in the table. The position will be changed if
  /// finalize is used. Can only be used before the table is finalized.
  /// Priority is only useful with reordering. Strings with the same priority
  /// will be put together. Strings with higher priority are placed closer to
  /// the begin of string table. When adding same string with different
  /// priority, the maximum priority win.
  ///
  /// \param S String to add.
  /// \param Priority Ordering priority used when finalizing with reordering.
  /// \return Position of \p S in the table (may change after finalize).
  LLVM_ABI size_t add(CachedHashStringRef S, uint8_t Priority = 0);
  /// Add string \p S to the builder; equivalent to \c add(CachedHashStringRef(S)).
  ///
  /// \param S String to add.
  /// \param Priority Ordering priority used when finalizing with reordering.
  /// \return Position of \p S in the table (may change after finalize).
  size_t add(StringRef S, uint8_t Priority = 0) {
    return add(CachedHashStringRef(S), Priority);
  }

  /// Analyze the strings and build the final table. No more strings can
  /// be added after this point.
  LLVM_ABI void finalize();

  /// Finalize the string table without reording it. In this mode, offsets
  /// returned by add will still be valid.
  LLVM_ABI void finalizeInOrder();

  /// Get the offset of a string in the string table. Can only be used
  /// after the table is finalized.
  ///
  /// \param S String whose finalized offset is requested.
  /// \return Finalized offset of \p S in the string table.
  LLVM_ABI size_t getOffset(CachedHashStringRef S) const;
  /// Get the offset of string \p S; equivalent to
  /// \c getOffset(CachedHashStringRef(S)).
  ///
  /// \param S String whose finalized offset is requested.
  /// \return Finalized offset of \p S in the string table.
  size_t getOffset(StringRef S) const {
    return getOffset(CachedHashStringRef(S));
  }

  /// Check whether a string has already been added.
  ///
  /// Since this class doesn't store the string values, this function can be
  /// used to check if storage needs to be done prior to adding the string.
  ///
  /// \param S String to look up.
  /// \return True if \p S has already been added.
  bool contains(StringRef S) const { return contains(CachedHashStringRef(S)); }
  /// Check whether string \p S has already been added.
  ///
  /// \param S String to look up.
  /// \return True if \p S has already been added.
  bool contains(CachedHashStringRef S) const { return StringIndexMap.count(S); }

  /// Return true if no strings have been added.
  ///
  /// \return True if no strings have been added.
  bool empty() const { return StringIndexMap.empty(); }
  /// Return the current size of the string table in bytes.
  ///
  /// \return Current size of the string table in bytes.
  size_t getSize() const { return Size; }
  /// Clear all strings and reset the builder to a non-finalized state.
  LLVM_ABI void clear();

  /// Write the finalized string table to \p OS.
  ///
  /// \param OS Stream to write the string table to.
  LLVM_ABI void write(raw_ostream &OS) const;
  /// Write the finalized string table into \p Buf.
  ///
  /// \param Buf Buffer of at least \c getSize() bytes to receive the table.
  LLVM_ABI void write(uint8_t *Buf) const;

  /// Return whether the string table has been finalized.
  ///
  /// \return True if the string table has been finalized.
  bool isFinalized() const { return Finalized; }
};

} // end namespace llvm

#endif // LLVM_MC_STRINGTABLEBUILDER_H
