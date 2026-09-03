//===- OptionStrCmp.h - Option String Comparison ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_OPTIONSTRCMP_H
#define LLVM_SUPPORT_OPTIONSTRCMP_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {

/// Compare two option name (or prefix) strings for sorting.
///
/// The ordering is *almost* case-insensitive lexicographic, with an exception.
/// `\0` comes at the end of the alphabet instead of the beginning (thus options
/// precede any other options which prefix them). Additionally, if two options
/// are identical ignoring case, they are ordered according to case-sensitive
/// ordering if \p FallbackCaseSensitive is true.
///
/// \param A First option name or prefix string.
/// \param B Second option name or prefix string.
/// \param FallbackCaseSensitive When true and the strings compare equal
///        ignoring case, fall back to a case-sensitive comparison.
/// \return Negative if \p A sorts before \p B, positive if after, and zero if
///         equal under the rules above.
LLVM_ABI int StrCmpOptionName(StringRef A, StringRef B,
                              bool FallbackCaseSensitive = true);

/// Compare two option prefix sequences for sorting.
///
/// Compares corresponding elements with \c StrCmpOptionName until a difference
/// is found. Equal-length identical sequences compare equal.
///
/// \param APrefixes First sequence of option prefixes.
/// \param BPrefixes Second sequence of option prefixes.
/// \return Negative if \p APrefixes sorts before \p BPrefixes, positive if
///         after, and zero if equal.
LLVM_ABI int StrCmpOptionPrefixes(ArrayRef<StringRef> APrefixes,
                                  ArrayRef<StringRef> BPrefixes);

} // namespace llvm

#endif // LLVM_SUPPORT_OPTIONSTRCMP_H
