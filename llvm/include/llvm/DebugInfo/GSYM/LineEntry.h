//===- LineEntry.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_LINEENTRY_H
#define LLVM_DEBUGINFO_GSYM_LINEENTRY_H

#include "llvm/DebugInfo/GSYM/ExtractRanges.h"

namespace llvm {
namespace gsym {

/// A single row in a GSYM function line table.
///
/// Line entries are used to encode the line tables in FunctionInfo objects.
/// They are stored as a sorted vector of these objects and store the
/// address, file and line of the line table row for a given address. The
/// size of a line table entry is calculated by looking at the next entry
/// in the FunctionInfo's vector of entries.
struct LineEntry {
  uint64_t Addr; ///< Start address of this line entry.
  uint32_t File; ///< 1 based index of file in FileTable
  uint32_t Line; ///< Source line number.

  /// Construct a LineEntry from address, file index, and line number.
  ///
  /// \param A Start address of this line entry.
  /// \param F 1-based index of the file in the FileTable.
  /// \param L Source line number.
  LineEntry(uint64_t A = 0, uint32_t F = 0, uint32_t L = 0)
      : Addr(A), File(F), Line(L) {}

  /// Return true if this line entry refers to a valid file index.
  ///
  /// \returns True if this line entry refers to a valid file index.
  bool isValid() { return File != 0; }
};

/// Stream a human-readable representation of \p LE to \p OS.
///
/// \param OS Destination stream.
/// \param LE LineEntry to print.
/// \returns A reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const LineEntry &LE) {
  return OS << "addr=" << HEX64(LE.Addr) << ", file=" << format("%3u", LE.File)
      << ", line=" << format("%3u", LE.Line);
}

/// Equality comparison operator for LineEntry.
///
/// \param LHS The left-hand LineEntry to compare.
/// \param RHS The right-hand LineEntry to compare.
/// \returns True if address, file, and line all match.
inline bool operator==(const LineEntry &LHS, const LineEntry &RHS) {
  return LHS.Addr == RHS.Addr && LHS.File == RHS.File && LHS.Line == RHS.Line;
}

/// Inequality comparison operator for LineEntry.
///
/// \param LHS The left-hand LineEntry to compare.
/// \param RHS The right-hand LineEntry to compare.
/// \returns True if address, file, or line differ.
inline bool operator!=(const LineEntry &LHS, const LineEntry &RHS) {
  return !(LHS == RHS);
}

/// Less-than comparison operator for LineEntry, ordered by address.
///
/// \param LHS The left-hand LineEntry to compare.
/// \param RHS The right-hand LineEntry to compare.
/// \returns True if \p LHS has a smaller address than \p RHS.
inline bool operator<(const LineEntry &LHS, const LineEntry &RHS) {
  return LHS.Addr < RHS.Addr;
}
} // namespace gsym
} // namespace llvm
#endif // LLVM_DEBUGINFO_GSYM_LINEENTRY_H
