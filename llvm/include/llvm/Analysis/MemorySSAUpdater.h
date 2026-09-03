//===- MemorySSAUpdater.h - Memory SSA Updater-------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// An automatic updater for MemorySSA that handles arbitrary insertion,
// deletion, and moves.  It performs phi insertion where necessary, and
// automatically updates the MemorySSA IR to be correct.
// While updating loads or removing instructions is often easy enough to not
// need this, updating stores should generally not be attemped outside this
// API.
//
// Basic API usage:
// Create the memory access you want for the instruction (this is mainly so
// we know where it is, without having to duplicate the entire set of create
// functions MemorySSA supports).
// Call insertDef or insertUse depending on whether it's a MemoryUse or a
// MemoryDef.
// That's it.
//
// For moving, first, move the instruction itself using the normal SSA
// instruction moving API, then just call moveBefore, moveAfter,or moveTo with
// the right arguments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYSSAUPDATER_H
#define LLVM_ANALYSIS_MEMORYSSAUPDATER_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/CFGDiff.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class BasicBlock;
class DominatorTree;
class Instruction;
class LoopBlocksRPO;
template <typename T, unsigned int N> class SmallSetVector;

/// Map from values to weak tracking value handles.
using ValueToValueMapTy = ValueMap<const Value *, WeakTrackingVH>;
/// Map from MemoryPhi nodes to their defining MemoryAccess.
using PhiToDefMap = SmallDenseMap<MemoryPhi *, MemoryAccess *>;
/// CFG edge update used when applying MemorySSA updates.
using CFGUpdate = cfg::Update<BasicBlock *>;

/// Automatic updater that inserts, deletes, and moves MemorySSA accesses.
///
/// Handles arbitrary insertion, deletion, and moves. It performs phi insertion
/// where necessary, and automatically updates the MemorySSA IR to be correct.
/// While updating loads or removing instructions is often easy enough to not
/// need this, updating stores should generally not be attempted outside this
/// API.
class MemorySSAUpdater {
private:
  MemorySSA *MSSA;

  /// We use WeakVH rather than a costly deletion to deal with dangling pointers.
  /// MemoryPhis are created eagerly and sometimes get zapped shortly afterwards.
  SmallVector<WeakVH, 16> InsertedPHIs;

  SmallPtrSet<BasicBlock *, 8> VisitedBlocks;
  SmallSet<AssertingVH<MemoryPhi>, 8> NonOptPhis;

public:
  /// Construct a MemorySSAUpdater for the given MemorySSA instance.
  ///
  /// \param MSSA MemorySSA IR to update.
  MemorySSAUpdater(MemorySSA *MSSA) : MSSA(MSSA) {}

