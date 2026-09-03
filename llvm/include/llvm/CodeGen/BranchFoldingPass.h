//===- llvm/CodeGen/BranchFoldingPass.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_BRANCHFOLDINGPASS_H
#define LLVM_CODEGEN_BRANCHFOLDINGPASS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that optimizes the machine CFG by folding branches.
///
/// Deletes branches to branches, eliminates branches to successor blocks
/// (creating fallthroughs), eliminates branches over branches, and optionally
/// performs tail merging.
class BranchFolderPass : public OptionalPassInfoMixin<BranchFolderPass> {
  bool EnableTailMerge;

public:
  /// Construct a branch-folding pass, optionally enabling tail merging.
  /// \param EnableTailMerge Whether identical block tails may be merged.
  BranchFolderPass(bool EnableTailMerge) : EnableTailMerge(EnableTailMerge) {}
  /// Optimize branches in \p MF using the branch-folding algorithm.
  /// \param MF Machine function whose CFG is optimized.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after branch folding.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Branch folding expects that the function contains no PHI instructions.
  /// \return Required properties, with NoPHIs set.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setNoPHIs();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_BRANCHFOLDINGPASS_H
