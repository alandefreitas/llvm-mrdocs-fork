//=- llvm/Analysis/PostDominators.h - Post Dominator Calculation --*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes interfaces to post dominance information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_POSTDOMINATORS_H
#define LLVM_ANALYSIS_POSTDOMINATORS_H

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Function;
class raw_ostream;

/// PostDominatorTree Class - Concrete subclass of DominatorTree that is used to
/// compute the post-dominator tree.
class PostDominatorTree : public PostDomTreeBase<BasicBlock> {
public:
  /// Base post-dominator-tree type specialized for BasicBlock.
  using Base = PostDomTreeBase<BasicBlock>;

  /// Construct an empty post-dominator tree.
  PostDominatorTree() = default;
  /// Construct a post-dominator tree for \p F.
  /// @param F Function whose CFG is analyzed.
  explicit PostDominatorTree(Function &F) { recalculate(F); }
  /// Handle invalidation explicitly.
  /// @param F Function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

  /// Bring base-class dominates overloads into this class.
  using Base::dominates;

  /// Return true if \p I1 dominates \p I2.
  ///
  /// This checks if \p I2 comes before \p I1 if they belong to the same basic
  /// block.
  /// @param I1 Instruction that may dominate \p I2.
  /// @param I2 Instruction to test for dominance.
  /// @return True if \p I1 dominates \p I2.
  LLVM_ABI bool dominates(const Instruction *I1, const Instruction *I2) const;
};

/// Analysis pass which computes a \c PostDominatorTree.
class PostDominatorTreeAnalysis
    : public AnalysisInfoMixin<PostDominatorTreeAnalysis> {
  friend AnalysisInfoMixin<PostDominatorTreeAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = PostDominatorTree;

  /// Run the analysis pass over a function and produce a post dominator tree.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager (unused).
  /// @return The computed post-dominator tree for \p F.
  LLVM_ABI PostDominatorTree run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c PostDominatorTree.
class PostDominatorTreePrinterPass
    : public RequiredPassInfoMixin<PostDominatorTreePrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed post-dominator tree.
  LLVM_ABI explicit PostDominatorTreePrinterPass(raw_ostream &OS);

  /// Print the PostDominatorTree for \p F and return all analyses preserved.
  /// @param F Function whose PostDominatorTree is printed.
  /// @param AM Function analysis manager providing PostDominatorTree.
  /// @return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy analysis pass which computes a \c PostDominatorTree.
struct LLVM_ABI PostDominatorTreeWrapperPass : public FunctionPass {
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Post-dominator tree computed by this pass.
  PostDominatorTree DT;

  /// Construct the legacy PostDominatorTree wrapper pass.
  PostDominatorTreeWrapperPass();

  /// Return the PostDominatorTree computed by this pass.
  /// @return The PostDominatorTree computed by this pass.
  PostDominatorTree &getPostDomTree() { return DT; }
  /// Return the PostDominatorTree computed by this pass.
  /// @return The PostDominatorTree computed by this pass.
  const PostDominatorTree &getPostDomTree() const { return DT; }

  /// Compute the PostDominatorTree for \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnFunction(Function &F) override;

  /// Verify the PostDominatorTree computed by this pass.
  void verifyAnalysis() const override;

  /// Report analysis usage for this pass.
  /// @param AU Analysis usage to populate; this pass preserves all analyses.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  /// Release the PostDominatorTree owned by this pass.
  void releaseMemory() override { DT.reset(); }

  /// Print the PostDominatorTree computed by this pass.
  /// @param OS Output stream.
  /// @param M Optional module (unused).
  void print(raw_ostream &OS, const Module *M) const override;
};

/// Create a legacy FunctionPass that computes a PostDominatorTree.
/// @return A new FunctionPass that computes a PostDominatorTree.
LLVM_ABI FunctionPass *createPostDomTree();

/// GraphTraits specialization so PostDominatorTree can be walked as a graph.
template <> struct GraphTraits<PostDominatorTree*>
  : public GraphTraits<DomTreeNode*> {
  /// Return the root node of post-dominator tree \p DT.
  /// @param DT Post-dominator tree whose root is the entry.
  /// @return The root node of \p DT.
  static NodeRef getEntryNode(PostDominatorTree *DT) {
    return DT->getRootNode();
  }

  /// Return a depth-first begin iterator over nodes of \p N.
  /// @param N Post-dominator tree to walk.
  /// @return A depth-first begin iterator over nodes of \p N.
  static nodes_iterator nodes_begin(PostDominatorTree *N) {
    return df_begin(getEntryNode(N));
  }

  /// Return a depth-first end iterator over nodes of \p N.
  /// @param N Post-dominator tree to walk.
  /// @return A depth-first end iterator over nodes of \p N.
  static nodes_iterator nodes_end(PostDominatorTree *N) {
    return df_end(getEntryNode(N));
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_POSTDOMINATORS_H