  /// Insert a definition into the MemorySSA IR.
  ///
  /// RenameUses will rename any use below the new def block (and any inserted
  /// phis). RenameUses should be set to true if the definition may cause new
  /// aliases for loads below it. This is not the case for hoisting or sinking
  /// or other forms of code *movement*. It *is* the case for straight code
  /// insertion.
  /// For example:
  /// store a
  /// if (foo) { }
  /// load a
  ///
  /// Moving the store into the if block, and calling insertDef, does not
  /// require RenameUses.
  /// However, changing it to:
  /// store a
  /// if (foo) { store b }
  /// load a
  /// Where a mayalias b, *does* require RenameUses be set to true.
  ///
  /// \param Def The MemoryDef to insert.
  /// \param RenameUses Whether to rename uses below the new definition.
  LLVM_ABI void insertDef(MemoryDef *Def, bool RenameUses = false);
  /// Insert a use into the MemorySSA IR.
  ///
  /// \param Use The MemoryUse to insert.
  /// \param RenameUses Whether to rename uses affected by the insertion.
  LLVM_ABI void insertUse(MemoryUse *Use, bool RenameUses = false);
  /// Update the MemoryPhi in `To` following an edge deletion between `From` and
  /// `To`. If `To` becomes unreachable, a call to removeBlocks should be made.
  ///
  /// \param From Predecessor block of the deleted edge.
  /// \param To Successor block of the deleted edge.
  LLVM_ABI void removeEdge(BasicBlock *From, BasicBlock *To);
  /// Update the MemoryPhi in `To` to have a single incoming edge from `From`,
  /// following a CFG change that replaced multiple edges (switch) with a direct
  /// branch.
  ///
  /// \param From Predecessor block that remains connected.
  /// \param To Successor block whose MemoryPhi is updated.
  LLVM_ABI void removeDuplicatePhiEdgesBetween(const BasicBlock *From,
                                               const BasicBlock *To);
  /// Update MemorySSA when inserting a unique backedge block for a loop.
  ///
  /// \param LoopHeader Header block of the loop.
  /// \param LoopPreheader Preheader block of the loop.
  /// \param BackedgeBlock Newly inserted unique backedge block.
  LLVM_ABI void
  updatePhisWhenInsertingUniqueBackedgeBlock(BasicBlock *LoopHeader,
                                             BasicBlock *LoopPreheader,
                                             BasicBlock *BackedgeBlock);
  /// Update MemorySSA after a loop was cloned.
  ///
  /// Given the blocks in RPO order, the exit blocks and a 1:1 mapping of all
  /// blocks and instructions cloned. This involves duplicating all defs and
  /// uses in the cloned blocks. Updating phi nodes in exit block successors is
  /// done separately.
  ///
  /// \param LoopBlocks Loop blocks in reverse post-order.
  /// \param ExitBlocks Exit blocks of the cloned loop.
  /// \param VM 1:1 mapping of all blocks and instructions cloned.
  /// \param IgnoreIncomingWithNoClones Skip phi incoming values with no clone.
  LLVM_ABI void updateForClonedLoop(const LoopBlocksRPO &LoopBlocks,
                                    ArrayRef<BasicBlock *> ExitBlocks,
                                    const ValueToValueMapTy &VM,
                                    bool IgnoreIncomingWithNoClones = false);
  /// Update MemorySSA after a block was cloned into its predecessor.
  ///
  /// Block BB was fully or partially cloned into its predecessor P1. Map
  /// contains the 1:1 mapping of instructions cloned and VM[BB]=P1.
  ///
  /// \param BB Original basic block that was cloned.
  /// \param P1 Predecessor into which BB was cloned.
  /// \param VM 1:1 mapping of instructions cloned; VM[BB] is P1.
  LLVM_ABI void updateForClonedBlockIntoPred(BasicBlock *BB, BasicBlock *P1,
                                             const ValueToValueMapTy &VM);
  /// Update phi nodes in exit block successors following cloning.
  ///
  /// Exit blocks that were not cloned don't have additional predecessors added.
  ///
  /// \param ExitBlocks Exit blocks of the cloned loop.
  /// \param VMap 1:1 mapping of cloned values.
  /// \param DT Dominator tree for the function.
  LLVM_ABI void updateExitBlocksForClonedLoop(ArrayRef<BasicBlock *> ExitBlocks,
                                              const ValueToValueMapTy &VMap,
                                              DominatorTree &DT);
  /// Update phi nodes in exit block successors after cloning with multiple maps.
  ///
  /// \param ExitBlocks Exit blocks of the cloned loop.
  /// \param VMaps Per-clone 1:1 mappings of cloned values.
  /// \param DT Dominator tree for the function.
  LLVM_ABI void updateExitBlocksForClonedLoop(
      ArrayRef<BasicBlock *> ExitBlocks,
      ArrayRef<std::unique_ptr<ValueToValueMapTy>> VMaps, DominatorTree &DT);

  /// Apply CFG updates, analogous with the DT edge updates.
  ///
  /// By default, the DT is assumed to be already up to date. If UpdateDTFirst
  /// is true, first update the DT with the same updates.
  ///
  /// \param Updates CFG edge updates to apply.
  /// \param DT Dominator tree for the function.
  /// \param UpdateDTFirst If true, update DT with the same updates first.
  LLVM_ABI void applyUpdates(ArrayRef<CFGUpdate> Updates, DominatorTree &DT,
                             bool UpdateDTFirst = false);
  /// Apply CFG insert updates, analogous with the DT edge updates.
  ///
  /// \param Updates CFG insert updates to apply.
  /// \param DT Dominator tree for the function.
  LLVM_ABI void applyInsertUpdates(ArrayRef<CFGUpdate> Updates,
                                   DominatorTree &DT);

