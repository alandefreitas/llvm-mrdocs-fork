//===- llvm/Support/Unicode.h - Unicode character properties  -*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines functions that allow querying certain properties of Unicode
// characters.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_UNICODE_H
#define LLVM_SUPPORT_UNICODE_H

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Compiler.h"
#include <optional>
#include <string>

namespace llvm {
class StringRef;

namespace sys {
/// Utilities for querying Unicode character properties and names.
namespace unicode {

/// Error codes returned by column-width helpers instead of a non-negative
/// width.
enum ColumnWidthErrors {
  /// The input was not valid UTF-8.
  ErrorInvalidUTF8 = -2,
  /// The input contained a non-printable character.
  ErrorNonPrintableCharacter = -1
};

/// Returns whether \p UCS is likely to display correctly on a terminal.
///
/// Exact behavior would have to depend on the specific terminal, so we define
/// the semantic that should be suitable for a generic Unicode-capable terminal.
///
/// Printable codepoints are those in the categories L, M, N, P, S and Zs.
///
/// \param UCS Unicode code point to test.
/// \return true if the character is considered printable.
LLVM_ABI bool isPrintable(int UCS);

/// Returns whether \p UCS is a Unicode formatting character (category Cf).
///
/// \param UCS Unicode code point to test.
/// \return true if the character is in the Cf category.
LLVM_ABI bool isFormatting(int UCS);

/// Returns the terminal column width of UTF-8 string \p Text.
///
/// This depends on the implementation of the terminal, and there's no standard
/// definition of character width. The implementation defines it in a way that
/// is expected to be compatible with a generic Unicode-capable terminal.
///
/// \param Text UTF-8 text whose column width is measured.
/// \return Character width:
///   * ErrorNonPrintableCharacter (-1) if \p Text contains non-printable
///     characters (as identified by isPrintable);
///   * 0 for each non-spacing and enclosing combining mark;
///   * 2 for each CJK character excluding halfwidth forms;
///   * 1 for each of the remaining characters.
LLVM_ABI int columnWidthUTF8(StringRef Text);

/// Folds \p C according to the Simple Unicode case folding rules.
///
/// \param C Unicode code point to fold.
/// \return The simple-case-folded code point.
LLVM_ABI int foldCharSimple(int C);

/// Maps a Unicode character name or alias to its code point with exact match.
///
/// The names and aliases are derived from UnicodeData.txt and NameAliases.txt.
/// For compatibility with the semantics of named character escape sequences in
/// C++, this mapping does an exact match sensitive to casing and spacing.
///
/// \param Name Character name or alias to look up.
/// \return The codepoint of the corresponding character, if any.
LLVM_ABI std::optional<char32_t> nameToCodepointStrict(StringRef Name);

/// Result of a successful loose Unicode character name lookup.
struct LooseMatchingResult {
  /// Matched Unicode code point.
  char32_t CodePoint;
  /// Canonical character name corresponding to \c CodePoint.
  SmallString<64> Name;
};

/// Maps a Unicode character name or alias to its code point with loose match.
///
/// Matching ignores differences in case, whitespace, underscores, and most
/// medial hyphens (UAX44-LM2).
///
/// \param Name Character name or alias to look up.
/// \return The matched code point and canonical name, if any.
LLVM_ABI std::optional<LooseMatchingResult>
nameToCodepointLooseMatching(StringRef Name);

/// A Unicode character name ranked by edit distance to a search pattern.
struct MatchForCodepointName {
  /// Matching character name.
  std::string Name;
  /// Edit distance between \c Name and the search pattern.
  uint32_t Distance = 0;
  /// Unicode code point for \c Name.
  char32_t Value = 0;
};

/// Finds Unicode character names nearest to \p Pattern by edit distance.
///
/// \param Pattern Name fragment to match against.
/// \param MaxMatchesCount Maximum number of matches to return.
/// \return Up to \p MaxMatchesCount nearest matches, sorted by distance.
LLVM_ABI SmallVector<MatchForCodepointName>
nearestMatchesForCodepointName(StringRef Pattern, std::size_t MaxMatchesCount);

} // namespace unicode
} // namespace sys
} // namespace llvm

#endif
