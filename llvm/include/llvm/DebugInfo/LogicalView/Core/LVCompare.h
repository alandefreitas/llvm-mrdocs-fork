//===-- LVCompare.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVCompare class, which is used to describe a logical
// view comparison.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVCOMPARE_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVCOMPARE_H

#include "llvm/DebugInfo/LogicalView/Core/LVObject.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
/// Logical view types and utilities for comparing debug information.
namespace logicalview {

class LVReader;

/// One missing or added element together with its comparison pass.
using LVPassEntry = std::tuple<LVReader *, LVElement *, LVComparePass>;
/// Table of missing/added elements recorded during comparison passes.
using LVPassTable = std::vector<LVPassEntry>;

/// Compares logical views and reports missing or added elements.
class LVCompare final {
  raw_ostream &OS;
  LVScopes ScopeStack;

  // As the comparison is performed twice (by exchanging the reference
  // and target readers) the element missing/added status does specify
  // the comparison pass.
  // By recording each missing/added elements along with its pass, it
  // allows checking which elements were missing/added during each pass.
  LVPassTable PassTable;

  // Reader used on the LHS of the comparison.
  // In the 'Missing' pass, it points to the reference reader.
  // In the 'Added' pass it points to the target reader.
  LVReader *Reader = nullptr;

  bool FirstMissing = true;
  bool PrintLines = false;
  bool PrintScopes = false;
  bool PrintSymbols = false;
  bool PrintTypes = false;

  static void setInstance(LVCompare *Compare);

  void printCurrentStack();
  void printSummary() const;

public:
  /// Default construction is deleted; an output stream is required.
  LVCompare() = delete;
  /// Construct a comparator that writes comparison output to \p OS.
  /// \param OS Stream used for comparison reports.
  LLVM_ABI LVCompare(raw_ostream &OS);
  /// Copy construction is not allowed.
  /// \param Other Unused source comparator instance.
  LVCompare(const LVCompare &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source comparator instance.
  LVCompare &operator=(const LVCompare &Other) = delete;
  /// Destroy the comparator.
  ~LVCompare() = default;

  /// Return the current global LVCompare instance.
  /// \returns Reference to the active global comparator.
  LLVM_ABI static LVCompare &getInstance();

  /// Push \p Scope onto the stack used while reporting missing/added items.
  /// \param Scope Scope to record on the reporting stack.
  void push(LVScope *Scope) { ScopeStack.push_back(Scope); }
  /// Pop the top scope from the missing/added reporting stack.
  void pop() { ScopeStack.pop_back(); }

  /// Compare the scope trees of \p ReferenceReader and \p TargetReader.
  /// \param ReferenceReader Reader providing the reference logical view.
  /// \param TargetReader Reader providing the target logical view.
  /// \returns Success or an error describing why comparison failed.
  LLVM_ABI Error execute(LVReader *ReferenceReader, LVReader *TargetReader);

  /// Record that \p Element was missing or added in \p Pass for \p Reader.
  /// \param Reader Reader associated with this comparison pass.
  /// \param Element Element that was missing or added.
  /// \param Pass Whether the element was missing or added.
  void addPassEntry(LVReader *Reader, LVElement *Element, LVComparePass Pass) {
    PassTable.emplace_back(Reader, Element, Pass);
  }
  /// Return the table of missing/added elements recorded so far.
  /// \returns Const reference to the recorded pass table.
  const LVPassTable &getPassTable() const & { return PassTable; }

  /// Print a report line for \p Element under comparison \p Pass.
  /// \param Element Element that was missing or added.
  /// \param Pass Whether the element was missing or added.
  LLVM_ABI void printItem(LVElement *Element, LVComparePass Pass);
  /// Print the comparison results to \p OS.
  /// \param OS Stream that receives the printed comparison output.
  LLVM_ABI void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump the comparison results to the debug stream.
  void dump() const { print(dbgs()); }
#endif
};

/// Return the current global LVCompare instance.
/// \returns Reference to the active global comparator.
inline LVCompare &getComparator() { return LVCompare::getInstance(); }

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVCOMPARE_H
