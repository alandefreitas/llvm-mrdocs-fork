//===- Transform/Utils/BasicBlockUtils.h - BasicBlock Utils -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform manipulations on basic blocks, and
// instructions contained within basic blocks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_BASICBLOCKUTILS_H
#define LLVM_TRANSFORMS_UTILS_BASICBLOCKUTILS_H

// FIXME: Move to this file: BasicBlock::removePredecessor, BB::splitBasicBlock

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Printable.h"
#include <cassert>

namespace llvm {
class CondBrInst;
class CycleInfo;
class LandingPadInst;
class Loop;
class PHINode;
template <typename PtrType> class SmallPtrSetImpl;
class BlockFrequencyInfo;
class BranchProbabilityInfo;
class DomTreeUpdater;
class Function;
class IRBuilderBase;
class LoopInfo;
class MDNode;
class MemoryDependenceResults;
class MemorySSAUpdater;
class PostDominatorTree;
class ReturnInst;
class TargetLibraryInfo;
class Value;

/// Check if the given basic block contains any loop or entry convergent
/// intrinsic instructions.
///
/// \param BB Basic block to inspect for convergent tokens.
/// \return True if \p BB contains a loop or entry convergent token.
LLVM_ABI bool HasLoopOrEntryConvergenceToken(const BasicBlock *BB);

/// Replace every block in \p BBs with a single unreachable instruction.
///
/// If \p Updates is specified, collect all necessary DT updates into this
/// vector. If \p KeepOneInputPHIs is true, one-input Phis in successors of
/// blocks being deleted will be preserved.
///
/// \param BBs Blocks whose contents are replaced with unreachable.
/// \param Updates Optional vector that receives dominator-tree updates.
/// \param KeepOneInputPHIs Whether to preserve one-input PHIs in successors.
LLVM_ABI void
detachDeadBlocks(ArrayRef<BasicBlock *> BBs,
                 SmallVectorImpl<DominatorTree::UpdateType> *Updates,
                 bool KeepOneInputPHIs = false);

/// Delete the specified block, which must have no predecessors.
///
/// \param BB Block to delete.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param KeepOneInputPHIs Whether to preserve one-input PHIs in successors.
LLVM_ABI void DeleteDeadBlock(BasicBlock *BB, DomTreeUpdater *DTU = nullptr,
                              bool KeepOneInputPHIs = false);

/// Delete the specified blocks, which must have no live predecessors.
///
/// The set of deleted blocks must have no predecessors that are not being
/// deleted themselves. \p BBs must have no duplicating blocks. If there are
/// loops among this set of blocks, all relevant loop info updates should be
/// done before this function is called. If \p KeepOneInputPHIs is true,
/// one-input Phis in successors of blocks being deleted will be preserved.
///
/// \param BBs Blocks to delete.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param KeepOneInputPHIs Whether to preserve one-input PHIs in successors.
LLVM_ABI void DeleteDeadBlocks(ArrayRef<BasicBlock *> BBs,
                               DomTreeUpdater *DTU = nullptr,
                               bool KeepOneInputPHIs = false);

/// Delete all basic blocks in \p F that are unreachable from its entry.
///
/// If \p KeepOneInputPHIs is true, one-input Phis in successors of blocks being
/// deleted will be preserved.
///
/// \param F Function whose unreachable blocks are deleted.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param KeepOneInputPHIs Whether to preserve one-input PHIs in successors.
/// \return True if any unreachable blocks were deleted.
LLVM_ABI bool EliminateUnreachableBlocks(Function &F,
                                         DomTreeUpdater *DTU = nullptr,
                                         bool KeepOneInputPHIs = false);

/// Fold away single-entry PHI nodes in a block with one predecessor.
///
/// We know that BB has one predecessor. If there are any single-entry PHI nodes
/// in it, fold them away. This handles the case when all entries to the PHI
/// nodes in a block are guaranteed equal, such as when the block has exactly
/// one predecessor.
///
/// \param BB Block whose single-entry PHIs may be folded.
/// \param MemDep Optional memory-dependence results to update.
/// \return True if any single-entry PHI nodes were folded away.
LLVM_ABI bool
FoldSingleEntryPHINodes(BasicBlock *BB,
                        MemoryDependenceResults *MemDep = nullptr);

/// Delete dead PHI nodes in a block and recursively dead operands.
///
/// Examine each PHI in the given block and delete it if it is dead. Also
/// recursively delete any operands that become dead as a result. This includes
/// tracing the def-use list from the PHI to see if it is ultimately unused or
/// if it reaches an unused cycle. Return true if any PHIs were deleted.
///
/// \param BB Block whose PHI nodes may be deleted.
/// \param TLI Optional target library info for deadness analysis.
/// \param MSSAU Optional MemorySSA updater notified of deletions.
/// \param KnownNonDeadPHIs Optional set of PHIs known not to be dead.
/// \return True if any PHI nodes were deleted.
LLVM_ABI bool
DeleteDeadPHIs(BasicBlock *BB, const TargetLibraryInfo *TLI = nullptr,
               MemorySSAUpdater *MSSAU = nullptr,
               SmallPtrSetImpl<PHINode *> *KnownNonDeadPHIs = nullptr);

/// Attempt to merge a block into its predecessor when possible.
///
/// The return value indicates success or failure. By default do not merge
/// blocks if BB's predecessor has multiple successors. If
/// PredecessorWithTwoSuccessors = true, the blocks can only be merged if BB's
/// Pred has a branch to BB and to AnotherBB, and BB has a single successor Sing.
/// In this case the branch will be updated with Sing instead of BB, and BB will
/// still be merged into its predecessor and removed. If \p DT is not nullptr,
/// update it directly; in that case, DTU must be nullptr.
///
/// \param BB Block to merge into its predecessor.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
/// \param MSSAU Optional MemorySSA updater for the merge.
/// \param MemDep Optional memory-dependence results to update.
/// \param PredecessorWithTwoSuccessors Allow merge when the predecessor has
///        exactly two successors as described above.
/// \param DT Optional dominator tree to update directly instead of via DTU.
/// \return True if \p BB was successfully merged into its predecessor.
LLVM_ABI bool MergeBlockIntoPredecessor(
    BasicBlock *BB, DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr,
    MemorySSAUpdater *MSSAU = nullptr,
    MemoryDependenceResults *MemDep = nullptr,
    bool PredecessorWithTwoSuccessors = false, DominatorTree *DT = nullptr);

/// Merge successors of the given blocks into them when possible.
///
/// Return true if at least two of the blocks were merged together. In order to
/// merge, each block must be terminated by an unconditional branch. If L is
/// provided, then the blocks merged into their predecessors must be in L. In
/// addition, This utility calls on another utility: MergeBlockIntoPredecessor.
/// Blocks are successfully merged when the call to MergeBlockIntoPredecessor
/// returns true.
///
/// \param MergeBlocks Set of blocks whose successors may be merged.
/// \param L Optional loop that candidate blocks must belong to.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
/// \return True if at least two of the blocks were merged together.
LLVM_ABI bool MergeBlockSuccessorsIntoGivenBlocks(
    SmallPtrSetImpl<BasicBlock *> &MergeBlocks, Loop *L = nullptr,
    DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr);

/// Try to remove redundant dbg.value instructions from a basic block.
///
/// Returns true if at least one instruction was removed. Remove redundant
/// pseudo ops when RemovePseudoOp is true.
///
/// \param BB Basic block whose dbg.value instructions may be pruned.
/// \return True if at least one instruction was removed.
LLVM_ABI bool RemoveRedundantDbgInstrs(BasicBlock *BB);

/// Replace all uses of an instruction with a value, then delete it.
///
/// \param BI Iterator referring to the instruction to replace and erase.
/// \param V Value that replaces all uses of the instruction.
LLVM_ABI void ReplaceInstWithValue(BasicBlock::iterator &BI, Value *V);

/// Replace one instruction with another in a basic block.
///
/// Copies DebugLoc from BI to I, if I doesn't already have a DebugLoc. The
/// original instruction is deleted and BI is updated to point to the new
/// instruction.
///
/// \param BB Basic block containing the instruction to replace.
/// \param BI Iterator referring to the instruction to replace.
/// \param I Replacement instruction to insert in its place.
LLVM_ABI void ReplaceInstWithInst(BasicBlock *BB, BasicBlock::iterator &BI,
                                  Instruction *I);

/// Replace one instruction with another and copy its debug location.
///
/// Copies DebugLoc from From to To, if To doesn't already have a DebugLoc.
///
/// \param From Instruction to replace and delete.
/// \param To Replacement instruction.
LLVM_ABI void ReplaceInstWithInst(Instruction *From, Instruction *To);

/// Check whether all paths from a block reach deopt or unreachable.
///
/// Check if we can prove that all paths starting from this block converge to a
/// block that either has a @llvm.experimental.deoptimize call prior to its
/// terminating return instruction or is terminated by unreachable. All blocks
/// in the traversed sequence must have an unique successor, maybe except for
/// the last one.
///
/// \param BB Block from which paths are examined.
/// \return True if all paths from \p BB reach deopt or unreachable.
LLVM_ABI bool IsBlockFollowedByDeoptOrUnreachable(const BasicBlock *BB);

/// Option class for critical edge splitting.
///
/// This provides a builder interface for overriding the default options used
/// during critical edge splitting.
struct CriticalEdgeSplittingOptions {
  /// Dominator tree to update when splitting edges.
  DominatorTree *DT;
  /// Post-dominator tree to update when splitting edges.
  PostDominatorTree *PDT;
  /// Loop info to update when splitting edges.
  LoopInfo *LI;
  /// MemorySSA updater notified of CFG changes.
  MemorySSAUpdater *MSSAU;
  /// Merge all identical edges to the same successor into one split block.
  bool MergeIdenticalEdges = false;
  /// Preserve one-input PHIs instead of folding them away.
  bool KeepOneInputPHIs = false;
  /// Require that LCSSA form is preserved by the split.
  bool PreserveLCSSA = false;
  /// Skip critical edges whose destination is unreachable.
  bool IgnoreUnreachableDests = false;
  /// Require preserving loop-simplify form when loop info is available.
  ///
  /// SplitCriticalEdge is guaranteed to preserve loop-simplify form if LI is
  /// provided. If it cannot be preserved, no splitting will take place. If it
  /// is not set, preserve loop-simplify form if possible.
  bool PreserveLoopSimplify = true;

