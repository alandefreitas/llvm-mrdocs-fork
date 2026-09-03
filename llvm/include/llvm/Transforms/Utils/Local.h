//===- Local.h - Functions to perform local transformations -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform various local transformations to the
// program.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOCAL_H
#define LLVM_TRANSFORMS_UTILS_LOCAL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Utils/SimplifyCFGOptions.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <cstdint>

namespace llvm {

class DataLayout;
class Value;
class WeakTrackingVH;
class WeakVH;
template <typename PtrType> class SmallPtrSetImpl;
template <typename T> class SmallVectorImpl;
class AAResults;
class AllocaInst;
class AssumptionCache;
class BasicBlock;
class CallBase;
class CallInst;
class CondBrInst;
class DIBuilder;
class DomTreeUpdater;
class Function;
class Instruction;
class InvokeInst;
class LoadInst;
class MDNode;
class MemorySSAUpdater;
class PHINode;
class StoreInst;
class TargetLibraryInfo;
class TargetTransformInfo;

//===----------------------------------------------------------------------===//
//  Local constant propagation.
//

/// Fold a constant-predicated terminator into an unconditional branch.
///
/// If a terminator instruction is predicated on a constant value, convert it
/// into an unconditional branch to the constant destination. This is a
/// nontrivial operation because the successors of this basic block must have
/// their PHI nodes updated. Also calls
/// RecursivelyDeleteTriviallyDeadInstructions() on any branch/switch conditions
/// and indirectbr addresses this might make dead if DeleteDeadConditions is
/// true.
///
/// \param BB Basic block whose terminator may be folded.
/// \param DeleteDeadConditions Whether to delete conditions made dead by the
/// fold.
/// \param TLI Optional target library info for dead-instruction analysis.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \return True if the terminator was folded into an unconditional branch.
LLVM_ABI bool ConstantFoldTerminator(BasicBlock *BB,
                                     bool DeleteDeadConditions = false,
                                     const TargetLibraryInfo *TLI = nullptr,
                                     DomTreeUpdater *DTU = nullptr);

//===----------------------------------------------------------------------===//
//  Local dead code elimination.
//

/// Return true if an unused instruction is trivially dead.
///
/// Return true if the result produced by the instruction is not used, and the
/// instruction will return. Certain side-effecting instructions are also
/// considered dead if there are no uses of the instruction.
///
/// \param I Instruction to test.
/// \param TLI Optional target library info used for library-call analysis.
/// \return True if the unused instruction is trivially dead.
LLVM_ABI bool
isInstructionTriviallyDead(Instruction *I,
                           const TargetLibraryInfo *TLI = nullptr);

/// Return true if an instruction would be trivially dead with no uses.
///
/// Return true if the result produced by the instruction would have no side
/// effects if it was not used. This is equivalent to checking whether
/// isInstructionTriviallyDead would be true if the use count was 0.
///
/// \param I Instruction to test.
/// \param TLI Optional target library info used for library-call analysis.
/// \return True if the instruction would be trivially dead with no uses.
LLVM_ABI bool
wouldInstructionBeTriviallyDead(const Instruction *I,
                                const TargetLibraryInfo *TLI = nullptr);

/// Recursively delete a trivially dead instruction and its dead operands.
///
/// If the specified value is a trivially dead instruction, delete it. If that
/// makes any of its operands trivially dead, delete them too, recursively.
/// Return true if any instructions were deleted.
///
/// \param V Value that may be a trivially dead instruction.
/// \param TLI Optional target library info used for deadness analysis.
/// \param MSSAU Optional MemorySSA updater notified of deletions.
/// \param AboutToDeleteCallback Optional callback invoked before each deletion.
/// \return True if any instructions were deleted.
LLVM_ABI bool RecursivelyDeleteTriviallyDeadInstructions(
    Value *V, const TargetLibraryInfo *TLI = nullptr,
    MemorySSAUpdater *MSSAU = nullptr,
    std::function<void(Value *)> AboutToDeleteCallback =
        std::function<void(Value *)>());

/// Recursively delete a worklist of trivially dead instructions.
///
/// Delete all of the instructions in `DeadInsts`, and all other instructions
/// that deleting these in turn causes to be trivially dead.
///
/// The initial instructions in the provided vector must all have empty use
/// lists and satisfy `isInstructionTriviallyDead`.
///
/// `DeadInsts` will be used as scratch storage for this routine and will be
/// empty afterward.
///
/// \param DeadInsts Worklist of trivially dead instructions to delete.
/// \param TLI Optional target library info used for deadness analysis.
/// \param MSSAU Optional MemorySSA updater notified of deletions.
/// \param AboutToDeleteCallback Optional callback invoked before each deletion.
LLVM_ABI void RecursivelyDeleteTriviallyDeadInstructions(
    SmallVectorImpl<WeakTrackingVH> &DeadInsts,
    const TargetLibraryInfo *TLI = nullptr, MemorySSAUpdater *MSSAU = nullptr,
    std::function<void(Value *)> AboutToDeleteCallback =
        std::function<void(Value *)>());

/// Recursively delete trivially dead instructions from a permissive worklist.
///
/// Same functionality as RecursivelyDeleteTriviallyDeadInstructions, but allow
/// instructions that are not trivially dead. These will be ignored. Returns
/// true if any changes were made, i.e. any instructions trivially dead were
/// found and deleted.
///
/// \param DeadInsts Worklist that may contain non-dead instructions.
/// \param TLI Optional target library info used for deadness analysis.
/// \param MSSAU Optional MemorySSA updater notified of deletions.
/// \param AboutToDeleteCallback Optional callback invoked before each deletion.
/// \return True if any trivially dead instructions were found and deleted.
LLVM_ABI bool RecursivelyDeleteTriviallyDeadInstructionsPermissive(
    SmallVectorImpl<WeakTrackingVH> &DeadInsts,
    const TargetLibraryInfo *TLI = nullptr, MemorySSAUpdater *MSSAU = nullptr,
    std::function<void(Value *)> AboutToDeleteCallback =
        std::function<void(Value *)>());

/// Recursively delete an effectively dead PHI and its dead operands.
///
/// If the specified value is an effectively dead PHI node, due to being a
/// def-use chain of single-use nodes that either forms a cycle or is terminated
/// by a trivially dead instruction, delete it. If that makes any of its
/// operands trivially dead, delete them too, recursively. Return true if a
/// change was made.
///
/// \param PN PHI node that may be effectively dead.
/// \param TLI Optional target library info used for deadness analysis.
/// \param MSSAU Optional MemorySSA updater notified of deletions.
/// \param KnownNonDeadPHIs Optional set of PHIs known not to be dead.
/// \return True if a change was made.
LLVM_ABI bool RecursivelyDeleteDeadPHINode(
    PHINode *PN, const TargetLibraryInfo *TLI = nullptr,
    MemorySSAUpdater *MSSAU = nullptr,
    SmallPtrSetImpl<PHINode *> *KnownNonDeadPHIs = nullptr);

/// Simplify instructions in a block and recursively delete dead ones.
///
/// Scan the specified basic block and try to simplify any instructions in it
/// and recursively delete dead instructions.
///
/// This returns true if it changed the code, note that it can delete
/// instructions in other blocks as well in this block.
///
/// \param BB Basic block whose instructions may be simplified.
/// \param TLI Optional target library info used for simplification.
/// \return True if any instructions were changed or deleted.
LLVM_ABI bool
SimplifyInstructionsInBlock(BasicBlock *BB,
                            const TargetLibraryInfo *TLI = nullptr);

//===----------------------------------------------------------------------===//
//  Control Flow Graph Restructuring.
//

/// Merge a block into its unique predecessor when that edge is trivial.
///
/// BB is a block with one predecessor and its predecessor is known to have one
/// successor (BB!). Eliminate the edge between them, moving the instructions in
/// the predecessor into BB. This deletes the predecessor block.
///
/// \param BB Block to absorb its unique predecessor into.
/// \param DTU Optional dominator-tree updater for CFG changes.
LLVM_ABI void MergeBasicBlockIntoOnlyPred(BasicBlock *BB,
                                          DomTreeUpdater *DTU = nullptr);

/// Eliminate an empty block that only contains an unconditional branch.
///
/// BB is known to contain an unconditional branch, and contains no instructions
/// other than PHI nodes, potential debug intrinsics and the branch. If
/// possible, eliminate BB by rewriting all the predecessors to branch to the
/// successor block and return true. If we can't transform, return false.
///
/// \param BB Empty block terminated by an unconditional branch.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \return True if the empty block was eliminated.
LLVM_ABI bool
TryToSimplifyUncondBranchFromEmptyBlock(BasicBlock *BB,
                                        DomTreeUpdater *DTU = nullptr);

/// Eliminate duplicate PHI nodes in a basic block.
///
/// Check for and eliminate duplicate PHI nodes in this block. This doesn't try
/// to be clever about PHI nodes which differ only in the order of the incoming
/// values, but instcombine orders them so it usually won't matter.
///
/// This overload removes the duplicate PHI nodes directly.
///
/// \param BB Basic block whose PHI nodes may be deduplicated.
/// \return True if any duplicate PHI nodes were eliminated.
LLVM_ABI bool EliminateDuplicatePHINodes(BasicBlock *BB);

/// Collect duplicate PHI nodes in a basic block for later removal.
///
/// Check for and eliminate duplicate PHI nodes in this block. This doesn't try
/// to be clever about PHI nodes which differ only in the order of the incoming
/// values, but instcombine orders them so it usually won't matter.
///
/// This overload collects the PHI nodes to be removed into the ToRemove set.
///
/// \param BB Basic block whose PHI nodes may be deduplicated.
/// \param ToRemove Set filled with duplicate PHI nodes to remove.
/// \return True if any duplicate PHI nodes were found.
LLVM_ABI bool EliminateDuplicatePHINodes(BasicBlock *BB,
                                         SmallPtrSetImpl<PHINode *> &ToRemove);

/// When true, SimplifyCFG requires a dominator tree and preserves it.
LLVM_ABI extern cl::opt<bool> RequireAndPreserveDomTree;

/// Simplify the CFG around a basic block with peephole optimizations.
///
/// This function is used to do simplification of a CFG. For example, it adjusts
/// branches to branches to eliminate the extra hop, it eliminates unreachable
/// basic blocks, and does other peephole optimization of the CFG. It returns
/// true if a modification was made, possibly deleting the basic block that was
/// pointed to. LoopHeaders is an optional input parameter providing the set of
/// loop headers that SimplifyCFG should not eliminate.
///
/// \param BB Basic block to simplify.
/// \param TTI Target transform info used for cost decisions.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param Options Extra SimplifyCFG knobs.
/// \param LoopHeaders Optional loop headers that must not be eliminated.
/// \return True if any CFG modification was made.
LLVM_ABI bool simplifyCFG(BasicBlock *BB, const TargetTransformInfo &TTI,
                          DomTreeUpdater *DTU = nullptr,
                          const SimplifyCFGOptions &Options = {},
                          ArrayRef<WeakVH> LoopHeaders = {});

/// Flatten a CFG by collapsing related if-conditions and regions.
///
/// This function is used to flatten a CFG. For example, it uses parallel-and
/// and parallel-or mode to collapse if-conditions and merge if-regions with
/// identical statements.
///
/// \param BB Basic block whose surrounding CFG may be flattened.
/// \param AA Optional alias analysis used during flattening.
/// \return True if the CFG was flattened.
LLVM_ABI bool FlattenCFG(BasicBlock *BB, AAResults *AA = nullptr);

/// Fold a setcc-and-branch block into a predecessor that already targets both.
///
/// If this basic block is ONLY a setcc and a branch, and if a predecessor
/// branches to us and one of our successors, fold the setcc into the
/// predecessor and use logical operations to pick the right destination.
///
/// \param BI Conditional branch that may be folded into a predecessor.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param MSSAU Optional MemorySSA updater for moved or deleted instructions.
/// \param TTI Optional target transform info used for profitability checks.
/// \param AC Optional assumption cache used during folding.
/// \param BonusInstThreshold Maximum bonus instructions allowed when folding.
/// \return True if the branch was folded into a predecessor.
LLVM_ABI bool foldBranchToCommonDest(CondBrInst *BI,
                                     llvm::DomTreeUpdater *DTU = nullptr,
                                     MemorySSAUpdater *MSSAU = nullptr,
                                     const TargetTransformInfo *TTI = nullptr,
                                     AssumptionCache *AC = nullptr,
                                     unsigned BonusInstThreshold = 1);

/// Demote an instruction's virtual register to a stack slot.
///
/// This function takes a virtual register computed by an Instruction and
/// replaces it with a slot in the stack frame, allocated via alloca. This
/// allows the CFG to be changed around without fear of invalidating the SSA
/// information for the value. It returns the pointer to the alloca inserted to
/// create a stack slot for X.
///
/// \param X Instruction whose value is demoted to the stack.
/// \param VolatileLoads Whether loads of the stack slot should be volatile.
/// \param AllocaPoint Optional insertion point for the alloca.
/// \return Pointer to the alloca inserted for the stack slot.
LLVM_ABI AllocaInst *DemoteRegToStack(
    Instruction &X, bool VolatileLoads = false,
    std::optional<BasicBlock::iterator> AllocaPoint = std::nullopt);

/// Demote a PHI node's virtual register to a stack slot.
///
/// This function takes a virtual register computed by a phi node and replaces
/// it with a slot in the stack frame, allocated via alloca. The phi node is
/// deleted and it returns the pointer to the alloca inserted.
///
/// \param P PHI node whose value is demoted to the stack.
/// \param AllocaPoint Optional insertion point for the alloca.
/// \return Pointer to the alloca inserted for the stack slot.
LLVM_ABI AllocaInst *DemotePHIToStack(
    PHINode *P, std::optional<BasicBlock::iterator> AllocaPoint = std::nullopt);

/// Try to raise a controlled object's alignment to a preferred value.
///
/// If the specified pointer points to an object that we control, try to modify
/// the object's alignment to PrefAlign. Returns a minimum known alignment of
/// the value after the operation, which may be lower than PrefAlign.
///
/// Increating value alignment isn't often possible though. If alignment is
/// important, a more reliable approach is to simply align all global variables
/// and allocation instructions to their preferred alignment from the beginning.
///
/// \param V Pointer value whose underlying object may be realigned.
/// \param PrefAlign Preferred alignment to try to enforce.
/// \param DL Data layout used to reason about object alignment.
/// \return Minimum known alignment of the value after the operation.
LLVM_ABI Align tryEnforceAlignment(Value *V, Align PrefAlign,
                                   const DataLayout &DL);

/// Return a known alignment for a value, optionally raising it first.
///
/// Try to ensure that the alignment of \p V is at least \p PrefAlign bytes. If
/// the owning object can be modified and has an alignment less than \p
/// PrefAlign, it will be increased and \p PrefAlign returned. If the alignment
/// cannot be increased, the known alignment of the value is returned.
///
/// It is not always possible to modify the alignment of the underlying object,
/// so if alignment is important, a more reliable approach is to simply align
/// all global variables and allocation instructions to their preferred
/// alignment from the beginning.
///
/// \param V Pointer value whose alignment is queried or enforced.
/// \param PrefAlign Preferred alignment to try to ensure, if any.
/// \param DL Data layout used to reason about object alignment.
/// \param CxtI Optional context instruction for local analysis.
/// \param AC Optional assumption cache used during analysis.
/// \param DT Optional dominator tree used during analysis.
/// \return The ensured or known alignment of \p V.
LLVM_ABI Align getOrEnforceKnownAlignment(Value *V, MaybeAlign PrefAlign,
                                          const DataLayout &DL,
                                          const Instruction *CxtI = nullptr,
                                          AssumptionCache *AC = nullptr,
                                          const DominatorTree *DT = nullptr);

/// Infer a known alignment for the specified pointer.
///
/// \param V Pointer value whose alignment is queried.
/// \param DL Data layout used to reason about object alignment.
/// \param CxtI Optional context instruction for local analysis.
/// \param AC Optional assumption cache used during analysis.
/// \param DT Optional dominator tree used during analysis.
/// \return Known alignment of the specified pointer.
inline Align getKnownAlignment(Value *V, const DataLayout &DL,
                               const Instruction *CxtI = nullptr,
                               AssumptionCache *AC = nullptr,
                               const DominatorTree *DT = nullptr) {
  return getOrEnforceKnownAlignment(V, MaybeAlign(), DL, CxtI, AC, DT);
}

/// Create a call that matches an invoke's operands and attributes.
///
/// Create a call that matches the invoke \p II in terms of arguments,
/// attributes, debug information, etc. The call is not placed in a block and it
/// will not have a name. The invoke instruction is not removed, nor are the
/// uses replaced by the new call.
///
/// \param II Invoke instruction to mirror as a call.
/// \return Newly created call matching the invoke.
LLVM_ABI CallInst *createCallMatchingInvoke(InvokeInst *II);

/// Convert the specified invoke into a normal call.
///
/// \param II Invoke instruction to convert.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \return Call that replaced the invoke.
LLVM_ABI CallInst *changeToCall(InvokeInst *II, DomTreeUpdater *DTU = nullptr);

///===---------------------------------------------------------------------===//
///  Dbg Intrinsic utilities
///

/// Insert a dbg.value before a store with an associated dbg.value.
///
/// Creates and inserts a dbg_value record intrinsic before a store that has an
/// associated llvm.dbg.value intrinsic.
///
/// \param DVR Debug variable record associated with the store.
/// \param SI Store at which to insert the dbg.value.
/// \param Builder DIBuilder used to create the dbg.value.
LLVM_ABI void InsertDebugValueAtStoreLoc(DbgVariableRecord *DVR, StoreInst *SI,
                                         DIBuilder &Builder);

/// Convert a dbg.declare into a dbg.value at a store.
///
/// Inserts a dbg.value record before a store to an alloca'd value that has an
/// associated dbg.declare record.
///
/// \param DVR dbg.declare record to lower at the store.
/// \param SI Store that writes the alloca'd value.
/// \param Builder DIBuilder used to create the dbg.value.
LLVM_ABI void ConvertDebugDeclareToDebugValue(DbgVariableRecord *DVR,
                                              StoreInst *SI,
                                              DIBuilder &Builder);

/// Convert a dbg.declare into a dbg.value at a load.
///
/// Inserts a dbg.value record before a load of an alloca'd value that has an
/// associated dbg.declare record.
///
/// \param DVR dbg.declare record to lower at the load.
/// \param LI Load that reads the alloca'd value.
/// \param Builder DIBuilder used to create the dbg.value.
LLVM_ABI void ConvertDebugDeclareToDebugValue(DbgVariableRecord *DVR,
                                              LoadInst *LI, DIBuilder &Builder);

/// Convert a dbg.declare into a dbg.value after a PHI.
///
/// Inserts a dbg.value record after a phi that has an associated
/// llvm.dbg.declare record.
///
/// \param DVR dbg.declare record to lower at the PHI.
/// \param LI PHI whose value should be described.
/// \param Builder DIBuilder used to create the dbg.value.
LLVM_ABI void ConvertDebugDeclareToDebugValue(DbgVariableRecord *DVR,
                                              PHINode *LI, DIBuilder &Builder);

/// Lower dbg.declare records into dbg.value records.
///
/// \param F Function whose dbg.declare records may be lowered.
/// \return True if any dbg.declare records were lowered.
LLVM_ABI bool LowerDbgDeclare(Function &F);

/// Propagate dbg.value intrinsics through newly inserted PHIs.
///
/// \param BB Basic block containing the newly inserted PHIs.
/// \param InsertedPHIs PHIs that need dbg.value propagation.
LLVM_ABI void
insertDebugValuesForPHIs(BasicBlock *BB,
                         SmallVectorImpl<PHINode *> &InsertedPHIs);

/// Replace a dbg.declare when its address is replaced.
///
/// Replaces dbg.declare record when the address it describes is replaced with a
/// new value. If Deref is true, an additional DW_OP_deref is prepended to the
/// expression. If Offset is non-zero, a constant displacement is added to the
/// expression (between the optional Deref operations). Offset can be negative.
///
/// \param Address Original address described by the dbg.declare.
/// \param NewAddress Replacement address for the dbg.declare.
/// \param Builder DIBuilder used to rewrite the declare.
/// \param DIExprFlags Flags controlling expression rewriting, including Deref.
/// \param Offset Constant displacement added to the expression.
/// \return True if any dbg.declare records were replaced.
LLVM_ABI bool replaceDbgDeclare(Value *Address, Value *NewAddress,
                                DIBuilder &Builder, uint8_t DIExprFlags,
                                int Offset);

/// Replace dbg.value records when an alloca address is replaced.
///
/// Replaces multiple dbg.value records when the alloca it describes is replaced
/// with a new value. If Offset is non-zero, a constant displacement is added to
/// the expression (after the mandatory Deref). Offset can be negative. New
/// dbg.value records are inserted at the locations of the instructions they
/// replace.
///
/// \param AI Alloca whose describing dbg.value records are rewritten.
/// \param NewAllocaAddress Replacement address for the alloca.
/// \param Builder DIBuilder used to rewrite the dbg.value records.
/// \param Offset Constant displacement added after the mandatory Deref.
LLVM_ABI void replaceDbgValueForAlloca(AllocaInst *AI, Value *NewAllocaAddress,
                                       DIBuilder &Builder, int Offset = 0);

/// Salvage debug records that use an instruction before it is deleted.
///
/// Salvage debug records that use \p I before the instruction is deleted.
/// Rewrite those uses in terms of its operands where we can, and encode the
/// instruction's effect in the record's DIExpression. Deleting the instruction
/// replaces any remaining debug-record uses with poison.
///
/// \param I Instruction about to be deleted whose debug uses may be salvaged.
LLVM_ABI void salvageDebugInfo(Instruction &I);

/// Salvage a specific set of debug records that use an instruction.
///
/// Salvage only the records in \p DbgRecords instead of finding every debug
/// user of \p I. Every record must be a debug user of the instruction.
///
/// Process records in order. For a dbg.assign, salvage a matching address
/// before its variable location since replacing a variable-location operand
/// can also replace the address. Stop when a checked variable location cannot
/// be salvaged. A matching address counts as processed even if salvage leaves
/// it unchanged. If nothing was processed, call setKillLocation() on every
/// supplied record.
///
/// \param I Instruction whose effect may be encoded into the records.
/// \param DbgRecords Debug records that use \p I and should be salvaged.
LLVM_ABI void
salvageDebugInfoForDbgValues(Instruction &I,
                             ArrayRef<DbgVariableRecord *> DbgRecords);

/// Append an instruction's effect to a salvaged DIExpression operand list.
///
/// Given an instruction \p I and DIExpression \p DIExpr operating on it, append
/// the effects of \p I to the DIExpression operand list \p Ops, or return \p
/// nullptr if it cannot be salvaged. \p CurrentLocOps is the number of SSA
/// values referenced by the incoming \p Ops. \return the first non-constant
/// operand implicitly referred to by Ops. If \p I references more than one
/// non-constant operand, any additional operands are added to \p
/// AdditionalValues.
///
/// \example
////
///   I = add %a, i32 1
///
///   Return = %a
///   Ops = llvm::dwarf::DW_OP_lit1 llvm::dwarf::DW_OP_add
///
///   I = add %a, %b
///
///   Return = %a
///   Ops = llvm::dwarf::DW_OP_LLVM_arg0 llvm::dwarf::DW_OP_add
///   AdditionalValues = %b
///
/// \param I Instruction whose effect may be appended to \p Ops.
/// \param CurrentLocOps Number of SSA values already referenced by \p Ops.
/// \param Ops DIExpression operand list being rewritten.
/// \param AdditionalValues Extra non-constant operands referenced by \p I.
LLVM_ABI Value *
salvageDebugInfoImpl(Instruction &I, uint64_t CurrentLocOps,
                     SmallVectorImpl<uint64_t> &Ops,
                     SmallVectorImpl<Value *> &AdditionalValues);

/// Retarget or salvage debug users when replacing a value that will be deleted.
///
/// Point debug users of \p From to \p To or salvage them. Use this function only
/// when replacing all uses of \p From with \p To, with a guarantee that \p From
/// is going to be deleted.
///
/// Follow these rules to prevent use-before-def of \p To:
///   . If \p To is a linked Instruction, set \p DomPoint to \p To.
///   . If \p To is an unlinked Instruction, set \p DomPoint to the Instruction
///     \p To will be inserted after.
///   . If \p To is not an Instruction (e.g a Constant), the choice of
///     \p DomPoint is arbitrary. Pick \p From for simplicity.
///
/// If a debug user cannot be preserved without reordering variable updates or
/// introducing a use-before-def, it is either salvaged (\ref salvageDebugInfo)
/// or deleted. Returns true if any debug users were updated.
///
/// \param From Instruction whose debug users are being retargeted.
/// \param To Replacement value for those debug users.
/// \param DomPoint Dominance point used to avoid use-before-def of \p To.
/// \param DT Dominator tree used to check dominance of \p DomPoint.
/// \return True if any debug users were updated.
LLVM_ABI bool replaceAllDbgUsesWith(Instruction &From, Value &To,
                                    Instruction &DomPoint, DominatorTree &DT);

/// Replace instruction operands of an unreachable terminator with poison.
///
/// If a terminator in an unreachable basic block has an operand of type
/// Instruction, transform it into poison. Return true if any operands are
/// changed to poison. Original Values prior to being changed to poison are
/// returned in \p PoisonedValues.
///
/// \param I Unreachable terminator whose operands may be poisoned.
/// \param PoisonedValues Original operand values replaced with poison.
/// \return True if any operands were changed to poison.
LLVM_ABI bool
handleUnreachableTerminator(Instruction *I,
                            SmallVectorImpl<Value *> &PoisonedValues);

/// Remove all non-terminator, non-EH-pad instructions from a block.
///
/// Remove all instructions from a basic block other than its terminator and any
/// present EH pad instructions. Returns the number of instructions that have
/// been removed.
///
/// \param BB Basic block to strip of non-terminator instructions.
/// \return Number of instructions removed.
LLVM_ABI unsigned removeAllNonTerminatorAndEHPadInstructions(BasicBlock *BB);

/// Insert unreachable before an instruction and make the rest of the block dead.
///
/// \param I Instruction before which unreachable is inserted.
/// \param PreserveLCSSA Whether to preserve LCSSA form while rewriting.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param MSSAU Optional MemorySSA updater for deleted instructions.
/// \return Number of instructions made unreachable or removed.
LLVM_ABI unsigned changeToUnreachable(Instruction *I,
                                      bool PreserveLCSSA = false,
                                      DomTreeUpdater *DTU = nullptr,
                                      MemorySSAUpdater *MSSAU = nullptr);

/// Convert a call into an invoke and split its basic block.
///
/// Convert the CallInst to InvokeInst with the specified unwind edge basic
/// block. This also splits the basic block where CI is located, because
/// InvokeInst is a terminator instruction. Returns the newly split basic block.
///
/// \param CI Call to convert into an invoke.
/// \param UnwindEdge Unwind successor for the new invoke.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \return Newly split basic block following the invoke.
LLVM_ABI BasicBlock *
changeToInvokeAndSplitBasicBlock(CallInst *CI, BasicBlock *UnwindEdge,
                                 DomTreeUpdater *DTU = nullptr);

/// Replace a terminator so it no longer has an unwind successor.
///
/// Replace 'BB's terminator with one that does not have an unwind successor
/// block. Rewrites `invoke` to `call`, etc. Updates any PHIs in unwind
/// successor. Returns the instruction that replaced the original terminator,
/// which might be a call in case the original terminator was an invoke.
///
/// \param BB Block whose terminator will be replaced. Its terminator must have
/// an unwind successor.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \return Instruction that replaced the original terminator.
LLVM_ABI Instruction *removeUnwindEdge(BasicBlock *BB,
                                       DomTreeUpdater *DTU = nullptr);

/// Remove blocks unreachable from a function's entry.
///
/// Remove all blocks that can not be reached from the function's entry. When \p
/// FoldInstsToUnreachable is true, it will also convert obviously unreachable
/// instructions into unreachable (e.g, store to null).
///
/// Returns true if any basic block was removed or any instruction was folded.
///
/// \param F Function whose unreachable blocks may be removed.
/// \param DTU Optional dominator-tree updater for CFG changes.
/// \param MSSAU Optional MemorySSA updater for deleted instructions.
/// \param FoldInstsToUnreachable Whether to fold obviously unreachable
/// instructions into unreachable.
/// \return True if any basic block was removed or any instruction was folded.
LLVM_ABI bool removeUnreachableBlocks(Function &F,
                                      DomTreeUpdater *DTU = nullptr,
                                      MemorySSAUpdater *MSSAU = nullptr,
                                      bool FoldInstsToUnreachable = true);

/// Combine metadata so one instruction can replace another after CSE.
///
/// Combine the metadata of two instructions so that K can replace J. This
/// specifically handles the case of CSE-like transformations. Some metadata can
/// only be kept if K dominates J. For this to be correct, K cannot be hoisted.
///
/// Unknown metadata is removed.
///
/// \param K Surviving instruction that may replace \p J.
/// \param J Instruction being replaced by \p K.
/// \param DoesKMove Whether \p K may move relative to \p J.
LLVM_ABI void combineMetadataForCSE(Instruction *K, const Instruction *J,
                                    bool DoesKMove);

/// Combine alias-analysis metadata after merging memory accesses.
///
/// Combine metadata of two instructions, where instruction J is a memory access
/// that has been merged into K. This will intersect alias-analysis metadata,
/// while preserving other known metadata.
///
/// \param K Surviving memory-access instruction.
/// \param J Memory-access instruction merged into \p K.
LLVM_ABI void combineAAMetadata(Instruction *K, const Instruction *J);

/// Copy metadata from a source load onto its replacement.
///
/// Copy the metadata from the source instruction to the destination (the
/// replacement for the source instruction).
///
/// \param Dest Destination load that receives the metadata.
/// \param Source Source load whose metadata is copied.
LLVM_ABI void copyMetadataForLoad(LoadInst &Dest, const LoadInst &Source);

/// Relax a replacement so it is no more restrictive than the original.
///
/// Patch the replacement so that it is not more restrictive than the value
/// being replaced. It assumes that the replacement does not get moved from its
/// original position.
///
/// \param I Original instruction being replaced.
/// \param Repl Replacement value that may need patching.
LLVM_ABI void patchReplacementInstruction(Instruction *I, Value *Repl);

/// Replace non-local uses of an instruction with another value.
///
/// Replace each use of 'From' with 'To', if that use does not belong to basic
/// block where 'From' is defined. Returns the number of replacements made.
///
/// \param From Instruction whose non-local uses are replaced.
/// \param To Replacement value for those uses.
/// \return Number of replacements made.
LLVM_ABI unsigned replaceNonLocalUsesWith(Instruction *From, Value *To);

/// Replace uses dominated by an edge with another value.
///
/// Replace each use of 'From' with 'To' if that use is dominated by the given
/// edge. Returns the number of replacements made.
///
/// \param From Value whose dominated uses are replaced.
/// \param To Replacement value for those uses.
/// \param DT Dominator tree used to test edge dominance.
/// \param Edge Edge that must dominate a use for it to be replaced.
/// \return Number of replacements made.
LLVM_ABI unsigned replaceDominatedUsesWith(Value *From, Value *To,
                                           DominatorTree &DT,
                                           const BasicBlockEdge &Edge);

/// Replace uses dominated by a block's end with another value.
///
/// Replace each use of 'From' with 'To' if that use is dominated by the end of
/// the given BasicBlock. Returns the number of replacements made.
///
/// \param From Value whose dominated uses are replaced.
/// \param To Replacement value for those uses.
/// \param DT Dominator tree used to test block dominance.
/// \param BB Block whose end must dominate a use for it to be replaced.
/// \return Number of replacements made.
LLVM_ABI unsigned replaceDominatedUsesWith(Value *From, Value *To,
                                           DominatorTree &DT,
                                           const BasicBlock *BB);

/// Replace uses dominated by an instruction with another value.
///
/// Replace each use of 'From' with 'To' if that use is dominated by the given
/// instruction. Returns the number of replacements made.
///
/// \param From Value whose dominated uses are replaced.
/// \param To Replacement value for those uses.
/// \param DT Dominator tree used to test instruction dominance.
/// \param I Instruction that must dominate a use for it to be replaced.
/// \return Number of replacements made.
LLVM_ABI unsigned replaceDominatedUsesWith(Value *From, Value *To,
                                           DominatorTree &DT,
                                           const Instruction *I);

/// Conditionally replace uses dominated by an edge with another value.
///
/// Replace each use of 'From' with 'To' if that use is dominated by the given
/// edge and the callback ShouldReplace returns true. Returns the number of
/// replacements made.
///
/// \param From Value whose dominated uses may be replaced.
/// \param To Replacement value for selected uses.
/// \param DT Dominator tree used to test edge dominance.
/// \param Edge Edge that must dominate a use for it to be considered.
/// \param ShouldReplace Predicate that selects which dominated uses to replace.
/// \return Number of replacements made.
LLVM_ABI unsigned replaceDominatedUsesWithIf(
    Value *From, Value *To, DominatorTree &DT, const BasicBlockEdge &Edge,
    function_ref<bool(const Use &U, const Value *To)> ShouldReplace);

/// Conditionally replace uses dominated by a block's end with another value.
///
/// Replace each use of 'From' with 'To' if that use is dominated by the end of
/// the given BasicBlock and the callback ShouldReplace returns true. Returns
/// the number of replacements made.
///
/// \param From Value whose dominated uses may be replaced.
/// \param To Replacement value for selected uses.
/// \param DT Dominator tree used to test block dominance.
/// \param BB Block whose end must dominate a use for it to be considered.
/// \param ShouldReplace Predicate that selects which dominated uses to replace.
/// \return Number of replacements made.
LLVM_ABI unsigned replaceDominatedUsesWithIf(
    Value *From, Value *To, DominatorTree &DT, const BasicBlock *BB,
    function_ref<bool(const Use &U, const Value *To)> ShouldReplace);

/// Conditionally replace uses dominated by an instruction with another value.
///
/// Replace each use of 'From' with 'To' if that use is dominated by the given
/// instruction and the callback ShouldReplace returns true. Returns the number
/// of replacements made.
///
/// \param From Value whose dominated uses may be replaced.
/// \param To Replacement value for selected uses.
/// \param DT Dominator tree used to test instruction dominance.
/// \param I Instruction that must dominate a use for it to be considered.
/// \param ShouldReplace Predicate that selects which dominated uses to replace.
/// \return Number of replacements made.
LLVM_ABI unsigned replaceDominatedUsesWithIf(
    Value *From, Value *To, DominatorTree &DT, const Instruction *I,
    function_ref<bool(const Use &U, const Value *To)> ShouldReplace);

/// Return true if this call calls a GC leaf function.
///
/// A leaf function is a function that does not safepoint the thread during its
/// execution. During a call or invoke to such a function, the callers stack
/// does not have to be made parseable.
///
/// Most passes can and should ignore this information, and it is only used
/// during lowering by the GC infrastructure.
///
/// \param Call Call or invoke to classify.
/// \param TLI Target library info used to recognize known leaf functions.
/// \return True if the call targets a GC leaf function.
LLVM_ABI bool callsGCLeafFunction(const CallBase *Call,
                                  const TargetLibraryInfo &TLI);

/// Copy nonnull metadata onto a replacement load.
///
/// Copy a nonnull metadata node to a new load instruction.
///
/// This handles mapping it to range metadata if the new load is an integer load
/// instead of a pointer load.
///
/// \param OldLI Original load that carried the metadata.
/// \param N Nonnull metadata node to copy or remap.
/// \param NewLI Replacement load that receives the metadata.
LLVM_ABI void copyNonnullMetadata(const LoadInst &OldLI, MDNode *N,
                                  LoadInst &NewLI);

/// Copy range metadata onto a replacement load.
///
/// Copy a range metadata node to a new load instruction.
///
/// This handles mapping it to nonnull metadata if the new load is a pointer
/// load instead of an integer load and the range doesn't cover null.
///
/// \param DL Data layout used when remapping range metadata.
/// \param OldLI Original load that carried the metadata.
/// \param N Range metadata node to copy or remap.
/// \param NewLI Replacement load that receives the metadata.
LLVM_ABI void copyRangeMetadata(const DataLayout &DL, const LoadInst &OldLI,
                                MDNode *N, LoadInst &NewLI);

/// Remove debug intrinsic instructions for a given instruction.
///
/// \param I Instruction whose debug users should be dropped.
LLVM_ABI void dropDebugUsers(Instruction &I);

/// Hoist all instructions from a block into a dominating insertion point.
///
/// Hoist all of the instructions in the \p IfBlock to the dominant block \p
/// DomBlock, by moving its instructions to the insertion point \p InsertPt.
///
/// The moved instructions receive the insertion point debug location values
/// (DILocations) and their debug intrinsic instructions are removed.
///
/// \param DomBlock Dominating block that receives the hoisted instructions.
/// \param InsertPt Insertion point within \p DomBlock.
/// \param BB Block whose instructions are hoisted.
LLVM_ABI void hoistAllInstructionsInto(BasicBlock *DomBlock,
                                       Instruction *InsertPt, BasicBlock *BB);

/// Create a debug-info expression for a constant.
///
/// \param DIB DIBuilder used to create the expression.
/// \param C Constant described by the expression.
/// \param Ty Type of the constant value.
/// \return DIExpression describing the constant, or nullptr if none.
LLVM_ABI DIExpression *getExpressionForConstant(DIBuilder &DIB,
                                                const Constant &C, Type &Ty);

/// Remap operands of debug records attached to an instruction.
///
/// Remap the operands of the debug records attached to \p Inst, and the
/// operands of \p Inst itself if it's a debug intrinsic.
///
/// \param Mapping Value map used to remap debug operands.
/// \param Inst Instruction whose attached debug records are remapped.
LLVM_ABI void remapDebugVariable(ValueToValueMapTy &Mapping, Instruction *Inst);

//===----------------------------------------------------------------------===//
//  Intrinsic pattern matching
//

/// Match a bswap or bitreverse idiom and replace it with an intrinsic.
///
/// Try to match a bswap or bitreverse idiom.
///
/// If an idiom is matched, an intrinsic call is inserted before \c I. Any added
/// instructions are returned in \c InsertedInsts. They will all have been added
/// to a basic block.
///
/// A bitreverse idiom normally requires around 2*BW nodes to be searched (where
/// BW is the bitwidth of the integer type). A bswap idiom requires anywhere up
/// to BW / 4 nodes to be searched, so is significantly faster.
///
/// This function returns true on a successful match or false otherwise.
///
/// \param I Instruction that may root a bswap or bitreverse idiom.
/// \param MatchBSwaps Whether to match bswap idioms.
/// \param MatchBitReversals Whether to match bitreverse idioms.
/// \param InsertedInsts Instructions inserted for a successful match.
/// \return True if a bswap or bitreverse idiom was matched.
LLVM_ABI bool
recognizeBSwapOrBitReverseIdiom(Instruction *I, bool MatchBSwaps,
                                bool MatchBitReversals,
                                SmallVectorImpl<Instruction *> &InsertedInsts);

//===----------------------------------------------------------------------===//
//  Sanitizer utilities
//

/// Mark known string library calls with NoBuiltin for sanitizer interception.
///
/// Given a CallInst, check if it calls a string function known to CodeGen, and
/// mark it with NoBuiltin if so. To be used by sanitizers that intend to
/// intercept string functions and want to avoid converting them to target
/// specific instructions.
///
/// \param CI Call that may be a known string library function.
/// \param TLI Target library info used to recognize string functions.
LLVM_ABI void
maybeMarkSanitizerLibraryCallNoBuiltin(CallInst *CI,
                                       const TargetLibraryInfo *TLI);

//===----------------------------------------------------------------------===//
//  Transform predicates
//

/// Return true if an operand may legally be replaced with a variable.
///
/// Given an instruction, is it legal to set operand OpIdx to a non-constant
/// value?
///
/// \param I Instruction whose operand is being considered.
/// \param OpIdx Operand index that would receive a variable.
/// \return True if operand \p OpIdx may legally be replaced with a variable.
LLVM_ABI bool canReplaceOperandWithVariable(const Instruction *I,
                                            unsigned OpIdx);

//===----------------------------------------------------------------------===//
//  Value helper functions
//

/// Invert a boolean condition, reusing an existing inverted copy when possible.
///
/// \param Condition True/false value to invert.
/// \return Inverted boolean value, reusing an existing inverted copy when
/// possible.
LLVM_ABI Value *invertCondition(Value *Condition);

//===----------------------------------------------------------------------===//
//  Assorted
//

/// Materialize attributes that can be inferred from others on a function.
///
/// If we can infer one attribute from another on the declaration of a function,
/// explicitly materialize the maximal set in the IR.
///
/// \param F Function whose attributes may be strengthened by inference.
/// \return True if any attributes were inferred and materialized.
LLVM_ABI bool inferAttributesFromOthers(Function &F);

//===----------------------------------------------------------------------===//
//  Helpers to track and update flags on instructions.
//

/// Accumulates no-wrap and related flags across merged instructions.
struct OverflowTracking {
  /// Whether every merged instruction has the nuw flag.
  bool HasNUW = true;
  /// Whether every merged instruction has the nsw flag.
  bool HasNSW = true;
  /// Whether every merged instruction has the disjoint flag.
  bool IsDisjoint = true;

#ifndef NDEBUG
  /// Opcode of merged instructions. All instructions passed to mergeFlags must
  /// have the same opcode.
  std::optional<unsigned> Opcode;
#endif

  // Note: At the moment, users are responsible to manage AllKnownNonNegative
  // and AllKnownNonZero manually. AllKnownNonNegative can be true in a case
  // where one of the operands is negative, but one the operators is not NSW.
  // AllKnownNonNegative should not be used independently of HasNSW
  /// Whether every merged value is known non-negative.
  bool AllKnownNonNegative = true;
  /// Whether every merged value is known non-zero.
  bool AllKnownNonZero = true;

  /// Construct an overflow tracker with optimistic default flags.
  OverflowTracking() = default;

  /// Merge in the no-wrap flags from an instruction.
  ///
  /// \param I Instruction whose flags are merged into this tracker.
  LLVM_ABI void mergeFlags(Instruction &I);

  /// Apply accumulated no-wrap flags to an instruction when applicable.
  ///
  /// \param I Instruction that receives the tracked flags.
  LLVM_ABI void applyFlags(Instruction &I);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOCAL_H
