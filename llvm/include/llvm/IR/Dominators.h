//===- Dominators.h - Dominator Info Calculation ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the DominatorTree class, which provides fast and efficient
// dominance queries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DOMINATORS_H
#define LLVM_IR_DOMINATORS_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/ilist_iterator.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Use.h"
#include "llvm/Pass.h"
#include "llvm/Support/CFGDiff.h"
#include "llvm/Support/CFGUpdate.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/GenericDomTree.h"
#include <utility>

namespace llvm {

class Function;
class Instruction;
class Module;
class Value;
class raw_ostream;
template <class GraphType> struct GraphTraits;

/// Explicit instantiation of DomTreeNodeBase for BasicBlock.
extern template class LLVM_TEMPLATE_ABI DomTreeNodeBase<BasicBlock>;
extern template class LLVM_TEMPLATE_ABI
    DominatorTreeBase<BasicBlock, false>; // DomTree
extern template class LLVM_TEMPLATE_ABI
    DominatorTreeBase<BasicBlock, true>; // PostDomTree

extern template class cfg::Update<BasicBlock *>;

namespace DomTreeBuilder {
/// Dominator tree over a function CFG of basic blocks.
using BBDomTree = DomTreeBase<BasicBlock>;
/// Post-dominator tree over a function CFG of basic blocks.
using BBPostDomTree = PostDomTreeBase<BasicBlock>;

/// Batch of CFG updates applied to a basic-block dominator tree.
using BBUpdates = ArrayRef<llvm::cfg::Update<BasicBlock *>>;

/// GraphDiff view of a basic-block dominator tree CFG.
using BBDomTreeGraphDiff = GraphDiff<BasicBlock *, false>;
/// GraphDiff view of a basic-block post-dominator tree CFG.
using BBPostDomTreeGraphDiff = GraphDiff<BasicBlock *, true>;

}  // namespace DomTreeBuilder

/// Dominator tree node for a BasicBlock.
using DomTreeNode = DomTreeNodeBase<BasicBlock>;

/// Directed edge between two basic blocks in a CFG.
class BasicBlockEdge {
  const BasicBlock *Start;
  const BasicBlock *End;

public:
  /// Construct an edge from \p Start_ to \p End_.
  /// @param Start_ Source basic block.
  /// @param End_ Destination basic block.
  BasicBlockEdge(const BasicBlock *Start_, const BasicBlock *End_) :
    Start(Start_), End(End_) {}

  /// Construct an edge from a non-const basic-block pair.
  /// @param Pair Source and destination basic blocks.
  BasicBlockEdge(const std::pair<BasicBlock *, BasicBlock *> &Pair)
      : Start(Pair.first), End(Pair.second) {}

  /// Construct an edge from a const basic-block pair.
  /// @param Pair Source and destination basic blocks.
  BasicBlockEdge(const std::pair<const BasicBlock *, const BasicBlock *> &Pair)
      : Start(Pair.first), End(Pair.second) {}

  /// Return the source basic block of this edge.
  /// @return The source basic block of this edge.
  const BasicBlock *getStart() const {
    return Start;
  }