  /// Construct options with the analyses to update during edge splitting.
  ///
  /// \param DT Optional dominator tree to update.
  /// \param LI Optional loop info to update.
  /// \param MSSAU Optional MemorySSA updater to notify.
  /// \param PDT Optional post-dominator tree to update.
  CriticalEdgeSplittingOptions(DominatorTree *DT = nullptr,
                               LoopInfo *LI = nullptr,
                               MemorySSAUpdater *MSSAU = nullptr,
                               PostDominatorTree *PDT = nullptr)
      : DT(DT), PDT(PDT), LI(LI), MSSAU(MSSAU) {}

  /// Enable merging identical edges to the same successor.
  ///
  /// \return Reference to this options object for chaining.
  CriticalEdgeSplittingOptions &setMergeIdenticalEdges() {
    MergeIdenticalEdges = true;
    return *this;
  }

  /// Enable preserving one-input PHI nodes.
  ///
  /// \return Reference to this options object for chaining.
  CriticalEdgeSplittingOptions &setKeepOneInputPHIs() {
    KeepOneInputPHIs = true;
    return *this;
  }

  /// Require that LCSSA form is preserved.
  ///
  /// \return Reference to this options object for chaining.
  CriticalEdgeSplittingOptions &setPreserveLCSSA() {
    PreserveLCSSA = true;
    return *this;
  }

