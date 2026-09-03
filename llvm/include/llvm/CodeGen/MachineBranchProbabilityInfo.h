//=- MachineBranchProbabilityInfo.h - Branch Probability Analysis -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass is used to evaluate branch probabilties on machine basic blocks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEBRANCHPROBABILITYINFO_H
#define LLVM_CODEGEN_MACHINEBRANCHPROBABILITYINFO_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/BranchProbability.h"

namespace llvm {

class MachineBranchProbabilityInfo {
  // Default weight value. Used when we don't have information about the edge.
  // TODO: DEFAULT_WEIGHT makes sense during static predication, when none of
  // the successors have a weight yet. But it doesn't make sense when providing
  // weight to an edge that may have siblings with non-zero weights. This can
  // be handled various ways, but it's probably fine for an edge with unknown
  // weight to just "inherit" the non-zero weight of an adjacent successor.
  static const uint32_t DEFAULT_WEIGHT = 16;

public:
  /// Handle invalidation explicitly.
  /// @param MF Machine function being invalidated.
  /// @param PA Set of preserved analyses.
  /// @param Inv Invalidator for dependent analyses.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(MachineFunction &MF, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);

  /// Get the probability of going from Src to Dst.
  ///
  /// It returns the sum of all probabilities for edges from Src to Dst.
  /// @param Src Source block of the edge(s).
  /// @param Dst Destination block of the edge(s).
  /// @return Sum of probabilities of all edges from \p Src to \p Dst.
  LLVM_ABI BranchProbability getEdgeProbability(
      const MachineBasicBlock *Src, const MachineBasicBlock *Dst) const;

  /// Get an edge's probability by successor index.
  ///
  /// Same as getEdgeProbability(Src, Dst), but using the successor index from
  /// Src. This is faster.
  /// @param Src Source block of the edge.
  /// @param SuccIdx Index of the successor edge leaving \p Src.
  /// @return Relative probability of the edge, never zero.
  LLVM_ABI BranchProbability getEdgeProbability(const MachineBasicBlock *Src,
                                                unsigned SuccIdx) const;

  /// Test if an edge is hot relative to other out-edges of the Src.
  ///
  /// Check whether this edge out of the source block is 'hot'. We define hot
  /// as having a relative probability > 80%.
  /// @param Src Source block of the edge.
  /// @param Dst Destination block of the edge.
  /// @return True if the edge probability is greater than 80%.
  LLVM_ABI bool isEdgeHot(const MachineBasicBlock *Src,
                          const MachineBasicBlock *Dst) const;

  /// Print an edge's probability.
  ///
  /// Retrieves an edge's probability similarly to \see getEdgeProbability, but
  /// then prints that probability to the provided stream. That stream is then
  /// returned. The printed value is between 0 (0% probability) and 1 (100%
  /// probability); it is never equal to 0, and can be 1 only if the source
  /// block has only one successor.
  /// @param OS Output stream to write the probability to.
  /// @param Src Source block of the edge.
  /// @param Dst Destination block of the edge.
  /// @return The output stream \p OS after writing.
  LLVM_ABI raw_ostream &
  printEdgeProbability(raw_ostream &OS, const MachineBasicBlock *Src,
                       const MachineBasicBlock *Dst) const;
};

/// Analysis pass which computes \c MachineBranchProbabilityInfo.
class MachineBranchProbabilityAnalysis
    : public AnalysisInfoMixin<MachineBranchProbabilityAnalysis> {
  friend AnalysisInfoMixin<MachineBranchProbabilityAnalysis>;

  static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = MachineBranchProbabilityInfo;

  /// Run the analysis pass over a machine function and produce MBPI.
  /// @param MF Machine function to analyze.
  /// @param MFAM Machine function analysis manager providing dependencies.
  /// @return Computed machine branch probability information for \p MF.
  LLVM_ABI Result run(MachineFunction &MF, MachineFunctionAnalysisManager &MFAM);
};

/// Printer pass for the \c MachineBranchProbabilityAnalysis results.
class MachineBranchProbabilityPrinterPass
    : public RequiredPassInfoMixin<MachineBranchProbabilityPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes machine branch probabilities to \p OS.
  /// @param OS Output stream for the printed probabilities.
  MachineBranchProbabilityPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print machine branch probability results for \p MF.
  /// @param MF Machine function whose probabilities are printed.
  /// @param MFAM Machine function analysis manager providing
  ///        MachineBranchProbabilityAnalysis.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// Legacy immutable pass wrapping \c MachineBranchProbabilityInfo.
class LLVM_ABI MachineBranchProbabilityInfoWrapperPass : public ImmutablePass {
  virtual void anchor();

  MachineBranchProbabilityInfo MBPI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy machine branch probability analysis wrapper pass.
  MachineBranchProbabilityInfoWrapperPass();

  /// Declare required and preserved analyses for this pass.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  /// Return the cached MachineBranchProbabilityInfo.
  /// @return Mutable reference to the cached analysis result.
  MachineBranchProbabilityInfo &getMBPI() { return MBPI; }
  /// Return the cached MachineBranchProbabilityInfo.
  /// @return Const reference to the cached analysis result.
  const MachineBranchProbabilityInfo &getMBPI() const { return MBPI; }
};
}


#endif