  /// Return the destination basic block of this edge.
  /// @return The destination basic block of this edge.
  const BasicBlock *getEnd() const { return End; }
};

/// DenseMapInfo specialization so BasicBlockEdge can be a DenseMap key.
template <> struct DenseMapInfo<BasicBlockEdge> {
  /// DenseMapInfo for the basic-block endpoints of an edge.
  using BBInfo = DenseMapInfo<const BasicBlock *>;

  /// Return a hash for the edge pointed to by \p V.
  /// @param V Pointer to the edge to hash.
  /// @return A hash of the edge pointed to by \p V.
  LLVM_ABI static unsigned getHashValue(const BasicBlockEdge *V);

  /// Return a hash for \p Edge.
  /// @param Edge Edge whose endpoints are hashed.
  /// @return A hash of \p Edge.
  static unsigned getHashValue(const BasicBlockEdge &Edge) {
    return hash_combine(BBInfo::getHashValue(Edge.getStart()),
                        BBInfo::getHashValue(Edge.getEnd()));
  }

  /// Return true if \p LHS and \p RHS have the same endpoints.
  /// @param LHS Left-hand edge.
  /// @param RHS Right-hand edge.
  /// @return True if \p LHS and \p RHS have the same endpoints.
  static bool isEqual(const BasicBlockEdge &LHS, const BasicBlockEdge &RHS) {
    return BBInfo::isEqual(LHS.getStart(), RHS.getStart()) &&
           BBInfo::isEqual(LHS.getEnd(), RHS.getEnd());
  }
};

/// Concrete subclass of DominatorTreeBase that is used to compute a
/// normal dominator tree.
///
/// Definition: A block is said to be forward statically reachable if there is
/// a path from the entry of the function to the block.  A statically reachable
/// block may become statically unreachable during optimization.
///
/// A forward unreachable block may appear in the dominator tree, or it may
/// not.  If it does, dominance queries will return results as if all reachable
/// blocks dominate it.  When asking for a Node corresponding to a potentially
/// unreachable block, calling code must handle the case where the block was
/// unreachable and the result of getNode() is nullptr.
///
/// Generally, a block known to be unreachable when the dominator tree is
/// constructed will not be in the tree.  One which becomes unreachable after
/// the dominator tree is initially constructed may still exist in the tree,
/// even if the tree is properly updated. Calling code should not rely on the
/// preceding statements; this is stated only to assist human understanding.
class DominatorTree : public DominatorTreeBase<BasicBlock, false> {
 public:
  /// Base dominator-tree type specialized for BasicBlock.
  using Base = DominatorTreeBase<BasicBlock, false>;

  /// Construct an empty dominator tree.
  DominatorTree() = default;
  /// Construct a dominator tree for \p F.
  /// @param F Function whose CFG is analyzed.
  explicit DominatorTree(Function &F) { recalculate(F); }
  /// Construct a dominator tree by applying CFG updates \p U to \p DT.
  /// @param DT Existing dominator tree to update from.
  /// @param U CFG updates to apply while recalculating.
  explicit DominatorTree(DominatorTree &DT, DomTreeBuilder::BBUpdates U) {
    recalculate(*DT.Parent, U);
  }

  /// Handle invalidation explicitly.
  /// @param F Function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

  /// Bring base-class dominates overloads into this class.
  using Base::dominates;

  /// Return true if the (end of the) basic block BB dominates the use U.
  /// @param BB Basic block that may dominate the use.
  /// @param U Use to test for dominance.
  /// @return True if \p BB dominates \p U.
  LLVM_ABI bool dominates(const BasicBlock *BB, const Use &U) const;

  /// Return true if value \p Def dominates use \p U.
  ///
  /// Def dominates U in the sense that Def is available at U, and could be
  /// substituted as the used value without violating the SSA dominance
  /// requirement.
  ///
  /// In particular, it is worth noting that:
  ///  * Non-instruction Defs dominate everything.
  ///  * Def does not dominate a use in Def itself (outside of degenerate cases
  ///    like unreachable code or trivial phi cycles).
  ///  * Invoke Defs only dominate uses in their default destination.
  /// @param Def Value that may dominate the use.
  /// @param U Use to test for dominance.
  /// @return True if \p Def dominates \p U.
  LLVM_ABI bool dominates(const Value *Def, const Use &U) const;

  /// Return true if value Def dominates all possible uses inside instruction
  /// User. Same comments as for the Use-based API apply.
  /// @param Def Value that may dominate the instruction.
  /// @param User Instruction whose uses are tested.
  /// @return True if \p Def dominates all uses in \p User.
  LLVM_ABI bool dominates(const Value *Def, const Instruction *User) const;
  /// Return true if \p Def dominates all possible uses of iterator \p User.
  /// @param Def Value that may dominate the instruction.
  /// @param User Iterator to the instruction whose uses are tested.
  /// @return True if \p Def dominates all uses of \p User.
  bool dominates(const Value *Def, BasicBlock::iterator User) const {
    return dominates(Def, &*User);
  }

