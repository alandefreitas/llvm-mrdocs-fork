//===- LookupResult.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_LOOKUPRESULT_H
#define LLVM_DEBUGINFO_GSYM_LOOKUPRESULT_H

#include "llvm/ADT/AddressRanges.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <inttypes.h>
#include <vector>

namespace llvm {
class raw_ostream;
namespace gsym {

/// A single source location for a looked-up address.
struct SourceLocation {
  StringRef Name;      ///< Function or symbol name.
  StringRef Dir;       ///< Line entry source file directory path.
  StringRef Base;      ///< Line entry source file basename.
  uint32_t Line = 0;   ///< Source file line number.
  uint32_t Offset = 0; ///< Byte size offset within the named function.
};

/// Equality comparison operator for SourceLocation.
///
/// \param LHS Left-hand SourceLocation.
/// \param RHS Right-hand SourceLocation.
/// \returns True if all fields of \p LHS and \p RHS are equal.
inline bool operator==(const SourceLocation &LHS, const SourceLocation &RHS) {
  return LHS.Name == RHS.Name && LHS.Dir == RHS.Dir && LHS.Base == RHS.Base &&
         LHS.Line == RHS.Line && LHS.Offset == RHS.Offset;
}

/// Stream a human-readable representation of \p R to \p OS.
///
/// \param OS Destination stream.
/// \param R Source location to print.
/// \returns \p OS after writing.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const SourceLocation &R);

/// A list of SourceLocation entries for an address lookup.
using SourceLocations = std::vector<SourceLocation>;

/// Result of looking up an address in a GSYM file.
///
/// Contains the lookup address, concrete function range and name, matching
/// source locations (including inlined frames), and optional call-site
/// function name regex patterns.
struct LookupResult {
  uint64_t LookupAddr = 0; ///< The address that this lookup pertains to.
  AddressRange FuncRange;  ///< The concrete function address range.
  StringRef FuncName; ///< The concrete function name that contains LookupAddr.
  /// Source locations that match the lookup address.
  ///
  /// This information will only be filled in if the FunctionInfo contains a
  /// line table. If an address is for a concrete function with no inlined
  /// functions, this array will have one entry. If an address points to an
  /// inline function, there will be one SourceLocation for each inlined
  /// function with the last entry pointing to the concrete function itself.
  /// This allows one address to generate multiple locations and allows
  /// unwinding of inline call stacks. The deepest inline function will appear
  /// at index zero in the source locations array, and the concrete function
  /// will appear at the end of the array.
  SourceLocations Locations;

  /// Regex patterns for functions that may be called at this address.
  ///
  /// This vector will be populated when:
  /// 1. The lookup address matches a call site's return address in a function
  /// 2. The call site has associated regex patterns that describe what
  /// functions can be called from that location
  ///
  /// The regex patterns can be used to validate function calls during runtime
  /// checking or symbolication. For example:
  /// - Patterns like "^foo$" indicate the call site can only call function
  /// "foo"
  /// - Patterns like "^std::" indicate the call site can call any function in
  ///   the std namespace
  /// - Multiple patterns allow matching against a set of allowed functions
  ///
  /// The patterns are stored as string references into the GSYM string table.
  /// This information is typically loaded from:
  /// - DWARF debug info call site entries
  /// - External YAML files specifying call site patterns
  /// - Other debug info formats that encode call site constraints
  ///
  /// The patterns will be empty if:
  /// - The lookup address is not at the return address of a call site
  /// - The call site has no associated function name constraints
  /// - Call site info was not included when creating the GSYM file
  std::vector<StringRef> CallSiteFuncRegex;

  /// Return the full source file path for the location at \p Index.
  ///
  /// Joins the directory and basename from \c Locations[\p Index], or returns
  /// whichever is available if one is empty. Returns an empty string if
  /// \p Index is out of range or both Dir and Base are empty.
  ///
  /// \param Index Index into the Locations array.
  /// \returns The full path as a string, or empty if unavailable.
  LLVM_ABI std::string getSourceFile(uint32_t Index) const;
};

/// Equality comparison operator for LookupResult.
///
/// \param LHS Left-hand LookupResult.
/// \param RHS Right-hand LookupResult.
/// \returns True if all fields of \p LHS and \p RHS are equal.
inline bool operator==(const LookupResult &LHS, const LookupResult &RHS) {
  if (LHS.LookupAddr != RHS.LookupAddr)
    return false;
  if (LHS.FuncRange != RHS.FuncRange)
    return false;
  if (LHS.FuncName != RHS.FuncName)
    return false;
  if (LHS.CallSiteFuncRegex != RHS.CallSiteFuncRegex)
    return false;
  return LHS.Locations == RHS.Locations;
}

/// Stream a human-readable representation of \p R to \p OS.
///
/// \param OS Destination stream.
/// \param R Lookup result to print.
/// \returns \p OS after writing.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const LookupResult &R);

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_LOOKUPRESULT_H
