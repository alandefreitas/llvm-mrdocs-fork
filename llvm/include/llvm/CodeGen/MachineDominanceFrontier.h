//===- llvm/CodeGen/MachineDominanceFrontier.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEDOMINANCEFRONTIER_H
#define LLVM_CODEGEN_MACHINEDOMINANCEFRONTIER_H

#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/DominanceFrontierImpl.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/Support/GenericDomTree.h"

namespace llvm {

/// Dominance frontier analysis result for a MachineFunction's MachineBasicBlocks.
class MachineDominanceFrontier
    : public DominanceFrontierBase<MachineBasicBlock, false> {
public:
  /// Dominator tree type specialized for MachineBasicBlock.
  using DomTreeT = DomTreeBase<MachineBasicBlock>;
  /// Dominator tree node type specialized for MachineBasicBlock.
  using DomTreeNodeT = DomTreeNodeBase<MachineBasicBlock>;
  /// Set of blocks in the dominance frontier of a single block.
  using DomSetType = MachineDominanceFrontier::DomSetType;
  /// Mutable iterator over the dominance frontier map.
  using iterator = MachineDominanceFrontier::iterator;
  /// Const iterator over the dominance frontier map.
  using const_iterator = MachineDominanceFrontier ::const_iterator;

  /// Construct an empty machine dominance frontier.
  MachineDominanceFrontier() = default;

  /// Handle invalidation explicitly.
  /// @param F Machine function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(MachineFunction &F, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);
};

/// Legacy analysis pass which computes a \c MachineDominanceFrontier.
class LLVM_ABI MachineDominanceFrontierWrapperPass
    : public MachineFunctionPass {
private:
  MachineDominanceFrontier MDF;

public:
  /// Construct the legacy MachineDominanceFrontier wrapper pass.
  MachineDominanceFrontierWrapperPass();

  /// Deleted copy constructor; this pass is not copyable.
  /// @param Other Unused; copy construction is deleted.
  MachineDominanceFrontierWrapperPass(
      const MachineDominanceFrontierWrapperPass &Other) = delete;
  /// Deleted copy assignment; this pass is not copyable.
  /// @param Other Unused; copy assignment is deleted.
  MachineDominanceFrontierWrapperPass &
  operator=(const MachineDominanceFrontierWrapperPass &Other) = delete;

  /// Pass identification, replacement for typeid.
  static char ID;

  /// Compute the MachineDominanceFrontier for \p F.
  /// @param F Machine function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnMachineFunction(MachineFunction &F) override;

  /// Report analysis usage for this pass.
  /// @param AU Analysis usage to populate with required and preserved analyses.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Release the MachineDominanceFrontier owned by this pass.
  void releaseMemory() override;

  /// Return the MachineDominanceFrontier computed by this pass.
  /// @return The MachineDominanceFrontier computed by this pass.
  MachineDominanceFrontier &getMDF() { return MDF; }
};

/// Analysis pass which computes a \c MachineDominanceFrontier.
class MachineDominanceFrontierAnalysis
    : public AnalysisInfoMixin<MachineDominanceFrontierAnalysis> {
  friend AnalysisInfoMixin<MachineDominanceFrontierAnalysis>;
  static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = MachineDominanceFrontier;

  /// Run the analysis pass over a machine function and produce a dominance frontier.
  /// @param MF Machine function to analyze.
  /// @param MFAM Machine function analysis manager providing MachineDominatorTree.
  /// @return The computed MachineDominanceFrontier for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEDOMINANCEFRONTIER_H
