//===-------------------------------------------------------------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_FRONTEND_DIRECTIVE_SPELLING_H
#define LLVM_FRONTEND_DIRECTIVE_SPELLING_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Compiler.h"

#include <limits>
#include <tuple>

namespace llvm {
/// Namespace for versioned spellings of frontend directives and clauses.
namespace directive {

/// Inclusive integer interval of language versions for a spelling.
struct VersionRange {
  /// Sentinel for an unbounded inclusive upper version bound.
  static constexpr int MaxValue = std::numeric_limits<int>::max();
  /// Inclusive lower bound of the version range.
  ///
  /// The default Version in get<Lang><Enum>Name() is 0; a default-constructed
  /// range includes that value by starting at 0.
  int Min = 0;
  /// Inclusive upper bound of the version range.
  int Max = MaxValue;

  /// Order ranges lexicographically by (Min, Max).
  /// \param R Version range to compare against.
  /// \return True if this range is ordered before \p R.
  bool operator<(const VersionRange &R) const {
    return std::tie(Min, Max) < std::tie(R.Min, R.Max);
  }
};

/// Directive or clause name valid for a range of language versions.
struct Spelling {
  /// Textual spelling of the directive or clause.
  StringRef Name;
  /// Language versions for which this spelling is valid.
  VersionRange Versions;
};

/// Find the first spelling in a range that applies to a language version.
/// \param Range Spellings to search; typically a TableGen-generated table.
/// \param Version Language version to match against each spelling's range.
/// \return The matching spelling name, or an empty StringRef if none apply.
LLVM_ABI StringRef FindName(llvm::iterator_range<const Spelling *> Range,
                            unsigned Version);

} // namespace directive
} // namespace llvm

#endif // LLVM_FRONTEND_DIRECTIVE_SPELLING_H
