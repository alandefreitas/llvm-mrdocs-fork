//===- llvm/CodeGen/MachinePostDominators.h ----------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes interfaces to post dominance information for
// target-specific code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEPOSTDOMINATORS_H
#define LLVM_CODEGEN_MACHINEPOSTDOMINATORS_H

#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

extern template class LLVM_TEMPLATE_ABI
    DominatorTreeBase<MachineBasicBlock, true>; // PostDomTree

namespace DomTreeBuilder {
/// Post-dominator tree over a MachineFunction CFG of MachineBasicBlocks.
using MBBPostDomTree = PostDomTreeBase<MachineBasicBlock>;
/// GraphDiff view of a MachineBasicBlock post-dominator tree CFG.
using MBBPostDomTreeGraphDiff = GraphDiff<MachineBasicBlock *, true>;

extern template LLVM_TEMPLATE_ABI void
Calculate<MBBPostDomTree>(MBBPostDomTree &DT);
extern template LLVM_TEMPLATE_ABI void
InsertEdge<MBBPostDomTree>(MBBPostDomTree &DT, MachineBasicBlock *From,
                           MachineBasicBlock *To);
extern template LLVM_TEMPLATE_ABI void
DeleteEdge<MBBPostDomTree>(MBBPostDomTree &DT, MachineBasicBlock *From,
                           MachineBasicBlock *To);
extern template LLVM_TEMPLATE_ABI void
ApplyUpdates<MBBPostDomTree>(MBBPostDomTree &DT, MBBPostDomTreeGraphDiff &,
                             MBBPostDomTreeGraphDiff *);
extern template LLVM_TEMPLATE_ABI bool
Verify<MBBPostDomTree>(const MBBPostDomTree &DT,
                       MBBPostDomTree::VerificationLevel VL);
} // namespace DomTreeBuilder

///
/// MachinePostDominatorTree - an analysis pass wrapper for DominatorTree
/// used to compute the post-dominator tree for MachineFunctions.
///
class MachinePostDominatorTree : public PostDomTreeBase<MachineBasicBlock> {
  using Base = PostDomTreeBase<MachineBasicBlock>;

public:
  /// Construct an empty machine post-dominator tree.
  MachinePostDominatorTree() = default;

  /// Construct a machine post-dominator tree for \p MF.
  /// @param MF Machine function whose CFG is analyzed.
  explicit MachinePostDominatorTree(MachineFunction &MF) { recalculate(MF); }

  /// Handle invalidation explicitly.
  /// @param MF Machine function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this result should be discarded.
  LLVM_ABI bool invalidate(MachineFunction &MF, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);

  /// Make findNearestCommonDominator(const NodeT *A, const NodeT *B) available.
  using Base::findNearestCommonDominator;

  /// Returns the nearest common dominator of the given blocks.
  /// If that tree node is a virtual root, a nullptr will be returned.
  /// @param Blocks Machine basic blocks whose nearest common dominator is sought.
  /// @return The nearest common dominator, or nullptr if it is a virtual root.
  LLVM_ABI MachineBasicBlock *
  findNearestCommonDominator(ArrayRef<MachineBasicBlock *> Blocks) const;
};

/// \brief Analysis pass which computes a \c MachinePostDominatorTree.
class MachinePostDominatorTreeAnalysis
    : public AnalysisInfoMixin<MachinePostDominatorTreeAnalysis> {
  friend AnalysisInfoMixin<MachinePostDominatorTreeAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  using Result = MachinePostDominatorTree;

  /// Run the analysis pass over a machine function and produce a post-dominator tree.
  /// @param MF Machine function to analyze.
  /// @param MFAM Machine function analysis manager (unused).
  /// @return The MachinePostDominatorTree computed for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// \brief Machine function pass which prints \c MachinePostDominatorTree.
class MachinePostDominatorTreePrinterPass
    : public RequiredPassInfoMixin<MachinePostDominatorTreePrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed machine post-dominator tree.
  explicit MachinePostDominatorTreePrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print the MachinePostDominatorTree for \p MF and return all analyses preserved.
  /// @param MF Machine function whose MachinePostDominatorTree is printed.
  /// @param MFAM Machine function analysis manager providing MachinePostDominatorTree.
  /// @return A PreservedAnalyses set with all analyses preserved.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// \brief Analysis pass which computes a \c MachinePostDominatorTree.
class LLVM_ABI MachinePostDominatorTreeWrapperPass
    : public MachineFunctionPass {
  std::optional<MachinePostDominatorTree> PDT;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy MachinePostDominatorTree wrapper pass.
  MachinePostDominatorTreeWrapperPass();

  /// Return the MachinePostDominatorTree computed by this pass.
  /// @return The MachinePostDominatorTree owned by this pass.
  MachinePostDominatorTree &getPostDomTree() { return *PDT; }
  /// Return the MachinePostDominatorTree computed by this pass.
  /// @return The MachinePostDominatorTree owned by this pass.
  const MachinePostDominatorTree &getPostDomTree() const { return *PDT; }

  /// Compute the MachinePostDominatorTree for \p MF.
  /// @param MF Machine function to analyze.
  /// @return False; this analysis does not modify \p MF.
  bool runOnMachineFunction(MachineFunction &MF) override;
  /// Report analysis usage for this pass.
  /// @param AU Analysis usage to populate; this pass preserves all analyses.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Release the MachinePostDominatorTree owned by this pass.
  void releaseMemory() override { PDT.reset(); }
  /// Verify the MachinePostDominatorTree computed by this pass.
  void verifyAnalysis() const override;
  /// Print the MachinePostDominatorTree computed by this pass.
  /// @param OS Output stream.
  /// @param M Optional module (unused).
  void print(llvm::raw_ostream &OS, const Module *M = nullptr) const override;
};
} //end of namespace llvm

#endif
