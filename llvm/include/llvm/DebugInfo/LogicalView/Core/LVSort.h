//===-- LVSort.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the sort algorithms.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSORT_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSORT_H

#include "llvm/Support/Compiler.h"

namespace llvm {
namespace logicalview {

class LVObject;

/// Mode used when sorting logical-view objects.
enum class LVSortMode {
  /// Do not apply a sort order.
  None = 0,
  /// Sort objects by their unique ID.
  ID,
  /// Sort objects by kind, then name, line, and offset.
  Kind,
  /// Sort objects by line number, then name, kind, and offset.
  Line,
  /// Sort objects by name, then line, kind, and offset.
  Name,
  /// Sort objects by DIE offset.
  Offset
};

/// Integer result returned by a logical-view sort comparator.
using LVSortValue = int;
/// Comparator function type used when sorting logical-view objects.
using LVSortFunction = LVSortValue (*)(const LVObject *LHS,
                                       const LVObject *RHS);

/// Return the comparator selected by the current command-line sort options.
/// \returns Comparator for the active LVSortMode, or nullptr when unsorted.
LLVM_ABI LVSortFunction getSortFunction();

/// Compare two objects by unique ID.
/// \param LHS Left-hand object in the comparison.
/// \param RHS Right-hand object in the comparison.
/// \returns Non-zero when \p LHS should order before \p RHS.
LLVM_ABI LVSortValue compareID(const LVObject *LHS, const LVObject *RHS);
/// Compare two objects by kind string.
/// \param LHS Left-hand object in the comparison.
/// \param RHS Right-hand object in the comparison.
/// \returns Non-zero when \p LHS should order before \p RHS.
LLVM_ABI LVSortValue compareKind(const LVObject *LHS, const LVObject *RHS);
/// Compare two objects by source line number.
/// \param LHS Left-hand object in the comparison.
/// \param RHS Right-hand object in the comparison.
/// \returns Non-zero when \p LHS should order before \p RHS.
LLVM_ABI LVSortValue compareLine(const LVObject *LHS, const LVObject *RHS);
/// Compare two objects by name.
/// \param LHS Left-hand object in the comparison.
/// \param RHS Right-hand object in the comparison.
/// \returns Non-zero when \p LHS should order before \p RHS.
LLVM_ABI LVSortValue compareName(const LVObject *LHS, const LVObject *RHS);
/// Compare two objects by DIE offset.
/// \param LHS Left-hand object in the comparison.
/// \param RHS Right-hand object in the comparison.
/// \returns Non-zero when \p LHS should order before \p RHS.
LLVM_ABI LVSortValue compareOffset(const LVObject *LHS, const LVObject *RHS);
/// Compare two objects by address range (lower, then upper).
/// \param LHS Left-hand object in the comparison.
/// \param RHS Right-hand object in the comparison.
/// \returns Non-zero when \p LHS should order before \p RHS.
LLVM_ABI LVSortValue compareRange(const LVObject *LHS, const LVObject *RHS);
/// Sort two objects by kind, then name, line, and offset.
/// \param LHS Left-hand object in the comparison.
/// \param RHS Right-hand object in the comparison.
/// \returns Non-zero when \p LHS should order before \p RHS.
LLVM_ABI LVSortValue sortByKind(const LVObject *LHS, const LVObject *RHS);
/// Sort two objects by line, then name, kind, and offset.
/// \param LHS Left-hand object in the comparison.
/// \param RHS Right-hand object in the comparison.
/// \returns Non-zero when \p LHS should order before \p RHS.
LLVM_ABI LVSortValue sortByLine(const LVObject *LHS, const LVObject *RHS);
/// Sort two objects by name, then line, kind, and offset.
/// \param LHS Left-hand object in the comparison.
/// \param RHS Right-hand object in the comparison.
/// \returns Non-zero when \p LHS should order before \p RHS.
LLVM_ABI LVSortValue sortByName(const LVObject *LHS, const LVObject *RHS);

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSORT_H
