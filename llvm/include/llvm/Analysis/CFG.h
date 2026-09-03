//===-- Analysis/CFG.h - BasicBlock Analyses --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions performs analyses on basic blocks, and instructions
// contained within basic blocks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CFG_H
#define LLVM_ANALYSIS_CFG_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Compiler.h"
#include <utility>

namespace llvm {

class BasicBlock;
class CycleInfo;
class DominatorTree;
class Function;
class Instruction;
class LoopInfo;
template <typename T> class SmallVectorImpl;

/// Find all loop backedges in the specified function.
///
/// This is a relatively cheap (compared to computing dominators and loop info)
/// analysis. The output is added to Result, as pairs of <from,to> edge info.
/// @param F Function whose backedges are collected.
/// @param Result Output vector of <from, to> backedge pairs.
LLVM_ABI void FindFunctionBackedges(
    const Function &F,
    SmallVectorImpl<std::pair<const BasicBlock *, const BasicBlock *>> &Result);

/// Return the successor index of \p Succ in \p BB's terminator.
///
/// Search for the specified successor of basic block BB and return its position
/// in the terminator instruction's list of successors. It is an error to call
/// this with a block that is not a successor.
/// @param BB Basic block whose terminator successors are searched.
/// @param Succ Successor basic block to locate.
/// @return Zero-based index of \p Succ among \p BB's terminator successors.
LLVM_ABI unsigned GetSuccessorNumber(const BasicBlock *BB,
                                     const BasicBlock *Succ);

/// Return true if the specified edge is a critical edge.
///
/// Critical edges are edges from a block with multiple successors to a block
/// with multiple predecessors.
/// @param TI Terminator instruction that defines the edge source.
/// @param SuccNum Zero-based successor index of the edge.
/// @param AllowIdenticalEdges If true, identical edges are not critical.
/// @return True if the edge is critical.
LLVM_ABI bool isCriticalEdge(const Instruction *TI, unsigned SuccNum,
                             bool AllowIdenticalEdges = false);

/// Return true if the edge from \p TI to \p Succ is a critical edge.
///
/// Critical edges are edges from a block with multiple successors to a block
/// with multiple predecessors.
/// @param TI Terminator instruction that defines the edge source.
/// @param Succ Successor basic block that defines the edge destination.
/// @param AllowIdenticalEdges If true, identical edges are not critical.
/// @return True if the edge is critical.
LLVM_ABI bool isCriticalEdge(const Instruction *TI, const BasicBlock *Succ,
                             bool AllowIdenticalEdges = false);

/// Determine whether instruction 'To' is reachable from 'From', without passing
/// through any blocks in ExclusionSet, returning true if uncertain.
///
/// Determine whether there is a path from From to To within a single function.
/// Returns false only if we can prove that once 'From' has been executed then
/// 'To' can not be executed. Conservatively returns true.
///
/// This function is linear with respect to the number of blocks in the CFG,
/// walking down successors from From to reach To, with a fixed threshold.
/// Using DT or LI allows us to answer more quickly. LI reduces the cost of
/// an entire loop of any number of blocks to be the same as the cost of a
/// single block. DT reduces the cost by allowing the search to terminate when
/// we find a block that dominates the block containing 'To'. DT is most useful
/// on branchy code but not loops, and LI is most useful on code with loops but
/// does not help on branchy code outside loops.
/// @param From Instruction that starts the reachability search.
/// @param To Instruction that is the reachability target.
/// @param ExclusionSet Optional set of blocks that may not be traversed.
/// @param DT Optional dominator tree used to prune the search.
/// @param LI Optional loop info used to prune the search.
/// @param CI Optional cycle info used to prune the search.
/// @return True if \p To may be reachable from \p From, or if reachability
/// cannot be disproved.
LLVM_ABI bool isPotentiallyReachable(
    const Instruction *From, const Instruction *To,
    const SmallPtrSetImpl<BasicBlock *> *ExclusionSet = nullptr,
    const DominatorTree *DT = nullptr, const LoopInfo *LI = nullptr,
    const CycleInfo *CI = nullptr);

/// Determine whether block 'To' is reachable from 'From', returning
/// true if uncertain.
///
/// Determine whether there is a path from From to To within a single function.
/// Returns false only if we can prove that once 'From' has been reached then
/// 'To' can not be executed. Conservatively returns true.
/// @param From Basic block that starts the reachability search.
/// @param To Basic block that is the reachability target.
/// @param ExclusionSet Optional set of blocks that may not be traversed.
/// @param DT Optional dominator tree used to prune the search.
/// @param LI Optional loop info used to prune the search.
/// @param CI Optional cycle info used to prune the search.
/// @return True if \p To may be reachable from \p From, or if reachability
/// cannot be disproved.
LLVM_ABI bool isPotentiallyReachable(
    const BasicBlock *From, const BasicBlock *To,
    const SmallPtrSetImpl<BasicBlock *> *ExclusionSet = nullptr,
    const DominatorTree *DT = nullptr, const LoopInfo *LI = nullptr,
    const CycleInfo *CI = nullptr);

/// Determine whether any block in \p Worklist can reach \p StopBB.
///
/// Determine whether there is a path from at least one block in Worklist to
/// StopBB within a single function without passing through any of the blocks
/// in 'ExclusionSet'. Returns false only if we can prove that once any block
/// in 'Worklist' has been reached then 'StopBB' can not be executed.
/// Conservatively returns true.
/// @param Worklist Source blocks from which reachability is explored.
/// @param StopBB Target block for the reachability query.
/// @param ExclusionSet Optional set of blocks that may not be traversed.
/// @param DT Optional dominator tree used to prune the search.
/// @param LI Optional loop info used to prune the search.
/// @param CI Optional cycle info used to prune the search.
/// @return True if \p StopBB may be reachable from any block in \p Worklist, or
/// if reachability cannot be disproved.
LLVM_ABI bool isPotentiallyReachableFromMany(
    SmallVectorImpl<BasicBlock *> &Worklist, const BasicBlock *StopBB,
    const SmallPtrSetImpl<BasicBlock *> *ExclusionSet,
    const DominatorTree *DT = nullptr, const LoopInfo *LI = nullptr,
    const CycleInfo *CI = nullptr);

/// Determine whether any block in \p Worklist can reach any block in \p StopSet.
///
/// Determine whether there is a potentially a path from at least one block in
/// 'Worklist' to at least one block in 'StopSet' within a single function
/// without passing through any of the blocks in 'ExclusionSet'. Returns false
/// only if we can prove that once any block in 'Worklist' has been reached then
/// no blocks in 'StopSet' can be executed without passing through any blocks in
/// 'ExclusionSet'. Conservatively returns true.
/// @param Worklist Source blocks from which reachability is explored.
/// @param StopSet Target blocks for the reachability query.
/// @param ExclusionSet Optional set of blocks that may not be traversed.
/// @param DT Optional dominator tree used to prune the search.
/// @param LI Optional loop info used to prune the search.
/// @param CI Optional cycle info used to prune the search.
/// @return True if any block in \p StopSet may be reachable from any block in
/// \p Worklist, or if reachability cannot be disproved.
LLVM_ABI bool isManyPotentiallyReachableFromMany(
    SmallVectorImpl<BasicBlock *> &Worklist,
    const SmallPtrSetImpl<const BasicBlock *> &StopSet,
    const SmallPtrSetImpl<BasicBlock *> *ExclusionSet,
    const DominatorTree *DT = nullptr, const LoopInfo *LI = nullptr,
    const CycleInfo *CI = nullptr);

/// Return true if the control flow in \p RPOTraversal is irreducible.
///
/// This is a generic implementation to detect CFG irreducibility based on loop
/// info analysis. It can be used for any kind of CFG (Loop, MachineLoop,
/// Function, MachineFunction, etc.) by providing an RPO traversal (\p
/// RPOTraversal) and the loop info analysis (\p LI) of the CFG. This utility
/// function is only recommended when loop info analysis is available. If loop
/// info analysis isn't available, please, don't compute it explicitly for this
/// purpose. There are more efficient ways to detect CFG irreducibility that
/// don't require recomputing loop info analysis (e.g., T1/T2 or Tarjan's
/// algorithm).
///
/// Requirements:
///   1) GraphTraits must be implemented for NodeT type. It is used to access
///      NodeT successors.
///   2) \p RPOTraversal must be a valid reverse post-order traversal of the
///      target CFG with begin()/end() iterator interfaces.
///   3) \p LI must be a valid LoopInfoBase that contains up-to-date loop
///      analysis information of the CFG.
///
/// This algorithm uses the information about reducible loop back-edges already
/// computed in \p LI. When a back-edge is found during the RPO traversal, the
/// algorithm checks whether the back-edge is one of the reducible back-edges in
/// loop info. If it isn't, the CFG is irreducible. For example, for the CFG
/// below (canonical irreducible graph) loop info won't contain any loop, so the
/// algorithm will return that the CFG is irreducible when checking the B <-
/// -> C back-edge.
///
/// \verbatim
/// (A->B, A->C, B->C, C->B, C->D)
///    A
///   / \
/// B<- ->C
///       |
///       D
/// \endverbatim
///
/// @param RPOTraversal Reverse post-order traversal of the CFG nodes.
/// @param LI Loop info for the CFG under analysis.
/// @return True if the CFG described by \p RPOTraversal is irreducible.
template <class NodeT, class RPOTraversalT, class LoopInfoT,
          class GT = GraphTraits<NodeT>>
bool containsIrreducibleCFG(RPOTraversalT &RPOTraversal, const LoopInfoT &LI) {
  /// Check whether the edge (\p Src, \p Dst) is a reducible loop backedge
  /// according to LI. I.e., check if there exists a loop that contains Src and
  /// where Dst is the loop header.
  auto isProperBackedge = [&](NodeT Src, NodeT Dst) {
    for (const auto *Lp = LI.getLoopFor(Src); Lp; Lp = Lp->getParentLoop()) {
      if (Lp->getHeader() == Dst)
        return true;
    }
    return false;
  };

  SmallPtrSet<NodeT, 32> Visited;
  for (NodeT Node : RPOTraversal) {
    Visited.insert(Node);
    for (NodeT Succ : make_range(GT::child_begin(Node), GT::child_end(Node))) {
      // Succ hasn't been visited yet
      if (!Visited.count(Succ))
        continue;
      // We already visited Succ, thus Node->Succ must be a backedge. Check that
      // the head matches what we have in the loop information. Otherwise, we
      // have an irreducible graph.
      if (!isProperBackedge(Node, Succ))
        return true;
    }
  }

  return false;
}

/// Return true if \p Src to \p Dest is a presplit coroutine suspend exit edge.
///
/// Returns true if these basic blocks belong to a presplit coroutine and the
/// edge corresponds to the 'default' case in the switch statement in the
/// pattern:
///
/// %0 = call i8 @llvm.coro.suspend(token none, i1 false)
/// switch i8 %0, label %suspend [i8 0, label %resume
///                               i8 1, label %cleanup]
///
/// i.e. the edge to the `%suspend` BB. This edge is special in that it will
/// be elided by coroutine lowering (coro-split), and the `%suspend` BB needs
/// to be kept as-is. It's not a real CFG edge - post-lowering, it will end
/// up being a `ret`, and it must be thus lowerable to support symmetric
/// transfer. For example:
///  - this edge is not a loop exit edge if encountered in a loop (and should
///    be ignored)
///  - must not be split for PGO instrumentation, for example.
/// @param Src Source basic block of the candidate edge.
/// @param Dest Destination basic block of the candidate edge.
/// @return True if the edge is a presplit coroutine suspend exit edge.
LLVM_ABI bool isPresplitCoroSuspendExitEdge(const BasicBlock &Src,
                                            const BasicBlock &Dest);

/// Return true if there is at least a path through which F can return, false if
/// there is no such path.
/// @param F Function to test for a feasible return path.
/// @return True if \p F has a feasible return path; false otherwise.
LLVM_ABI bool canReturn(const Function &F);
} // namespace llvm

#endif
