//===- llvm/Analysis/DominanceFrontier.h - Dominator Frontiers --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the DominanceFrontier class, which calculate and holds the
// dominance frontier for a function.
//
// CAUTION: For SSA-construction-like problems there are more efficient ways to
// do that, take a look at GenericIteratedDominanceFrontier.h/SSAUpdater.h. Also
// note that that this analysis computes dominance frontiers for *every* block
// which inherently increases complexity. Unless you do need *all* of them and
// *without* any modifications to the DomTree/CFG in between queries there
// should be better alternatives.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DOMINANCEFRONTIER_H
#define LLVM_ANALYSIS_DOMINANCEFRONTIER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/GenericDomTree.h"
#include <cassert>

namespace llvm {

class BasicBlock;
class Function;
class raw_ostream;

/// Common base class for computing forward and inverse dominance frontiers.
///
/// Holds the dominance frontier for every block in a function, for either
/// forward or post-dominance depending on \c IsPostDom.
template <class BlockT, bool IsPostDom>
class DominanceFrontierBase {
public:
  /// Set of blocks in the dominance frontier of a single block.
  ///
  /// Uses SetVector so iterating frontiers of a block is deterministic.
  using DomSetType = SetVector<BlockT *>;
  /// Map from each block to its dominance frontier set.
  using DomSetMapType = DenseMap<BlockT *, DomSetType>;
  /// Dominator tree type for \c BlockT with the same post-dom polarity.
  using DomTreeT = DominatorTreeBase<BlockT, IsPostDom>;
  /// Dominator tree node type for \c BlockT.
  using DomTreeNodeT = DomTreeNodeBase<BlockT>;

protected:
  /// Graph type used when walking CFG edges (inverted when post-dominating).
  using GraphTy = std::conditional_t<IsPostDom, Inverse<BlockT *>, BlockT *>;
  /// GraphTraits specialization for \c GraphTy.
  using BlockTraits = GraphTraits<GraphTy>;

  /// Dominance frontier set for each block in the function.
  DomSetMapType Frontiers;
  /// True when this instance computes post-dominance frontiers.
  static constexpr bool IsPostDominators = IsPostDom;

public:
  /// Construct an empty dominance frontier.
  DominanceFrontierBase() = default;

  /// Return true if analysis based of postdoms.
  /// @return True if this computes post-dominance frontiers.
  bool isPostDominator() const {
    return IsPostDominators;
  }

  /// Clear the computed dominance frontier sets.
  void releaseMemory() {
    Frontiers.clear();
  }

  /// Mutable iterator over the dominance frontier map.
  using iterator = typename DomSetMapType::iterator;
  /// Const iterator over the dominance frontier map.
  using const_iterator = typename DomSetMapType::const_iterator;

  /// Return an iterator to the first entry in the frontier map.
  /// @return An iterator to the first entry in the frontier map.
  iterator begin() { return Frontiers.begin(); }
  /// Return a const iterator to the first entry in the frontier map.
  /// @return A const iterator to the first entry in the frontier map.
  const_iterator begin() const { return Frontiers.begin(); }
  /// Return an iterator past the last entry in the frontier map.
  /// @return An iterator past the last entry in the frontier map.
  iterator end() { return Frontiers.end(); }
  /// Return a const iterator past the last entry in the frontier map.
  /// @return A const iterator past the last entry in the frontier map.
  const_iterator end() const { return Frontiers.end(); }
  /// Find the frontier entry for block \p B.
  /// @param B Block whose dominance frontier is requested.
  /// @return An iterator to the frontier entry for \p B, or end() if none.
  iterator find(BlockT *B) { return Frontiers.find(B); }
  /// Find the frontier entry for const block \p B.
  /// @param B Block whose dominance frontier is requested.
  /// @return A const iterator to the frontier entry for \p B, or end() if none.
  const_iterator find(const BlockT *B) const { return Frontiers.find(B); }

  /// Print the dominance frontiers in a human-readable form.
  /// @param OS Output stream to write to.
  void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump the dominance frontier to dbgs().
  void dump() const;
#endif

  /// Compute dominance frontiers for all blocks in \p DT.
  /// @param DT Dominator tree to analyze.
  void analyze(const DomTreeT &DT);
};

/// Dominance frontier analysis result for a Function's BasicBlocks.
class DominanceFrontier : public DominanceFrontierBase<BasicBlock, false> {
public:
  /// Dominator tree type specialized for BasicBlock.
  using DomTreeT = DomTreeBase<BasicBlock>;
  /// Dominator tree node type specialized for BasicBlock.
  using DomTreeNodeT = DomTreeNodeBase<BasicBlock>;
  /// Set of blocks in the dominance frontier of a single block.
  using DomSetType = DominanceFrontier::DomSetType;
  /// Mutable iterator over the dominance frontier map.
  using iterator = DominanceFrontier::iterator;
  /// Const iterator over the dominance frontier map.
  using const_iterator = DominanceFrontier::const_iterator;

  /// Handle invalidation explicitly.
  /// @param F Function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);
};

/// Legacy analysis pass which computes a \c DominanceFrontier.
class LLVM_ABI DominanceFrontierWrapperPass : public FunctionPass {
  DominanceFrontier DF;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy DominanceFrontier wrapper pass.
  DominanceFrontierWrapperPass();

  /// Return the DominanceFrontier computed by this pass.
  /// @return The DominanceFrontier computed by this pass.
  DominanceFrontier &getDominanceFrontier() { return DF; }
  /// Return the DominanceFrontier computed by this pass.
  /// @return The DominanceFrontier computed by this pass.
  const DominanceFrontier &getDominanceFrontier() const { return DF;  }

  /// Release the DominanceFrontier owned by this pass.
  void releaseMemory() override;

  /// Compute the DominanceFrontier for \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnFunction(Function &F) override;

  /// Report analysis usage for this pass.
  /// @param AU Analysis usage to populate with required and preserved analyses.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Print the DominanceFrontier computed by this pass.
  /// @param OS Output stream.
  /// @param M Optional module (unused).
  void print(raw_ostream &OS, const Module *M = nullptr) const override;

  /// Dump the DominanceFrontier to dbgs().
  void dump() const;
};

/// Explicit instantiation of DominanceFrontierBase for forward dominance.
extern template class LLVM_TEMPLATE_ABI
    DominanceFrontierBase<BasicBlock, false>;
/// Explicit instantiation of DominanceFrontierBase for post-dominance.
extern template class LLVM_TEMPLATE_ABI DominanceFrontierBase<BasicBlock, true>;

/// Analysis pass which computes a \c DominanceFrontier.
class DominanceFrontierAnalysis
    : public AnalysisInfoMixin<DominanceFrontierAnalysis> {
  friend AnalysisInfoMixin<DominanceFrontierAnalysis>;

  static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = DominanceFrontier;

  /// Run the analysis pass over a function and produce a dominance frontier.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing DominatorTree.
  /// @return The computed dominance frontier for \p F.
  LLVM_ABI DominanceFrontier run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c DominanceFrontier.
class DominanceFrontierPrinterPass
    : public RequiredPassInfoMixin<DominanceFrontierPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed dominance frontier.
  LLVM_ABI explicit DominanceFrontierPrinterPass(raw_ostream &OS);

  /// Print the DominanceFrontier for \p F and return all analyses preserved.
  /// @param F Function whose DominanceFrontier is printed.
  /// @param AM Function analysis manager providing DominanceFrontier.
  /// @return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_DOMINANCEFRONTIER_H