  /// Move a MemoryAccess before another MemoryAccess.
  ///
  /// \param What Access to move.
  /// \param Where Access to insert before.
  LLVM_ABI void moveBefore(MemoryUseOrDef *What, MemoryUseOrDef *Where);
  /// Move a MemoryAccess after another MemoryAccess.
  ///
  /// \param What Access to move.
  /// \param Where Access to insert after.
  LLVM_ABI void moveAfter(MemoryUseOrDef *What, MemoryUseOrDef *Where);
  /// Move a MemoryAccess to a specific place in a basic block.
  ///
  /// \param What Access to move.
  /// \param BB Destination basic block.
  /// \param Where Insertion place within BB.
  LLVM_ABI void moveToPlace(MemoryUseOrDef *What, BasicBlock *BB,
                            MemorySSA::InsertionPlace Where);
  /// Move MemoryAccesses after splicing a block into From and To.
  ///
  /// `From` block was spliced into `From` and `To`. There is a CFG edge from
  /// `From` to `To`. Move all accesses from `From` to `To` starting at
  /// instruction `Start`. `To` is newly created BB, so empty of
  /// MemorySSA::MemoryAccesses. Edges are already updated, so successors of
  /// `To` with MPhi nodes need to update incoming block.
  /// |------|        |------|
  /// | From |        | From |
  /// |      |        |------|
  /// |      |           ||
  /// |      |   =>      \/
  /// |      |        |------|  <- Start
  /// |      |        |  To  |
  /// |------|        |------|
  ///
  /// \param From Block that was spliced; source of moved accesses.
  /// \param To Newly created successor that receives the accesses.
  /// \param Start First instruction whose MemoryAccess should move to To.
  LLVM_ABI void moveAllAfterSpliceBlocks(BasicBlock *From, BasicBlock *To,
                                         Instruction *Start);
  /// Move MemoryAccesses after merging From into To.
  ///
  /// `From` block was merged into `To`. There is a CFG edge from `To` to
  /// `From`.`To` still branches to `From`, but all instructions were moved and
  /// `From` is now an empty block; `From` is about to be deleted. Move all
  /// accesses from `From` to `To` starting at instruction `Start`. `To` may
  /// have multiple successors, `From` has a single predecessor. `From` may have
  /// successors with MPhi nodes, replace their incoming block with `To`.
  /// |------|        |------|
  /// |  To  |        |  To  |
  /// |------|        |      |
  ///    ||      =>   |      |
  ///    \/           |      |
  /// |------|        |      |  <- Start
  /// | From |        |      |
  /// |------|        |------|
  ///
  /// \param From Empty block about to be deleted; source of moved accesses.
  /// \param To Block that absorbed From's instructions.
  /// \param Start First instruction whose MemoryAccess should move to To.
  LLVM_ABI void moveAllAfterMergeBlocks(BasicBlock *From, BasicBlock *To,
                                        Instruction *Start);
  /// Rewire MemorySSA after inserting New as an immediate predecessor of Old.
  ///
  /// A new empty BasicBlock (New) now branches directly to Old. Some of
  /// Old's predecessors (Preds) are now branching to New instead of Old.
  /// If New is the only predecessor, move Old's Phi, if present, to New.
  /// Otherwise, add a new Phi in New with appropriate incoming values, and
  /// update the incoming values in Old's Phi node too, if present.
  ///
  /// \param Old Original block that New now precedes.
  /// \param New Newly inserted empty immediate predecessor of Old.
  /// \param Preds Former predecessors of Old that now branch to New.
  /// \param IdenticalEdgesWereMerged Whether identical CFG edges were merged.
  LLVM_ABI void wireOldPredecessorsToNewImmediatePredecessor(
      BasicBlock *Old, BasicBlock *New, ArrayRef<BasicBlock *> Preds,
      bool IdenticalEdgesWereMerged = true);
  // The below are utility functions. Other than creation of accesses to pass
  // to insertDef, and removeAccess to remove accesses, you should generally
  // not attempt to update memoryssa yourself. It is very non-trivial to get
  // the edge cases right, and the above calls already operate in near-optimal
  // time bounds.

