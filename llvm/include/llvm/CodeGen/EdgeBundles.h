//===-------- EdgeBundles.h - Bundles of CFG edges --------------*- c++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The EdgeBundles analysis forms equivalence classes of CFG edges such that all
// edges leaving a machine basic block are in the same bundle, and all edges
// entering a machine basic block are in the same bundle.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_EDGEBUNDLES_H
#define LLVM_CODEGEN_EDGEBUNDLES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntEqClasses.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class EdgeBundlesWrapperLegacy;
class EdgeBundlesAnalysis;

/// Equivalence classes of CFG edges that share an incoming or outgoing block.
///
/// Forms bundles such that all edges leaving a machine basic block are in the
/// same bundle, and all edges entering a machine basic block are in the same
/// bundle.
class EdgeBundles {
  friend class EdgeBundlesWrapperLegacy;
  friend class EdgeBundlesAnalysis;

  const MachineFunction *MF = nullptr;

  /// EC - Each edge bundle is an equivalence class. The keys are:
  ///   2*BB->getNumber()   -> Ingoing bundle.
  ///   2*BB->getNumber()+1 -> Outgoing bundle.
  IntEqClasses EC;

  /// Blocks - Map each bundle to a list of basic block numbers.
  SmallVector<SmallVector<unsigned, 8>, 4> Blocks;

  void init();
  EdgeBundles(MachineFunction &MF);

public:
  /// getBundle - Return the ingoing (Out = false) or outgoing (Out = true)
  /// bundle number for basic block #N
  ///
  /// \param N Basic block number whose bundle is requested.
  /// \param Out If true, return the outgoing bundle; otherwise the ingoing.
  /// \return The equivalence-class bundle number for block \p N.
  unsigned getBundle(unsigned N, bool Out) const { return EC[2 * N + Out]; }

  /// getNumBundles - Return the total number of bundles in the CFG.
  ///
  /// \return The number of edge-bundle equivalence classes.
  unsigned getNumBundles() const { return EC.getNumClasses(); }

  /// getBlocks - Return an array of blocks that are connected to Bundle.
  ///
  /// \param Bundle Edge bundle whose connected basic block numbers are
  ///        returned.
  /// \return Basic block numbers connected to \p Bundle.
  ArrayRef<unsigned> getBlocks(unsigned Bundle) const { return Blocks[Bundle]; }

  /// getMachineFunction - Return the last machine function computed.
  ///
  /// \return The machine function most recently analyzed, or null if none.
  const MachineFunction *getMachineFunction() const { return MF; }

  /// view - Visualize the annotated bipartite CFG with Graphviz.
  LLVM_ABI void view() const;

  /// Invalidate this analysis result when required by the new pass manager.
  ///
  /// \param MF Machine function whose analysis result may be invalidated.
  /// \param PA Set of analyses preserved by the transform.
  /// \param Inv Invalidator for resolving analysis dependencies.
  /// \return True if this result should be discarded.
  LLVM_ABI bool invalidate(MachineFunction &MF, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);
};

/// Legacy MachineFunctionPass wrapper that computes and owns \c EdgeBundles.
class LLVM_ABI EdgeBundlesWrapperLegacy : public MachineFunctionPass {
public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the legacy EdgeBundles wrapper pass.
  EdgeBundlesWrapperLegacy() : MachineFunctionPass(ID) {}

  /// Return the EdgeBundles computed by this pass.
  ///
  /// \return The EdgeBundles owned by this pass.
  EdgeBundles &getEdgeBundles() { return *Impl; }
  /// Return the EdgeBundles computed by this pass.
  ///
  /// \return The EdgeBundles owned by this pass.
  const EdgeBundles &getEdgeBundles() const { return *Impl; }

private:
  std::unique_ptr<EdgeBundles> Impl;
  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage&) const override;
};

/// Analysis pass that computes \c EdgeBundles for a machine function.
class EdgeBundlesAnalysis : public AnalysisInfoMixin<EdgeBundlesAnalysis> {
  friend AnalysisInfoMixin<EdgeBundlesAnalysis>;
  static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = EdgeBundles;
  /// Compute edge bundles for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MFAM Analysis manager for the machine function.
  /// \return Edge bundles for \p MF.
  LLVM_ABI EdgeBundles run(MachineFunction &MF,
                           MachineFunctionAnalysisManager &MFAM);
};

} // end namespace llvm

#endif