  /// Ignore critical edges that lead to unreachable destinations.
  ///
  /// \return Reference to this options object for chaining.
  CriticalEdgeSplittingOptions &setIgnoreUnreachableDests() {
    IgnoreUnreachableDests = true;
    return *this;
  }

  /// Allow splitting even when loop-simplify form cannot be preserved.
  ///
  /// \return Reference to this options object for chaining.
  CriticalEdgeSplittingOptions &unsetPreserveLoopSimplify() {
    PreserveLoopSimplify = false;
    return *this;
  }
};

/// Insert LCSSA PHI nodes required after splitting a loop exit edge.
///
/// When a loop exit edge is split, LCSSA form may require new PHIs in the new
/// exit block. This function inserts the new PHIs, as needed. Preds is a list
/// of preds inside the loop, SplitBB is the new loop exit block, and DestBB is
/// the old loop exit, now the successor of SplitBB.
///
/// \param Preds Predecessors inside the loop of the split exit edge.
/// \param SplitBB Newly created loop exit block.
/// \param DestBB Original loop exit, now successor of \p SplitBB.
LLVM_ABI void createPHIsForSplitLoopExit(ArrayRef<BasicBlock *> Preds,
                                         BasicBlock *SplitBB,
                                         BasicBlock *DestBB);

/// Split a critical edge from a terminator to one of its successors.
///
/// If this edge is a critical edge, insert a new node to split the critical
/// edge. This will update the analyses passed in through the option struct.
/// This returns the new block if the edge was split, null otherwise.
///
/// If MergeIdenticalEdges in the options struct is true (not the default),
/// *all* edges from TI to the specified successor will be merged into the same
/// critical edge block. This is most commonly interesting with switch
/// instructions, which may have many edges to any one destination.  This
/// ensures that all edges to that dest go to one block instead of each going
/// to a different block, but isn't the standard definition of a "critical
/// edge".
///
/// It is invalid to call this function on a critical edge that starts at an
/// IndirectBrInst.  Splitting these edges will almost always create an invalid
/// program because the address of the new block won't be the one that is jumped
/// to.
///
/// \param TI Terminator whose outgoing edge may be split.
/// \param SuccNum Index of the successor edge to split.
/// \param Options Analyses and policy controlling the split.
/// \param BBName Optional name for the newly created block.
/// \return The new block if the edge was split, or null otherwise.
LLVM_ABI BasicBlock *
SplitCriticalEdge(Instruction *TI, unsigned SuccNum,
                  const CriticalEdgeSplittingOptions &Options =
                      CriticalEdgeSplittingOptions(),
                  const Twine &BBName = "");

/// Split an edge that is already known to be critical.
///
/// If it is known that an edge is critical, SplitKnownCriticalEdge can be
/// called directly, rather than calling SplitCriticalEdge first.
///
/// \param TI Terminator whose outgoing critical edge is split.
/// \param SuccNum Index of the successor edge to split.
/// \param Options Analyses and policy controlling the split.
/// \param BBName Optional name for the newly created block.
/// \return The newly created block that splits the critical edge.
LLVM_ABI BasicBlock *
SplitKnownCriticalEdge(Instruction *TI, unsigned SuccNum,
                       const CriticalEdgeSplittingOptions &Options =
                           CriticalEdgeSplittingOptions(),
                       const Twine &BBName = "");

/// Split the critical edge between two basic blocks if one exists.
///
/// If an edge from Src to Dst is critical, split the edge and return the new
/// block, otherwise return null. This method requires that there be an edge
/// between the two blocks. It updates the analyses passed in the options
/// struct.
///
/// \param Src Source block of the edge.
/// \param Dst Destination block of the edge.
/// \param Options Analyses and policy controlling the split.
/// \return The new block if the edge was split, or null otherwise.
inline BasicBlock *
SplitCriticalEdge(BasicBlock *Src, BasicBlock *Dst,
                  const CriticalEdgeSplittingOptions &Options =
                      CriticalEdgeSplittingOptions()) {
  Instruction *TI = Src->getTerminator();
  unsigned i = 0;
  while (true) {
    assert(i != TI->getNumSuccessors() && "Edge doesn't exist!");
    if (TI->getSuccessor(i) == Dst)
      return SplitCriticalEdge(TI, i, Options);
    ++i;
  }
}

/// Split every critical edge in a function.
///
/// Loop over all of the edges in the CFG, breaking critical edges as they are
/// found. Returns the number of broken edges.
///
/// \param F Function whose critical edges are split.
/// \param Options Analyses and policy controlling each split.
/// \return The number of critical edges that were split.
LLVM_ABI unsigned
SplitAllCriticalEdges(Function &F, const CriticalEdgeSplittingOptions &Options =
                                       CriticalEdgeSplittingOptions());

/// Split the edge connecting two blocks and return the new block.
///
/// \param From Source block of the edge.
/// \param To Destination block of the edge.
/// \param DT Optional dominator tree to update.
/// \param LI Optional loop info to update.
/// \param MSSAU Optional MemorySSA updater for the split.
/// \param BBName Optional name for the newly created block.
/// \return The newly created intermediate block on the edge.
LLVM_ABI BasicBlock *SplitEdge(BasicBlock *From, BasicBlock *To,
                               DominatorTree *DT = nullptr,
                               LoopInfo *LI = nullptr,
                               MemorySSAUpdater *MSSAU = nullptr,
                               const Twine &BBName = "");

/// \brief Create a new intermediate target block for a callbr or switch edge.
///
/// Create a new basic block between a callbr or switch instruction and one of
/// its successors. The new block replaces the original successor in the
/// callbr/switch instruction and unconditionally branches to the original
/// successor. This is useful for normalizing control flow, e.g., when
/// transforming irreducible loops.
///
/// \param MultiBrBlock   block containing the callbr or switch instruction
/// \param Succ           original successor block
/// \param SuccIdx        index of the original successor in the terminator
///                       instruction
/// \param BrTarget       optional \p BasicBlock generated by \c
/// SplitMultiBrEdge
///                       to reuse for the split
/// \param DTU            optional \p DomTreeUpdater for updating the
///                       dominator tree
/// \param CI             optional \p CycleInfo for updating cycle membership
/// \param LI             optional \p LoopInfo for updating loop membership
/// \param UpdatedLI      optional output flag indicating if \p LoopInfo has
///                       been updated
///
/// \returns newly created intermediate target block
///
/// \note This function updates PHI nodes, dominator tree, loop info, and
/// cycle info as needed.
LLVM_ABI BasicBlock *
SplitMultiBrEdge(BasicBlock *MultiBrBlock, BasicBlock *Succ, unsigned SuccIdx,
                 BasicBlock *BrTarget = nullptr, DomTreeUpdater *DTU = nullptr,
                 CycleInfo *CI = nullptr, LoopInfo *LI = nullptr,
                 bool *UpdatedLI = nullptr);

/// Sets the unwind edge of an instruction to a particular successor.
///
/// \param TI Instruction whose unwind successor is updated.
/// \param Succ New unwind successor block.
LLVM_ABI void setUnwindEdgeTo(Instruction *TI, BasicBlock *Succ);

/// Replace OldPred with NewPred in PHI nodes of a destination block.
///
/// \param DestBB Block whose PHI nodes are updated.
/// \param OldPred Predecessor edge being replaced.
/// \param NewPred Predecessor edge that replaces \p OldPred.
/// \param Until Optional PHI at which to stop updating; later PHIs are skipped.
LLVM_ABI void updatePhiNodes(BasicBlock *DestBB, BasicBlock *OldPred,
                             BasicBlock *NewPred, PHINode *Until = nullptr);

/// Split an edge when the successor is an exception-handling block.
///
/// \param BB Source block of the edge.
/// \param Succ Exception-handling successor block.
/// \param OriginalPad Optional original landing pad associated with the edge.
/// \param LandingPadReplacement Optional PHI that replaces the landing pad.
/// \param Options Analyses and policy controlling the split.
/// \param BBName Optional name for the newly created block.
/// \return The newly created block that splits the EH edge.
LLVM_ABI BasicBlock *
ehAwareSplitEdge(BasicBlock *BB, BasicBlock *Succ,
                 LandingPadInst *OriginalPad = nullptr,
                 PHINode *LandingPadReplacement = nullptr,
                 const CriticalEdgeSplittingOptions &Options =
                     CriticalEdgeSplittingOptions(),
                 const Twine &BBName = "");

/// Split a block so instructions from \p SplitPt move to a new successor.
///
/// Everything before \p SplitPt stays in \p Old and everything starting with \p
/// SplitPt moves to a new block. The two blocks are joined by an unconditional
/// branch. The new block with name \p BBName is returned.
///
/// FIXME: deprecated, switch to the DomTreeUpdater-based one.
///
/// \param Old Block to split.
/// \param SplitPt First instruction moved into the new block.
/// \param DT Dominator tree to update.
/// \param LI Optional loop info to update.
/// \param MSSAU Optional MemorySSA updater for the split.
/// \param BBName Optional name for the newly created block.
/// \return The newly created successor block containing instructions from
///         \p SplitPt onward.
LLVM_ABI BasicBlock *SplitBlock(BasicBlock *Old, BasicBlock::iterator SplitPt,
                                DominatorTree *DT, LoopInfo *LI = nullptr,
                                MemorySSAUpdater *MSSAU = nullptr,
                                const Twine &BBName = "");

/// Split a block at an instruction using a dominator tree.
///
/// \param Old Block to split.
/// \param SplitPt First instruction moved into the new block.
/// \param DT Dominator tree to update.
/// \param LI Optional loop info to update.
/// \param MSSAU Optional MemorySSA updater for the split.
/// \param BBName Optional name for the newly created block.
/// \return The newly created successor block containing instructions from
///         \p SplitPt onward.
inline BasicBlock *SplitBlock(BasicBlock *Old, Instruction *SplitPt,
                              DominatorTree *DT, LoopInfo *LI = nullptr,
                              MemorySSAUpdater *MSSAU = nullptr,
                              const Twine &BBName = "") {
  return SplitBlock(Old, SplitPt->getIterator(), DT, LI, MSSAU, BBName);
}

/// Split a block so instructions from \p SplitPt move to a new successor.
///
/// Everything before \p SplitPt stays in \p Old and everything starting with \p
/// SplitPt moves to a new block. The two blocks are joined by an unconditional
/// branch. The new block with name \p BBName is returned.
///
/// \param Old Block to split.
/// \param SplitPt First instruction moved into the new block.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
/// \param MSSAU Optional MemorySSA updater for the split.
/// \param BBName Optional name for the newly created block.
/// \return The newly created successor block containing instructions from
///         \p SplitPt onward.
LLVM_ABI BasicBlock *SplitBlock(BasicBlock *Old, BasicBlock::iterator SplitPt,
                                DomTreeUpdater *DTU = nullptr,
                                LoopInfo *LI = nullptr,
                                MemorySSAUpdater *MSSAU = nullptr,
                                const Twine &BBName = "");

/// Split a block at an instruction using a dominator-tree updater.
///
/// \param Old Block to split.
/// \param SplitPt First instruction moved into the new block.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
/// \param MSSAU Optional MemorySSA updater for the split.
/// \param BBName Optional name for the newly created block.
/// \return The newly created successor block containing instructions from
///         \p SplitPt onward.
inline BasicBlock *SplitBlock(BasicBlock *Old, Instruction *SplitPt,
                              DomTreeUpdater *DTU = nullptr,
                              LoopInfo *LI = nullptr,
                              MemorySSAUpdater *MSSAU = nullptr,
                              const Twine &BBName = "") {
  return SplitBlock(Old, SplitPt->getIterator(), DTU, LI, MSSAU, BBName);
}

/// Split a block so instructions before \p SplitPt move to a new predecessor.
///
/// All instructions before \p SplitPt are moved to a new block and all
/// instructions after \p SplitPt stay in the old block. The new block and the
/// old block are joined by inserting an unconditional branch to the end of the
/// new block. The new block with name \p BBName is returned.
///
/// \param Old Block to split.
/// \param SplitPt First instruction that remains in the old block.
/// \param DTU Dominator-tree updater for CFG changes.
/// \param LI Loop info to update.
/// \param MSSAU MemorySSA updater for the split.
/// \param BBName Optional name for the newly created block.
/// \return The newly created predecessor block containing instructions before
///         \p SplitPt.
LLVM_ABI BasicBlock *splitBlockBefore(BasicBlock *Old,
                                      BasicBlock::iterator SplitPt,
                                      DomTreeUpdater *DTU, LoopInfo *LI,
                                      MemorySSAUpdater *MSSAU,
                                      const Twine &BBName = "");

/// Split a block before an instruction using a dominator-tree updater.
///
/// \param Old Block to split.
/// \param SplitPt First instruction that remains in the old block.
/// \param DTU Dominator-tree updater for CFG changes.
/// \param LI Loop info to update.
/// \param MSSAU MemorySSA updater for the split.
/// \param BBName Optional name for the newly created block.
/// \return The newly created predecessor block containing instructions before
///         \p SplitPt.
inline BasicBlock *splitBlockBefore(BasicBlock *Old, Instruction *SplitPt,
                             DomTreeUpdater *DTU, LoopInfo *LI,
                             MemorySSAUpdater *MSSAU, const Twine &BBName = "") {
  return splitBlockBefore(Old, SplitPt->getIterator(), DTU, LI, MSSAU, BBName);
}

/// Redirect selected predecessors of a block through a new intermediate block.
///
/// This method introduces at least one new basic block into the function and
/// moves some of the predecessors of BB to be predecessors of the new block.
/// The new predecessors are indicated by the Preds array. The new block is
/// given a suffix of 'Suffix'. Returns new basic block to which predecessors
/// from Preds are now pointing.
///
/// If BB is a landingpad block then additional basicblock might be introduced.
/// It will have Suffix+".split_lp". See SplitLandingPadPredecessors for more
/// details on this case.
///
/// This currently updates the LLVM IR, DominatorTree, LoopInfo, and LCCSA but
/// no other analyses. In particular, it does not preserve LoopSimplify
/// (because it's complicated to handle the case where one of the edges being
/// split is an exit of a loop with other exits).
///
/// FIXME: deprecated, switch to the DomTreeUpdater-based one.
///
/// \param BB Block whose predecessors are redirected.
/// \param Preds Predecessors moved to the new intermediate block.
/// \param Suffix Name suffix for the newly created block.
/// \param DT Dominator tree to update.
/// \param LI Optional loop info to update.
/// \param MSSAU Optional MemorySSA updater for the split.
/// \param PreserveLCSSA Whether to preserve LCSSA form.
/// \return The new intermediate block to which predecessors from \p Preds now
///         point.
LLVM_ABI BasicBlock *SplitBlockPredecessors(
    BasicBlock *BB, ArrayRef<BasicBlock *> Preds, const char *Suffix,
    DominatorTree *DT, LoopInfo *LI = nullptr,
    MemorySSAUpdater *MSSAU = nullptr, bool PreserveLCSSA = false);

/// Redirect selected predecessors of a block through a new intermediate block.
///
/// This method introduces at least one new basic block into the function and
/// moves some of the predecessors of BB to be predecessors of the new block.
/// The new predecessors are indicated by the Preds array. The new block is
/// given a suffix of 'Suffix'. Returns new basic block to which predecessors
/// from Preds are now pointing.
///
/// If BB is a landingpad block then additional basicblock might be introduced.
/// It will have Suffix+".split_lp". See SplitLandingPadPredecessors for more
/// details on this case.
///
/// This currently updates the LLVM IR, DominatorTree, LoopInfo, and LCCSA but
/// no other analyses. In particular, it does not preserve LoopSimplify
/// (because it's complicated to handle the case where one of the edges being
/// split is an exit of a loop with other exits).
///
/// \param BB Block whose predecessors are redirected.
/// \param Preds Predecessors moved to the new intermediate block.
/// \param Suffix Name suffix for the newly created block.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
/// \param MSSAU Optional MemorySSA updater for the split.
/// \param PreserveLCSSA Whether to preserve LCSSA form.
/// \return The new intermediate block to which predecessors from \p Preds now
///         point.
LLVM_ABI BasicBlock *SplitBlockPredecessors(
    BasicBlock *BB, ArrayRef<BasicBlock *> Preds, const char *Suffix,
    DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr,
    MemorySSAUpdater *MSSAU = nullptr, bool PreserveLCSSA = false);

/// Split a landing-pad block's predecessors into two new landing-pad clones.
///
/// This method transforms the landing pad, OrigBB, by introducing two new basic
/// blocks into the function. One of those new basic blocks gets the
/// predecessors listed in Preds. The other basic block gets the remaining
/// predecessors of OrigBB. The landingpad instruction OrigBB is clone into both
/// of the new basic blocks. The new blocks are given the suffixes 'Suffix1' and
/// 'Suffix2', and are returned in the NewBBs vector.
///
/// This currently updates the LLVM IR, DominatorTree, LoopInfo, and LCCSA but
/// no other analyses. In particular, it does not preserve LoopSimplify
/// (because it's complicated to handle the case where one of the edges being
/// split is an exit of a loop with other exits).
///
/// \param OrigBB Original landing-pad block being split.
/// \param Preds Predecessors moved to the first new landing-pad block.
/// \param Suffix Name suffix for the first newly created block.
/// \param Suffix2 Name suffix for the second newly created block.
/// \param NewBBs Output vector filled with the two new blocks.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
/// \param MSSAU Optional MemorySSA updater for the split.
/// \param PreserveLCSSA Whether to preserve LCSSA form.
LLVM_ABI void SplitLandingPadPredecessors(
    BasicBlock *OrigBB, ArrayRef<BasicBlock *> Preds, const char *Suffix,
    const char *Suffix2, SmallVectorImpl<BasicBlock *> &NewBBs,
    DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr,
    MemorySSAUpdater *MSSAU = nullptr, bool PreserveLCSSA = false);

/// Fold a return into a predecessor that ends with an unconditional branch.
///
/// This method duplicates the specified return instruction into a predecessor
/// which ends in an unconditional branch. If the return instruction returns a
/// value defined by a PHI, propagate the right value into the return. It
/// returns the new return instruction in the predecessor.
///
/// \param RI Return instruction being folded.
/// \param BB Block containing \p RI.
/// \param Pred Predecessor ending in an unconditional branch to \p BB.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \return The new return instruction inserted into the predecessor.
LLVM_ABI ReturnInst *FoldReturnIntoUncondBranch(ReturnInst *RI, BasicBlock *BB,
                                                BasicBlock *Pred,
                                                DomTreeUpdater *DTU = nullptr);

/// Split a block and insert a conditional then-branch around the split point.
///
/// Split the containing block at the specified instruction - everything before
/// SplitBefore stays in the old basic block, and the rest of the instructions
/// in the BB are moved to a new block. The two blocks are connected by a
/// conditional branch (with value of Cmp being the condition).
/// Before:
///   Head
///   SplitBefore
///   Tail
/// After:
///   Head
///   if (Cond)
///     ThenBlock
///   SplitBefore
///   Tail
///
/// If \p ThenBlock is not specified, a new block will be created for it.
/// If \p Unreachable is true, the newly created block will end with
/// UnreachableInst, otherwise it branches to Tail.
/// Returns the NewBasicBlock's terminator.
///
/// Updates DTU and LI if given.
///
/// \param Cond Condition controlling the inserted branch.
/// \param SplitBefore Instruction at which the block is split.
/// \param Unreachable Whether the then-block ends with unreachable.
/// \param BranchWeights Optional branch-weight metadata for the new branch.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
/// \param ThenBlock Optional existing block to use as the then-block.
/// \return The terminator of the then-block.
LLVM_ABI Instruction *
SplitBlockAndInsertIfThen(Value *Cond, BasicBlock::iterator SplitBefore,
                          bool Unreachable, MDNode *BranchWeights = nullptr,
                          DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr,
                          BasicBlock *ThenBlock = nullptr);

/// Split a block and insert a then-branch at an instruction.
///
/// \param Cond Condition controlling the inserted branch.
/// \param SplitBefore Instruction at which the block is split.
/// \param Unreachable Whether the then-block ends with unreachable.
/// \param BranchWeights Optional branch-weight metadata for the new branch.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
/// \param ThenBlock Optional existing block to use as the then-block.
/// \return The terminator of the then-block.
inline Instruction *SplitBlockAndInsertIfThen(Value *Cond, Instruction *SplitBefore,
                                       bool Unreachable,
                                       MDNode *BranchWeights = nullptr,
                                       DomTreeUpdater *DTU = nullptr,
                                       LoopInfo *LI = nullptr,
                                       BasicBlock *ThenBlock = nullptr) {
  return SplitBlockAndInsertIfThen(Cond, SplitBefore->getIterator(),
                                   Unreachable, BranchWeights, DTU, LI,
                                   ThenBlock);
}

/// Split a block and insert a conditional else-branch around the split point.
///
/// Similar to SplitBlockAndInsertIfThen, but the inserted block is on the false
/// path of the branch.
///
/// \param Cond Condition controlling the inserted branch.
/// \param SplitBefore Instruction at which the block is split.
/// \param Unreachable Whether the else-block ends with unreachable.
/// \param BranchWeights Optional branch-weight metadata for the new branch.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
/// \param ElseBlock Optional existing block to use as the else-block.
/// \return The terminator of the else-block.
LLVM_ABI Instruction *
SplitBlockAndInsertIfElse(Value *Cond, BasicBlock::iterator SplitBefore,
                          bool Unreachable, MDNode *BranchWeights = nullptr,
                          DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr,
                          BasicBlock *ElseBlock = nullptr);

/// Split a block and insert an else-branch at an instruction.
///
/// \param Cond Condition controlling the inserted branch.
/// \param SplitBefore Instruction at which the block is split.
/// \param Unreachable Whether the else-block ends with unreachable.
/// \param BranchWeights Optional branch-weight metadata for the new branch.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
/// \param ElseBlock Optional existing block to use as the else-block.
/// \return The terminator of the else-block.
inline Instruction *SplitBlockAndInsertIfElse(Value *Cond, Instruction *SplitBefore,
                                       bool Unreachable,
                                       MDNode *BranchWeights = nullptr,
                                       DomTreeUpdater *DTU = nullptr,
                                       LoopInfo *LI = nullptr,
                                       BasicBlock *ElseBlock = nullptr) {
  return SplitBlockAndInsertIfElse(Cond, SplitBefore->getIterator(),
                                   Unreachable, BranchWeights, DTU, LI,
                                   ElseBlock);
}

/// Split a block and insert both then and else branches around the split point.
///
/// SplitBlockAndInsertIfThenElse is similar to SplitBlockAndInsertIfThen,
/// but also creates the ElseBlock.
/// Before:
///   Head
///   SplitBefore
///   Tail
/// After:
///   Head
///   if (Cond)
///     ThenBlock
///   else
///     ElseBlock
///   SplitBefore
///   Tail
///
/// Updates DT if given.
///
/// \param Cond Condition controlling the inserted branch.
/// \param SplitBefore Instruction at which the block is split.
/// \param ThenTerm Set to the terminator of the then-block.
/// \param ElseTerm Set to the terminator of the else-block.
/// \param BranchWeights Optional branch-weight metadata for the new branch.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
LLVM_ABI void SplitBlockAndInsertIfThenElse(
    Value *Cond, BasicBlock::iterator SplitBefore, Instruction **ThenTerm,
    Instruction **ElseTerm, MDNode *BranchWeights = nullptr,
    DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr);

/// Split a block and insert then/else branches at an instruction.
///
/// \param Cond Condition controlling the inserted branch.
/// \param SplitBefore Instruction at which the block is split.
/// \param ThenTerm Set to the terminator of the then-block.
/// \param ElseTerm Set to the terminator of the else-block.
/// \param BranchWeights Optional branch-weight metadata for the new branch.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
inline void SplitBlockAndInsertIfThenElse(Value *Cond, Instruction *SplitBefore,
                                   Instruction **ThenTerm,
                                   Instruction **ElseTerm,
                                   MDNode *BranchWeights = nullptr,
                                   DomTreeUpdater *DTU = nullptr,
                                   LoopInfo *LI = nullptr)
{
  SplitBlockAndInsertIfThenElse(Cond, SplitBefore->getIterator(), ThenTerm,
                               ElseTerm, BranchWeights, DTU, LI);
}

/// Split a block and insert optional then/else blocks around the split point.
///
/// Split the containing block at the specified instruction - everything before
/// SplitBefore stays in the old basic block, and the rest of the instructions
/// in the BB are moved to a new block. The two blocks are connected by a
/// conditional branch (with value of Cmp being the condition).
/// Before:
///   Head
///   SplitBefore
///   Tail
/// After:
///   Head
///   if (Cond)
///     TrueBlock
///   else
////    FalseBlock
///   SplitBefore
///   Tail
///
/// If \p ThenBlock is null, the resulting CFG won't contain the TrueBlock. If
/// \p ThenBlock is non-null and points to non-null BasicBlock pointer, that
/// block will be inserted as the TrueBlock. Otherwise a new block will be
/// created. Likewise for the \p ElseBlock parameter.
/// If \p UnreachableThen or \p UnreachableElse is true, the corresponding newly
/// created blocks will end with UnreachableInst, otherwise with branches to
/// Tail. The function will not modify existing basic blocks passed to it. The
/// caller must ensure that Tail is reachable from Head.
/// Returns the newly created blocks in \p ThenBlock and \p ElseBlock.
/// Updates DTU and LI if given.
///
/// \param Cond Condition controlling the inserted branch.
/// \param SplitBefore Instruction at which the block is split.
/// \param ThenBlock In/out then-block pointer as described above.
/// \param ElseBlock In/out else-block pointer as described above.
/// \param UnreachableThen Whether a newly created then-block ends with
///        unreachable.
/// \param UnreachableElse Whether a newly created else-block ends with
///        unreachable.
/// \param BranchWeights Optional branch-weight metadata for the new branch.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
LLVM_ABI void SplitBlockAndInsertIfThenElse(
    Value *Cond, BasicBlock::iterator SplitBefore, BasicBlock **ThenBlock,
    BasicBlock **ElseBlock, bool UnreachableThen = false,
    bool UnreachableElse = false, MDNode *BranchWeights = nullptr,
    DomTreeUpdater *DTU = nullptr, LoopInfo *LI = nullptr);

/// Split a block and insert optional then/else blocks at an instruction.
///
/// \param Cond Condition controlling the inserted branch.
/// \param SplitBefore Instruction at which the block is split.
/// \param ThenBlock In/out then-block pointer controlling then-path creation.
/// \param ElseBlock In/out else-block pointer controlling else-path creation.
/// \param UnreachableThen Whether a newly created then-block ends with
///        unreachable.
/// \param UnreachableElse Whether a newly created else-block ends with
///        unreachable.
/// \param BranchWeights Optional branch-weight metadata for the new branch.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param LI Optional loop info to update.
inline void SplitBlockAndInsertIfThenElse(Value *Cond, Instruction *SplitBefore,
                                   BasicBlock **ThenBlock,
                                   BasicBlock **ElseBlock,
                                   bool UnreachableThen = false,
                                   bool UnreachableElse = false,
                                   MDNode *BranchWeights = nullptr,
                                   DomTreeUpdater *DTU = nullptr,
                                   LoopInfo *LI = nullptr) {
  SplitBlockAndInsertIfThenElse(Cond, SplitBefore->getIterator(), ThenBlock,
    ElseBlock, UnreachableThen, UnreachableElse, BranchWeights, DTU, LI);
}

/// Insert a simple counted for-loop at a split point.
///
/// Insert a for (int i = 0; i < End; i++) loop structure (with the exception
/// that \p End is assumed > 0, and thus not checked on entry) at \p
/// SplitBefore.  Returns the first insert point in the loop body, and the
/// PHINode for the induction variable (i.e. "i" above).
///
/// \param End Exclusive upper bound of the induction variable; assumed > 0.
/// \param SplitBefore Instruction at which the loop is inserted.
/// \return A pair of the first insert point in the loop body and the induction
///         variable.
LLVM_ABI std::pair<Instruction *, Value *>
SplitBlockAndInsertSimpleForLoop(Value *End, BasicBlock::iterator SplitBefore);

/// Invoke a callback once for each lane of a vector with \p EC elements.
///
/// To simplify porting legacy code, this defaults to unrolling the implied loop
/// for non-scalable element counts, but this is not considered to be part of
/// the contract of this routine, and is expected to change in the future. The
/// callback takes as arguments an IRBuilder whose insert point is correctly set
/// for instantiating the given index, and a value which is (at runtime) the
/// index to access. This index *may* be a constant.
///
/// \param EC Element count describing the number of lanes.
/// \param IndexTy Integer type used for lane indices.
/// \param InsertBefore Instruction at which the per-lane code is inserted.
/// \param Func Callback invoked for each lane index.
LLVM_ABI void SplitBlockAndInsertForEachLane(
    ElementCount EC, Type *IndexTy, BasicBlock::iterator InsertBefore,
    std::function<void(IRBuilderBase &, Value *)> Func);

/// Invoke a callback once for each lane up to an effective vector length.
///
/// EVL is assumed > 0. To simplify porting legacy code, this defaults to
/// unrolling the implied loop for non-scalable element counts, but this is not
/// considered to be part of the contract of this routine, and is expected to
/// change in the future. The callback takes as arguments an IRBuilder whose
/// insert point is correctly set for instantiating the given index, and a value
/// which is (at runtime) the index to access. This index *may* be a constant.
///
/// \param End Effective vector length; assumed > 0.
/// \param InsertBefore Instruction at which the per-lane code is inserted.
/// \param Func Callback invoked for each lane index.
LLVM_ABI void SplitBlockAndInsertForEachLane(
    Value *End, BasicBlock::iterator InsertBefore,
    std::function<void(IRBuilderBase &, Value *)> Func);

/// Return the branch that selects between predecessors of an if-merge block.
///
/// Check whether BB is the merge point of a if-region. If so, return the branch
/// instruction that determines which entry into BB will be taken. Also, return
/// by references the block that will be entered from if the condition is true,
/// and the block that will be entered if the condition is false.
///
/// This does no checking to see if the true/false blocks have large or unsavory
/// instructions in them.
///
/// \param BB Candidate merge block of an if-region.
/// \param IfTrue Set to the predecessor entered when the condition is true.
/// \param IfFalse Set to the predecessor entered when the condition is false.
/// \return The conditional branch that selects between predecessors of \p BB,
///         or null if \p BB is not an if-merge.
LLVM_ABI CondBrInst *GetIfCondition(BasicBlock *BB, BasicBlock *&IfTrue,
                                    BasicBlock *&IfFalse);

/// Split critical edges leaving an indirectbr when a simple case applies.
///
/// Split critical edges where the source of the edge is an indirectbr
/// instruction. This isn't always possible, but we can handle some easy cases.
/// This is useful because MI is unable to split such critical edges,
/// which means it will not be able to sink instructions along those edges.
/// This is especially painful for indirect branches with many successors, where
/// we end up having to prepare all outgoing values in the origin block.
///
/// Our normal algorithm for splitting critical edges requires us to update
/// the outgoing edges of the edge origin block, but for an indirectbr this
/// is hard, since it would require finding and updating the block addresses
/// the indirect branch uses. But if a block only has a single indirectbr
/// predecessor, with the others being regular branches, we can do it in a
/// different way.
/// Say we have A -> D, B -> D, I -> D where only I -> D is an indirectbr.
/// We can split D into D0 and D1, where D0 contains only the PHIs from D,
/// and D1 is the D block body. We can then duplicate D0 as D0A and D0B, and
/// create the following structure:
/// A -> D0A, B -> D0A, I -> D0B, D0A -> D1, D0B -> D1
/// If BPI and BFI aren't non-null, BPI/BFI will be updated accordingly.
/// When `IgnoreBlocksWithoutPHI` is set to `true` critical edges leading to a
/// block without phi-instructions will not be split.
///
/// \param F Function whose indirectbr critical edges may be split.
/// \param IgnoreBlocksWithoutPHI Skip destinations that contain no PHI nodes.
/// \param BPI Optional branch-probability info to update.
/// \param BFI Optional block-frequency info to update.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \return True if any critical edges were split.
LLVM_ABI bool SplitIndirectBrCriticalEdges(Function &F,
                                           bool IgnoreBlocksWithoutPHI,
                                           BranchProbabilityInfo *BPI = nullptr,
                                           BlockFrequencyInfo *BFI = nullptr,
                                           DomTreeUpdater *DTU = nullptr);

/// Invert a conditional branch's condition and swap its successors.
///
/// \param PBI Conditional branch to invert.
/// \param Builder IR builder used to materialize the inverted condition.
LLVM_ABI void InvertBranch(CondBrInst *PBI, IRBuilderBase &Builder);

/// Print BasicBlock \p BB as an operand or print "<nullptr>" if \p BB is a
/// nullptr.
///
/// \param BB Basic block to print, or nullptr.
/// \return A printable representation of \p BB as an operand, or "<nullptr>".
LLVM_ABI Printable printBasicBlock(const BasicBlock *BB);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_BASICBLOCKUTILS_H