  /// Create a MemoryAccess in MemorySSA at a specified point in a block.
  ///
  /// When used by itself, this method will only insert the new MemoryAccess
  /// into the access list, but not make any other changes, such as inserting
  /// MemoryPHI nodes, or updating users to point to the new MemoryAccess. You
  /// must specify a correct Definition in this case.
  ///
  /// Usually, this API is instead combined with insertUse() or insertDef(),
  /// which will perform all the necessary MSSA updates. If these APIs are used,
  /// then nullptr can be used as Definition, as the correct defining access
  /// will be automatically determined.
  ///
  /// Note: If a MemoryAccess already exists for I, this function will make it
  /// inaccessible and it *must* have removeMemoryAccess called on it.
  ///
  /// \param I Instruction to create a MemoryAccess for.
  /// \param Definition Defining access for the new MemoryAccess, or nullptr.
  /// \param BB Basic block in which to create the access.
  /// \param Point Insertion place within BB.
  /// \param CreationMustSucceed If true, creation must succeed.
  /// \return The newly created MemoryAccess, or nullptr on failure.
  LLVM_ABI MemoryAccess *
  createMemoryAccessInBB(Instruction *I, MemoryAccess *Definition,
                         const BasicBlock *BB, MemorySSA::InsertionPlace Point,
                         bool CreationMustSucceed = true);

  /// Create a MemoryAccess in MemorySSA before an existing MemoryAccess.
  ///
  /// See createMemoryAccessInBB() for usage details.
  ///
  /// \param I Instruction to create a MemoryAccess for.
  /// \param Definition Defining access for the new MemoryAccess, or nullptr.
  /// \param InsertPt Existing access to insert before.
  /// \return The newly created MemoryUseOrDef.
  LLVM_ABI MemoryUseOrDef *createMemoryAccessBefore(Instruction *I,
                                                    MemoryAccess *Definition,
                                                    MemoryUseOrDef *InsertPt);
  /// Create a MemoryAccess in MemorySSA after an existing MemoryAccess.
  ///
  /// See createMemoryAccessInBB() for usage details.
  ///
  /// \param I Instruction to create a MemoryAccess for.
  /// \param Definition Defining access for the new MemoryAccess, or nullptr.
  /// \param InsertPt Existing access to insert after.
  /// \return The newly created MemoryUseOrDef.
  LLVM_ABI MemoryUseOrDef *createMemoryAccessAfter(Instruction *I,
                                                   MemoryAccess *Definition,
                                                   MemoryAccess *InsertPt);

  /// Remove a MemoryAccess from MemorySSA.
  ///
  /// Includes updating all definitions and uses. This should be called when a
  /// memory instruction that has a MemoryAccess associated with it is erased
  /// from the program. For example, if a store or load is simply erased (not
  /// replaced), removeMemoryAccess should be called on the MemoryAccess for
  /// that store/load.
  ///
  /// \param MA MemoryAccess to remove.
  /// \param OptimizePhis If true, try to optimize affected MemoryPhis.
  LLVM_ABI void removeMemoryAccess(MemoryAccess *MA, bool OptimizePhis = false);

  /// Remove MemoryAccess for a given instruction, if a MemoryAccess exists.
  ///
  /// This should be called when an instruction (load/store) is deleted from
  /// the program.
  ///
  /// \param I Instruction whose MemoryAccess should be removed, if any.
  /// \param OptimizePhis If true, try to optimize affected MemoryPhis.
  void removeMemoryAccess(const Instruction *I, bool OptimizePhis = false) {
    if (MemoryAccess *MA = MSSA->getMemoryAccess(I))
      removeMemoryAccess(MA, OptimizePhis);
  }

  /// Remove all MemoryAccesses in blocks about to be deleted.
  ///
  /// Assumption we make here: all uses of deleted defs and phi must either
  /// occur in blocks about to be deleted (thus will be deleted as well), or
  /// they occur in phis that will simply lose an incoming value.
  /// Deleted blocks still have successor info, but their predecessor edges and
  /// Phi nodes may already be updated. Instructions in DeadBlocks should be
  /// deleted after this call.
  ///
  /// \param DeadBlocks Basic blocks about to be deleted.
  LLVM_ABI void removeBlocks(const SmallSetVector<BasicBlock *, 8> &DeadBlocks);

