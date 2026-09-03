//===- llvm/CodeGen/GlobalMerge.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALMERGE_H
#define LLVM_CODEGEN_GLOBALMERGE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

/// Options that control how the global-merge pass groups and merges globals.
struct GlobalMergeOptions {
  /// Maximum offset in bytes between locations of merged globals.
  ///
  /// FIXME: Infer the maximum possible offset depending on the actual users
  /// (these max offsets are different for the users inside Thumb or ARM
  /// functions), see the code that passes in the offset in the ARM backend
  /// for more information.
  unsigned MaxOffset = 0;
  /// Minimum size in bytes for a global to be considered for merging.
  unsigned MinSize = 0;
  /// Whether globals should be grouped by their uses before merging.
  bool GroupByUse = true;
  /// Whether globals that are only used alone should be ignored for merging.
  bool IgnoreSingleUse = true;
  /// Whether we should merge global variables that have external linkage.
  bool MergeExternal = true;
  /// Whether we should merge constant global variables.
  bool MergeConstantGlobals = false;
  /// Whether we should merge constant global variables aggressively without
  /// looking at use.
  bool MergeConstAggressive = false;
  /// Whether we should try to optimize for size only.
  ///
  /// Currently, this applies a dead simple heuristic: only consider globals
  /// used in minsize functions for merging.
  /// FIXME: This could learn about optsize, and be used in the cost model.
  bool SizeOnly = false;
};

/// New PM pass that merges globals to reduce address-materialization cost.
///
/// FIXME: This pass must run before AsmPrinterPass::doInitialization!
class GlobalMergePass : public OptionalPassInfoMixin<GlobalMergePass> {
  const TargetMachine *TM;
  GlobalMergeOptions Options;

public:
  /// Construct a global-merge pass for target \p TM with the given \p Options.
  /// \param TM Target machine used to guide merging decisions.
  /// \param Options Configuration controlling which globals may be merged.
  GlobalMergePass(const TargetMachine *TM, GlobalMergeOptions Options)
      : TM(TM), Options(Options) {}

  /// Merge eligible global variables in module \p M.
  /// \param M Module whose globals are considered for merging.
  /// \param MAM Module analysis manager providing required analyses.
  /// \return The set of analyses preserved after merging globals.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_GLOBALMERGE_H