  /// Returns true if Def would dominate a use in any instruction in BB.
  /// If Def is an instruction in BB, then Def does not dominate BB.
  ///
  /// Does not accept Value to avoid ambiguity with dominance checks between
  /// two basic blocks.
  /// @param Def Instruction that may dominate the block.
  /// @param BB Basic block to test for dominance.
  /// @return True if \p Def would dominate a use in any instruction in \p BB.
  LLVM_ABI bool dominates(const Instruction *Def, const BasicBlock *BB) const;

  /// Return true if an edge dominates a use.
  ///
  /// If BBE is not a unique edge between start and end of the edge, it can
  /// never dominate the use.
  /// @param BBE Edge that may dominate the use.
  /// @param U Use to test for dominance.
  /// @return True if \p BBE dominates \p U.
  LLVM_ABI bool dominates(const BasicBlockEdge &BBE, const Use &U) const;
  /// Return true if edge \p BBE dominates basic block \p BB.
  /// @param BBE Edge that may dominate the block.
  /// @param BB Basic block to test for dominance.
  /// @return True if \p BBE dominates \p BB.
  LLVM_ABI bool dominates(const BasicBlockEdge &BBE,
                          const BasicBlock *BB) const;
  /// Returns true if edge \p BBE1 dominates edge \p BBE2.
  /// @param BBE1 Edge that may dominate.
  /// @param BBE2 Edge to test for dominance.
  /// @return True if \p BBE1 dominates \p BBE2.
  LLVM_ABI bool dominates(const BasicBlockEdge &BBE1,
                          const BasicBlockEdge &BBE2) const;

  /// Bring base-class isReachableFromEntry overloads into this class.
  using Base::isReachableFromEntry;

  /// Provide an overload for a Use.
  /// @param U Use whose parent instruction reachability is tested.
  /// @return True if \p U is reachable from the function entry.
  LLVM_ABI bool isReachableFromEntry(const Use &U) const;

  /// Bring base-class findNearestCommonDominator overloads into this class.
  using Base::findNearestCommonDominator;

  /// Find the nearest instruction I that dominates both I1 and I2, in the sense
  /// that a result produced before I will be available at both I1 and I2.
  /// @param I1 First instruction.
  /// @param I2 Second instruction.
  /// @return The nearest common dominating instruction of \p I1 and \p I2.
  LLVM_ABI Instruction *findNearestCommonDominator(Instruction *I1,
                                                   Instruction *I2) const;

  /// Open a GraphViz window showing this dominator tree.
  /// @param Name Name used for the temporary graph file.
  /// @param Title Window title for the rendered graph.
  LLVM_ABI void viewGraph(const Twine &Name, const Twine &Title);
  /// Open a GraphViz window showing this dominator tree with a default name.
  LLVM_ABI void viewGraph();
};

//===-------------------------------------
// DominatorTree GraphTraits specializations so the DominatorTree can be
// iterable by generic graph iterators.

/// GraphTraits helpers for walking dominator-tree nodes depth-first.
template <class Node, class ChildIterator> struct DomTreeGraphTraitsBase {
  /// Graph node type for a dominator-tree node pointer.
  using NodeRef = Node *;
  /// Iterator over immediate children in the dominator tree.
  using ChildIteratorType = ChildIterator;
  /// Depth-first iterator over dominator-tree nodes.
  using nodes_iterator = df_iterator<Node *, df_iterator_default_set<Node*>>;

  /// Return \p N as the graph entry node.
  /// @param N Dominator-tree node used as the entry.
  /// @return \p N as the graph entry node.
  static NodeRef getEntryNode(NodeRef N) { return N; }
  /// Return the begin iterator over children of \p N.
  /// @param N Dominator-tree node whose children are walked.
  /// @return The begin iterator over children of \p N.
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  /// Return the end iterator over children of \p N.
  /// @param N Dominator-tree node whose children are walked.
  /// @return The end iterator over children of \p N.
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }

  /// Return a depth-first begin iterator rooted at \p N.
  /// @param N Dominator-tree node used as the DFS root.
  /// @return A depth-first begin iterator rooted at \p N.
  static nodes_iterator nodes_begin(NodeRef N) {
    return df_begin(getEntryNode(N));
  }

  /// Return a depth-first end iterator rooted at \p N.
  /// @param N Dominator-tree node used as the DFS root.
  /// @return A depth-first end iterator rooted at \p N.
  static nodes_iterator nodes_end(NodeRef N) { return df_end(getEntryNode(N)); }
};

