//===- FileEntry.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_FILEENTRY_H
#define LLVM_DEBUGINFO_GSYM_FILEENTRY_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/DebugInfo/GSYM/GsymTypes.h"
#include <functional>
#include <stdint.h>

namespace llvm {
namespace gsym {

/// A GSYM file path split into directory and basename string-table offsets.
///
/// Files in GSYM are contained in FileEntry structs where we split the
/// directory and basename into two different strings in the string
/// table. This allows paths to shared commont directory and filename
/// strings and saves space.
struct FileEntry {

  /// String table offset of the directory path.
  gsym_strp_t Dir = 0;
  /// String table offset of the file basename.
  gsym_strp_t Base = 0;

  /// Construct a FileEntry with zero directory and basename offsets.
  FileEntry() = default;

  /// Construct a FileEntry from directory and basename string table offsets.
  ///
  /// \param D String table offset of the directory.
  /// \param B String table offset of the basename.
  FileEntry(gsym_strp_t D, gsym_strp_t B) : Dir(D), Base(B) {}

  /// Returns the on-disk encoded size of a FileEntry for the given string
  /// offset size. It's different from sizeof(FileEntry) because of padding.
  ///
  /// \param StringOffsetSize Size in bytes of each string table offset.
  /// \returns The encoded size in bytes for two string offsets.
  static constexpr uint64_t getEncodedSize(uint8_t StringOffsetSize) {
    return 2 * StringOffsetSize;
  }

  /// Equality comparison operator for FileEntry.
  ///
  /// \param RHS The FileEntry to compare against.
  /// \returns True if both directory and basename offsets match.
  bool operator==(const FileEntry &RHS) const {
    return Base == RHS.Base && Dir == RHS.Dir;
  };
  /// Inequality comparison operator for FileEntry.
  ///
  /// \param RHS The FileEntry to compare against.
  /// \returns True if the directory or basename offsets differ.
  bool operator!=(const FileEntry &RHS) const {
    return Base != RHS.Base || Dir != RHS.Dir;
  };
};

} // namespace gsym

/// DenseMapInfo specialization so FileEntry can be used as a DenseMap key.
template <> struct DenseMapInfo<gsym::FileEntry> {
  /// Compute a DenseMap hash for \p Val.
  ///
  /// \param Val FileEntry to hash.
  /// \returns A hash value suitable for DenseMap.
  static unsigned getHashValue(const gsym::FileEntry &Val) {
    return llvm::hash_combine(
        DenseMapInfo<gsym::gsym_strp_t>::getHashValue(Val.Dir),
        DenseMapInfo<gsym::gsym_strp_t>::getHashValue(Val.Base));
  }
  /// Return true if \p LHS and \p RHS are equal.
  ///
  /// \param LHS Left-hand FileEntry.
  /// \param RHS Right-hand FileEntry.
  /// \returns True if \p LHS and \p RHS are equal.
  static bool isEqual(const gsym::FileEntry &LHS, const gsym::FileEntry &RHS) {
    return LHS == RHS;
  }
};

} // namespace llvm
#endif // LLVM_DEBUGINFO_GSYM_FILEENTRY_H