  /// Instruction I will be changed to an unreachable. Remove all accesses in
  /// I's block that follow I (inclusive), and update the Phis in the blocks'
  /// successors.
  ///
  /// \param I Instruction that will become unreachable.
  LLVM_ABI void changeToUnreachable(const Instruction *I);

  /// Get handle on MemorySSA.
  ///
  /// \return The MemorySSA instance being updated.
  MemorySSA* getMemorySSA() const { return MSSA; }

private:
  // Move What before Where in the MemorySSA IR.
  template <class WhereType>
  void moveTo(MemoryUseOrDef *What, BasicBlock *BB, WhereType Where);
  // Move all memory accesses from `From` to `To` starting at `Start`.
  // Restrictions apply, see public wrappers of this method.
  void moveAllAccesses(BasicBlock *From, BasicBlock *To, Instruction *Start);
  MemoryAccess *getPreviousDef(MemoryAccess *);
  MemoryAccess *getPreviousDefInBlock(MemoryAccess *);
  MemoryAccess *
  getPreviousDefFromEnd(BasicBlock *,
                        DenseMap<BasicBlock *, TrackingVH<MemoryAccess>> &);
  MemoryAccess *
  getPreviousDefIterative(BasicBlock *,
                          DenseMap<BasicBlock *, TrackingVH<MemoryAccess>> &);
  MemoryAccess *recursePhi(MemoryAccess *Phi);
  MemoryAccess *tryRemoveTrivialPhi(MemoryPhi *Phi);
  template <class RangeType>
  MemoryAccess *tryRemoveTrivialPhi(MemoryPhi *Phi, RangeType &Operands);
  void tryRemoveTrivialPhis(ArrayRef<WeakVH> UpdatedPHIs);
  void fixupDefs(const SmallVectorImpl<WeakVH> &);
  /// Clone all uses and defs from BB to NewBB given a 1:1 map of all
  /// instructions and blocks cloned, and a map of MemoryPhi : Definition
  /// (MemoryAccess Phi or Def).
  ///
  /// \param VMap Maps old instructions to cloned instructions and old blocks
  ///        to cloned blocks
  /// \param MPhiMap, is created in the caller of this private method, and maps
  ///        existing MemoryPhis to new definitions that new MemoryAccesses
  ///        must point to. These definitions may not necessarily be MemoryPhis
  ///        themselves, they may be MemoryDefs. As such, the map is between
  ///        MemoryPhis and MemoryAccesses, where the MemoryAccesses may be
  ///        MemoryPhis or MemoryDefs and not MemoryUses.
  /// \param IsInClonedRegion Determines whether a basic block was cloned.
  ///        References to accesses outside the cloned region will not be
  ///        remapped.
  /// \param CloneWasSimplified If false, the clone was exact. Otherwise,
  ///        assume that the clone involved simplifications that may have:
  ///        (1) turned a MemoryUse into an instruction that MemorySSA has no
  ///        representation for, or (2) turned a MemoryDef into a MemoryUse or
  ///        an instruction that MemorySSA has no representation for. No other
  ///        cases are supported.
  void cloneUsesAndDefs(BasicBlock *BB, BasicBlock *NewBB,
                        const ValueToValueMapTy &VMap, PhiToDefMap &MPhiMap,
                        function_ref<bool(BasicBlock *)> IsInClonedRegion,
                        bool CloneWasSimplified = false);

  template <typename Iter>
  void privateUpdateExitBlocksForClonedLoop(ArrayRef<BasicBlock *> ExitBlocks,
                                            Iter ValuesBegin, Iter ValuesEnd,
                                            DominatorTree &DT);
  void applyInsertUpdates(ArrayRef<CFGUpdate>, DominatorTree &DT,
                          const GraphDiff<BasicBlock *> *GD);
};
} // end namespace llvm

#endif // LLVM_ANALYSIS_MEMORYSSAUPDATER_H