/// GraphTraits specialization for mutable DomTreeNode pointers.
template <>
struct GraphTraits<DomTreeNode *>
    : public DomTreeGraphTraitsBase<DomTreeNode, DomTreeNode::const_iterator> {
};

/// GraphTraits specialization for const DomTreeNode pointers.
template <>
struct GraphTraits<const DomTreeNode *>
    : public DomTreeGraphTraitsBase<const DomTreeNode,
                                    DomTreeNode::const_iterator> {};

/// GraphTraits specialization so DominatorTree can be walked as a graph.
template <> struct GraphTraits<DominatorTree*>
  : public GraphTraits<DomTreeNode*> {
  /// Return the root node of dominator tree \p DT.
  /// @param DT Dominator tree whose root is the entry.
  /// @return The root node of \p DT.
  static NodeRef getEntryNode(DominatorTree *DT) { return DT->getRootNode(); }

  /// Return a depth-first begin iterator over nodes of \p N.
  /// @param N Dominator tree to walk.
  /// @return A depth-first begin iterator over nodes of \p N.
  static nodes_iterator nodes_begin(DominatorTree *N) {
    return df_begin(getEntryNode(N));
  }

  /// Return a depth-first end iterator over nodes of \p N.
  /// @param N Dominator tree to walk.
  /// @return A depth-first end iterator over nodes of \p N.
  static nodes_iterator nodes_end(DominatorTree *N) {
    return df_end(getEntryNode(N));
  }
};

/// Analysis pass which computes a \c DominatorTree.
class DominatorTreeAnalysis : public AnalysisInfoMixin<DominatorTreeAnalysis> {
  friend AnalysisInfoMixin<DominatorTreeAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  using Result = DominatorTree;

  /// Run the analysis pass over a function and produce a dominator tree.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager (unused).
  /// @return The computed dominator tree for \p F.
  LLVM_ABI DominatorTree run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c DominatorTree.
class DominatorTreePrinterPass
    : public RequiredPassInfoMixin<DominatorTreePrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed dominator tree.
  LLVM_ABI explicit DominatorTreePrinterPass(raw_ostream &OS);

  /// Print the DominatorTree for \p F and return all analyses preserved.
  /// @param F Function whose DominatorTree is printed.
  /// @param AM Function analysis manager providing DominatorTree.
  /// @return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Verifier pass for the \c DominatorTree.
struct DominatorTreeVerifierPass
    : RequiredPassInfoMixin<DominatorTreeVerifierPass> {
  /// Verify the DominatorTree for \p F and return all analyses preserved.
  /// @param F Function whose DominatorTree is verified.
  /// @param AM Function analysis manager providing DominatorTree.
  /// @return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Enables verification of dominator trees.
///
/// This check is expensive and is disabled by default.  `-verify-dom-info`
/// allows selectively enabling the check without needing to recompile.
LLVM_ABI extern bool VerifyDomInfo;

/// Legacy analysis pass which computes a \c DominatorTree.
class LLVM_ABI DominatorTreeWrapperPass : public FunctionPass {
  DominatorTree DT;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy DominatorTree wrapper pass.
  DominatorTreeWrapperPass();

  /// Return the DominatorTree computed by this pass.
  /// @return The DominatorTree computed by this pass.
  DominatorTree &getDomTree() { return DT; }
  /// Return the DominatorTree computed by this pass.
  /// @return The DominatorTree computed by this pass.
  const DominatorTree &getDomTree() const { return DT; }

  /// Compute the DominatorTree for \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnFunction(Function &F) override;

  /// Verify the DominatorTree computed by this pass.
  void verifyAnalysis() const override;

  /// Report analysis usage for this pass.
  /// @param AU Analysis usage to populate; this pass preserves all analyses.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  /// Release the DominatorTree owned by this pass.
  void releaseMemory() override { DT.reset(); }

  /// Print the DominatorTree computed by this pass.
  /// @param OS Output stream.
  /// @param M Optional module (unused).
  void print(raw_ostream &OS, const Module *M = nullptr) const override;
};
} // end namespace llvm

#endif // LLVM_IR_DOMINATORS_H
