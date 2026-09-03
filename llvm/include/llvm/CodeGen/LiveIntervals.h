//===- LiveIntervals.h - Live Interval Analysis -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file implements the LiveInterval analysis pass.  Given some
/// numbering of each the machine instructions (in this implemention depth-first
/// order) an interval [i, j) is said to be a live interval for register v if
/// there is no instruction with number j' > j such that v is live at j' and
/// there is no instruction with number i' < i such that v is live at i'. In
/// this implementation intervals can have holes, i.e. an interval might look
/// like [1,20), [50,65), [1000,1001).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVEINTERVALS_H
#define LLVM_CODEGEN_LIVEINTERVALS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervalCalc.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <utility>

namespace llvm {

/// Command-line option to use segment sets when computing physreg unit ranges.
LLVM_ABI extern cl::opt<bool> UseSegmentSetForPhysRegs;

class BitVector;
class MachineBlockFrequencyInfo;
class MachineDominatorTree;
class MachineFunction;
class MachineInstr;
class MachineRegisterInfo;
class ProfileSummaryInfo;
class raw_ostream;
class TargetInstrInfo;
class VirtRegMap;

/// Analysis that computes and maintains live intervals for virtual registers.
class LiveIntervals {
  friend class LiveIntervalsAnalysis;
  friend class LiveIntervalsWrapperPass;

  MachineFunction *MF = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  const TargetInstrInfo *TII = nullptr;
  SlotIndexes *Indexes = nullptr;
  MachineDominatorTree *DomTree = nullptr;
  std::unique_ptr<LiveIntervalCalc> LICalc;

  /// Special pool allocator for VNInfo's (LiveInterval val#).
  VNInfo::Allocator VNInfoAllocator;

  /// Live interval pointers for all the virtual registers.
  IndexedMap<LiveInterval *, VirtReg2IndexFunctor> VirtRegIntervals;

  /// Sorted list of instructions with register mask operands. Always use the
  /// 'r' slot, RegMasks are normal clobbers, not early clobbers.
  SmallVector<SlotIndex, 8> RegMaskSlots;

  /// This vector is parallel to RegMaskSlots, it holds a pointer to the
  /// corresponding register mask.  This pointer can be recomputed as:
  ///
  ///   MI = Indexes->getInstructionFromIndex(RegMaskSlot[N]);
  ///   unsigned OpNum = findRegMaskOperand(MI);
  ///   RegMaskBits[N] = MI->getOperand(OpNum).getRegMask();
  ///
  /// This is kept in a separate vector partly because some standard
  /// libraries don't support lower_bound() with mixed objects, partly to
  /// improve locality when searching in RegMaskSlots.
  /// Also see the comment in LiveInterval::find().
  SmallVector<const uint32_t *, 8> RegMaskBits;

  /// For each basic block number, keep (begin, size) pairs indexing into the
  /// RegMaskSlots and RegMaskBits arrays.
  /// Note that basic block numbers may not be layout contiguous, that's why
  /// we can't just keep track of the first register mask in each basic
  /// block.
  SmallVector<std::pair<unsigned, unsigned>, 8> RegMaskBlocks;

  /// Keeps a live range set for each register unit to track fixed physreg
  /// interference.
  SmallVector<LiveRange *, 0> RegUnitRanges;

  // Can only be created from pass manager.
  LiveIntervals() = default;
  LiveIntervals(MachineFunction &MF, SlotIndexes &SI, MachineDominatorTree &DT)
      : Indexes(&SI), DomTree(&DT) {
    analyze(MF);
  }

  LLVM_ABI void analyze(MachineFunction &MF);

  LLVM_ABI void clear();

public:
  /// Move-construct live intervals from another instance.
  ///
  /// \param Other Instance to move from.
  LiveIntervals(LiveIntervals &&Other) = default;
  /// Destroy the live intervals analysis and free allocated ranges.
  LLVM_ABI ~LiveIntervals();

  /// Invalidate this analysis result when required by the new pass manager.
  ///
  /// \param MF Machine function whose analysis result may be invalidated.
  /// \param PA Set of analyses preserved by the transform.
  /// \param Inv Invalidator for resolving analysis dependencies.
  /// \return True if this result should be discarded.
  LLVM_ABI bool invalidate(MachineFunction &MF, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);

  /// Calculate the spill weight to assign to a single instruction.
  ///
  /// If \p PSI is provided the calculation is altered for optsize functions.
  ///
  /// \param isDef True if the instruction defines the register.
  /// \param isUse True if the instruction uses the register.
  /// \param MBFI Block frequency info used to weight the spill cost.
  /// \param MI Instruction whose spill weight is computed.
  /// \param PSI Optional profile summary used for size-optimized functions.
  /// \return Computed spill weight for the instruction.
  LLVM_ABI static float getSpillWeight(bool isDef, bool isUse,
                                       const MachineBlockFrequencyInfo *MBFI,
                                       const MachineInstr &MI,
                                       ProfileSummaryInfo *PSI = nullptr);

  /// Calculate the spill weight to assign to a basic block.
  ///
  /// If \p PSI is provided the calculation is altered for optsize functions.
  ///
  /// \param isDef True if the register is defined in the block.
  /// \param isUse True if the register is used in the block.
  /// \param MBFI Block frequency info used to weight the spill cost.
  /// \param MBB Basic block whose spill weight is computed.
  /// \param PSI Optional profile summary used for size-optimized functions.
  /// \return Computed spill weight for the basic block.
  LLVM_ABI static float getSpillWeight(bool isDef, bool isUse,
                                       const MachineBlockFrequencyInfo *MBFI,
                                       const MachineBasicBlock *MBB,
                                       ProfileSummaryInfo *PSI = nullptr);

  /// Calculate spill weight for an instruction using a precomputed size flag.
  ///
  /// Variants taking a precomputed \p OptForSize rather than deriving it from a
  /// ProfileSummaryInfo.
  ///
  /// \param isDef True if the instruction defines the register.
  /// \param isUse True if the instruction uses the register.
  /// \param MBFI Block frequency info used to weight the spill cost.
  /// \param MI Instruction whose spill weight is computed.
  /// \param OptForSize True when optimizing for code size.
  /// \return Computed spill weight for the instruction.
  LLVM_ABI static float getSpillWeight(bool isDef, bool isUse,
                                       const MachineBlockFrequencyInfo *MBFI,
                                       const MachineInstr &MI, bool OptForSize);

  /// Calculate spill weight for a basic block using a precomputed size flag.
  ///
  /// \param isDef True if the register is defined in the block.
  /// \param isUse True if the register is used in the block.
  /// \param MBFI Block frequency info used to weight the spill cost.
  /// \param MBB Basic block whose spill weight is computed.
  /// \param OptForSize True when optimizing for code size.
  /// \return Computed spill weight for the basic block.
  LLVM_ABI static float getSpillWeight(bool isDef, bool isUse,
                                       const MachineBlockFrequencyInfo *MBFI,
                                       const MachineBasicBlock *MBB,
                                       bool OptForSize);

  /// Return the live interval for virtual register \p Reg, computing it if
  /// needed.
  ///
  /// \param Reg Virtual register whose interval is requested.
  /// \return Live interval for \p Reg.
  LiveInterval &getInterval(Register Reg) {
    if (hasInterval(Reg))
      return *VirtRegIntervals[Reg.id()];

    return createAndComputeVirtRegInterval(Reg);
  }

  /// Return the live interval for virtual register \p Reg, computing it if
  /// needed.
  ///
  /// \param Reg Virtual register whose interval is requested.
  /// \return Live interval for \p Reg.
  const LiveInterval &getInterval(Register Reg) const {
    return const_cast<LiveIntervals *>(this)->getInterval(Reg);
  }

  /// Return true if a live interval already exists for \p Reg.
  ///
  /// \param Reg Virtual register to query.
  /// \return True if a live interval already exists for \p Reg.
  bool hasInterval(Register Reg) const {
    return VirtRegIntervals.inBounds(Reg.id()) && VirtRegIntervals[Reg.id()];
  }

  /// Create an empty live interval for virtual register \p Reg.
  ///
  /// \param Reg Virtual register that must not already have an interval.
  /// \return Newly created empty live interval for \p Reg.
  LiveInterval &createEmptyInterval(Register Reg) {
    assert(!hasInterval(Reg) && "Interval already exists!");
    VirtRegIntervals.grow(Reg.id());
    auto &Interval = VirtRegIntervals[Reg.id()];
    Interval = createInterval(Reg);
    return *Interval;
  }

  /// Create and compute the live interval for virtual register \p Reg.
  ///
  /// \param Reg Virtual register whose interval is created and computed.
  /// \return Newly created and computed live interval for \p Reg.
  LiveInterval &createAndComputeVirtRegInterval(Register Reg) {
    LiveInterval &LI = createEmptyInterval(Reg);
    computeVirtRegInterval(LI);
    return LI;
  }

  /// Create and compute the live interval for \p Reg, reporting if a split is
  /// needed.
  ///
  /// \param Reg Virtual register whose interval is created and computed.
  /// \param NeedSplit Set to true if the computed interval may need splitting.
  /// \return Newly created and computed live interval for \p Reg.
  LiveInterval &createAndComputeVirtRegInterval(Register Reg, bool &NeedSplit) {
    LiveInterval &LI = createEmptyInterval(Reg);
    NeedSplit = computeVirtRegInterval(LI);
    return LI;
  }

  /// Return an existing interval for \p Reg.
  ///
  /// If \p Reg has no interval then this creates a new empty one instead.
  /// Note: does not trigger interval computation.
  ///
  /// \param Reg Virtual register whose interval is returned or created.
  /// \return Existing or newly created empty live interval for \p Reg.
  LiveInterval &getOrCreateEmptyInterval(Register Reg) {
    return hasInterval(Reg) ? getInterval(Reg) : createEmptyInterval(Reg);
  }

  /// Remove and delete the live interval for virtual register \p Reg.
  ///
  /// \param Reg Virtual register whose interval is removed.
  void removeInterval(Register Reg) {
    auto &Interval = VirtRegIntervals[Reg];
    delete Interval;
    Interval = nullptr;
  }

  /// Given a register and an instruction, adds a live segment from that
  /// instruction to the end of its MBB.
  ///
  /// \param Reg Virtual register receiving the new live segment.
  /// \param startInst Instruction at which the live segment begins.
  /// \return The live segment added from \p startInst to the end of its MBB.
  LLVM_ABI LiveInterval::Segment
  addSegmentToEndOfBlock(Register Reg, MachineInstr &startInst);

  /// Shrink a live interval to its remaining uses after some uses were removed.
  ///
  /// After removing some uses of a register, shrink its live range to just
  /// the remaining uses. This method does not compute reaching defs for new
  /// uses, and it doesn't remove dead defs.
  /// Dead PHIDef values are marked as unused. New dead machine instructions
  /// are added to the dead vector. Returns true if the interval may have been
  /// separated into multiple connected components.
  ///
  /// \param li Live interval to shrink.
  /// \param dead Optional list that receives newly dead instructions.
  /// \return True if \p li may have been separated into connected components.
  LLVM_ABI bool shrinkToUses(LiveInterval *li,
                             SmallVectorImpl<MachineInstr *> *dead = nullptr);

  /// Shrink a subregister live range to uses matching its lane mask.
  ///
  /// Specialized version of
  /// shrinkToUses(LiveInterval *li, SmallVectorImpl<MachineInstr*> *dead)
  /// that works on a subregister live range and only looks at uses matching
  /// the lane mask of the subregister range.
  /// This may leave the subrange empty which needs to be cleaned up with
  /// LiveInterval::removeEmptySubranges() afterwards.
  ///
  /// \param SR Subregister live range to shrink.
  /// \param Reg Virtual register that owns \p SR.
  LLVM_ABI void shrinkToUses(LiveInterval::SubRange &SR, Register Reg);

  /// Extend live range \p LR to reach all points in \p Indices.
  ///
  /// The points in the \p Indices array must be jointly dominated by the union
  /// of the existing defs in \p LR and points in \p Undefs.
  ///
  /// PHI-defs are added as needed to maintain SSA form.
  ///
  /// If a SlotIndex in \p Indices is the end index of a basic block, \p LR
  /// will be extended to be live out of the basic block.
  /// If a SlotIndex in \p Indices is jointy dominated only by points in
  /// \p Undefs, the live range will not be extended to that point.
  ///
  /// See also LiveRangeCalc::extend().
  ///
  /// \param LR Live range to extend.
  /// \param Indices Slot indexes that \p LR must reach.
  /// \param Undefs Locations where \p LR becomes undefined.
  LLVM_ABI void extendToIndices(LiveRange &LR, ArrayRef<SlotIndex> Indices,
                                ArrayRef<SlotIndex> Undefs);

  /// Extend live range \p LR to reach all points in \p Indices.
  ///
  /// The points in the \p Indices array must be jointly dominated by the union
  /// of the existing defs in \p LR and points in \p Undefs.
  ///
  /// \param LR Live range to extend.
  /// \param Indices Slot indexes that \p LR must reach.
  void extendToIndices(LiveRange &LR, ArrayRef<SlotIndex> Indices) {
    extendToIndices(LR, Indices, /*Undefs=*/{});
  }

  /// Prune live range \p LR of any liveness reachable from \p Kill.
  ///
  /// If \p LR has a live value at \p Kill, prune its live range by removing
  /// any liveness reachable from Kill. Add live range end points to
  /// EndPoints such that extendToIndices(LI, EndPoints) will reconstruct the
  /// value's live range.
  ///
  /// Calling pruneValue() and extendToIndices() can be used to reconstruct
  /// SSA form after adding defs to a virtual register.
  ///
  /// \param LR Live range to prune.
  /// \param Kill Slot index from which reachable liveness is removed.
  /// \param EndPoints Optional list that receives live-range end points.
  LLVM_ABI void pruneValue(LiveRange &LR, SlotIndex Kill,
                           SmallVectorImpl<SlotIndex> *EndPoints);

  /// Deleted overload that rejects pruning a LiveInterval directly.
  ///
  /// This function should not be used. Its intent is to tell you that you are
  /// doing something wrong if you call pruneValue directly on a
  /// LiveInterval. Indeed, you are supposed to call pruneValue on the main
  /// LiveRange and all the LiveRanges of the subranges if any.
  ///
  /// \param LI Live interval that must not be pruned directly.
  /// \param Kill Unused; present only to match the LiveRange overload.
  /// \param EndPoints Unused; present only to match the LiveRange overload.
  [[maybe_unused]] void pruneValue(LiveInterval &LI, SlotIndex Kill,
                                   SmallVectorImpl<SlotIndex> *EndPoints) {
    llvm_unreachable(
        "Use pruneValue on the main LiveRange and on each subrange");
  }

  /// Return the SlotIndexes analysis used by this LiveIntervals instance.
  ///
  /// \return Pointer to the SlotIndexes analysis, or nullptr if unset.
  SlotIndexes *getSlotIndexes() const { return Indexes; }

  /// Returns true if the specified machine instr has been removed or was
  /// never entered in the map.
  ///
  /// \param Instr Instruction to query in the SlotIndexes map.
  /// \return True if \p Instr is absent from the SlotIndexes map.
  bool isNotInMIMap(const MachineInstr &Instr) const {
    return !Indexes->hasIndex(Instr);
  }

  /// Returns the base index of the given instruction.
  ///
  /// \param Instr Instruction whose SlotIndex is requested.
  /// \return Base SlotIndex of \p Instr.
  SlotIndex getInstructionIndex(const MachineInstr &Instr) const {
    return Indexes->getInstructionIndex(Instr);
  }

  /// Returns the instruction associated with the given index.
  ///
  /// \param index Slot index whose instruction is requested.
  /// \return Instruction at \p index, or nullptr if none.
  MachineInstr *getInstructionFromIndex(SlotIndex index) const {
    return Indexes->getInstructionFromIndex(index);
  }

  /// Return the first index in the given basic block.
  ///
  /// \param mbb Basic block whose start SlotIndex is requested.
  /// \return First SlotIndex in \p mbb.
  SlotIndex getMBBStartIdx(const MachineBasicBlock *mbb) const {
    return Indexes->getMBBStartIdx(mbb);
  }

  /// Return the last index in the given basic block.
  ///
  /// \param mbb Basic block whose end SlotIndex is requested.
  /// \return Last SlotIndex in \p mbb.
  SlotIndex getMBBEndIdx(const MachineBasicBlock *mbb) const {
    return Indexes->getMBBEndIdx(mbb);
  }

  /// Return true if \p LR is live into basic block \p mbb.
  ///
  /// \param LR Live range to query.
  /// \param mbb Basic block whose live-in status is tested.
  /// \return True if \p LR is live into \p mbb.
  bool isLiveInToMBB(const LiveRange &LR, const MachineBasicBlock *mbb) const {
    return LR.liveAt(getMBBStartIdx(mbb));
  }

  /// Return true if \p LR is live out of basic block \p mbb.
  ///
  /// \param LR Live range to query.
  /// \param mbb Basic block whose live-out status is tested.
  /// \return True if \p LR is live out of \p mbb.
  bool isLiveOutOfMBB(const LiveRange &LR, const MachineBasicBlock *mbb) const {
    return LR.liveAt(getMBBEndIdx(mbb).getPrevSlot());
  }

  /// Return the basic block that contains SlotIndex \p index.
  ///
  /// \param index Slot index whose basic block is requested.
  /// \return Basic block containing \p index.
  MachineBasicBlock *getMBBFromIndex(SlotIndex index) const {
    return Indexes->getMBBFromIndex(index);
  }

  /// Adds an empty block \p MBB to the SlotIndexes and regmask maps.
  ///
  /// \param MBB Newly inserted empty basic block to register in the maps.
  void insertMBBInMaps(MachineBasicBlock *MBB) {
    insertMBBInMapsImpl(MBB, /*AssumeRegMaskEmpty=*/true);
  }

  /// After the tail of \p Orig has been sliced into \p SplitBB, updates the
  /// SlotIndexes and regmask maps and re-slices \p Orig's regmask table across
  /// the two blocks.
  ///
  /// \param Orig Original basic block whose tail was sliced.
  /// \param SplitBB New block that received the sliced tail of \p Orig.
  void splitAt(MachineBasicBlock &Orig, MachineBasicBlock &SplitBB) {
    insertMBBInMapsImpl(&SplitBB, /*AssumeRegMaskEmpty=*/false);
    reassignRegMaskSlots(Orig, SplitBB);
  }

  /// Insert machine instruction \p MI into the SlotIndexes map.
  ///
  /// \param MI Instruction to insert into the SlotIndexes map.
  /// \return Slot index assigned to \p MI.
  SlotIndex InsertMachineInstrInMaps(MachineInstr &MI) {
    return Indexes->insertMachineInstrInMaps(MI);
  }

  /// Insert the instructions in range [\p B, \p E) into the SlotIndexes map.
  ///
  /// \param B Begin iterator of the instruction range to insert.
  /// \param E End iterator of the instruction range to insert.
  void InsertMachineInstrRangeInMaps(MachineBasicBlock::iterator B,
                                     MachineBasicBlock::iterator E) {
    for (MachineBasicBlock::iterator I = B; I != E; ++I)
      Indexes->insertMachineInstrInMaps(*I);
  }

  /// Remove machine instruction \p MI from the SlotIndexes map.
  ///
  /// \param MI Instruction to remove from the SlotIndexes map.
  void RemoveMachineInstrFromMaps(MachineInstr &MI) {
    Indexes->removeMachineInstrFromMaps(MI);
  }

  /// Replace \p MI with \p NewMI in the SlotIndexes map.
  ///
  /// \param MI Instruction currently mapped in SlotIndexes.
  /// \param NewMI Instruction that takes \p MI's place in the map.
  /// \return Slot index now associated with \p NewMI.
  SlotIndex ReplaceMachineInstrInMaps(MachineInstr &MI, MachineInstr &NewMI) {
    return Indexes->replaceMachineInstrInMaps(MI, NewMI);
  }

  /// Return the allocator used for VNInfo value numbers.
  ///
  /// \return Allocator used to allocate VNInfo value numbers.
  VNInfo::Allocator &getVNInfoAllocator() { return VNInfoAllocator; }

  /// Print the live intervals analysis results.
  ///
  /// \param O Output stream for the dump.
  LLVM_ABI void print(raw_ostream &O) const;
  /// Dump the live intervals analysis results to dbgs().
  LLVM_ABI void dump() const;

  /// Clear and recompute live intervals for machine function \p MF.
  ///
  /// For legacy pass to recompute liveness.
  ///
  /// \param MF Machine function whose live intervals are recomputed.
  void reanalyze(MachineFunction &MF) {
    clear();
    analyze(MF);
  }

  /// Return the machine dominator tree used by this analysis.
  ///
  /// \return Machine dominator tree associated with this LiveIntervals instance.
  MachineDominatorTree &getDomTree() { return *DomTree; }

  /// If LI is confined to a single basic block, return a pointer to that
  /// block.  If LI is live in to or out of any block, return NULL.
  ///
  /// \param LI Live interval whose block confinement is tested.
  /// \return The single basic block containing \p LI, or nullptr if not confined.
  LLVM_ABI MachineBasicBlock *intervalIsInOneMBB(const LiveInterval &LI) const;

  /// Returns true if VNI is killed by any PHI-def values in LI.
  ///
  /// This may conservatively return true to avoid expensive computations.
  ///
  /// \param LI Live interval that may contain PHI kills of \p VNI.
  /// \param VNI Value number tested for a PHI kill.
  /// \return True if \p VNI may be killed by a PHI-def in \p LI.
  LLVM_ABI bool hasPHIKill(const LiveInterval &LI, const VNInfo *VNI) const;

  /// Add kill flags to any instruction that kills a virtual register.
  ///
  /// \param VRM Virtual register map used when placing kill flags.
  LLVM_ABI void addKillFlags(const VirtRegMap *VRM);

  /// Notify LiveIntervals that instruction \p MI moved within a basic block.
  ///
  /// Call this method to notify LiveIntervals that instruction \p MI has been
  /// moved within a basic block. This will update the live intervals for all
  /// operands of \p MI. Moves between basic blocks are not supported.
  ///
  /// \param MI Instruction that was moved within its basic block.
  /// \param UpdateFlags Update live intervals for nonallocatable physregs.
  LLVM_ABI void handleMove(MachineInstr &MI, bool UpdateFlags = false);

  /// Update intervals of operands of all instructions in the newly
  /// created bundle specified by \p BundleStart.
  ///
  /// Assumes existing liveness is accurate.
  /// \pre BundleStart should be the first instruction in the Bundle.
  /// \pre BundleStart should not have a have SlotIndex as one will be assigned.
  ///
  /// \param BundleStart First instruction in the newly created bundle.
  /// \param UpdateFlags Update live intervals for nonallocatable physregs.
  LLVM_ABI void handleMoveIntoNewBundle(MachineInstr &BundleStart,
                                        bool UpdateFlags = false);

  /// Update live intervals for instructions in an iterator range.
  ///
  /// Update live intervals for instructions in a range of iterators. It is
  /// intended for use after target hooks that may insert or remove
  /// instructions, and is only efficient for a small number of instructions.
  ///
  /// OrigRegs is a vector of registers that were originally used by the
  /// instructions in the range between the two iterators.
  ///
  /// Currently, the only changes that are supported are simple removal
  /// and addition of uses.
  ///
  /// \param MBB Basic block containing the repaired instruction range.
  /// \param Begin Begin iterator of the repaired instruction range.
  /// \param End End iterator of the repaired instruction range.
  /// \param OrigRegs Registers originally used by instructions in the range.
  LLVM_ABI void repairIntervalsInRange(MachineBasicBlock *MBB,
                                       MachineBasicBlock::iterator Begin,
                                       MachineBasicBlock::iterator End,
                                       ArrayRef<Register> OrigRegs);

  // Register mask functions.
  //
  // Machine instructions may use a register mask operand to indicate that a
  // large number of registers are clobbered by the instruction.  This is
  // typically used for calls.
  //
  // For compile time performance reasons, these clobbers are not recorded in
  // the live intervals for individual physical registers.  Instead,
  // LiveIntervalAnalysis maintains a sorted list of instructions with
  // register mask operands.

  /// Returns a sorted array of slot indices of all instructions with
  /// register mask operands.
  ///
  /// \return Sorted slot indices of all register-mask instructions.
  ArrayRef<SlotIndex> getRegMaskSlots() const { return RegMaskSlots; }

  /// Returns a sorted array of slot indices of all instructions with register
  /// mask operands in the basic block numbered \p MBBNum.
  ///
  /// \param MBBNum Basic block number whose register-mask slots are returned.
  /// \return Sorted slot indices of register-mask instructions in block \p MBBNum.
  ArrayRef<SlotIndex> getRegMaskSlotsInBlock(unsigned MBBNum) const {
    std::pair<unsigned, unsigned> P = RegMaskBlocks[MBBNum];
    return getRegMaskSlots().slice(P.first, P.second);
  }

  /// Returns an array of register mask pointers corresponding to
  /// getRegMaskSlots().
  ///
  /// \return Register mask bit pointers parallel to getRegMaskSlots().
  ArrayRef<const uint32_t *> getRegMaskBits() const { return RegMaskBits; }

  /// Returns an array of mask pointers corresponding to
  /// getRegMaskSlotsInBlock(MBBNum).
  ///
  /// \param MBBNum Basic block number whose register-mask bits are returned.
  /// \return Register mask bit pointers for instructions in block \p MBBNum.
  ArrayRef<const uint32_t *> getRegMaskBitsInBlock(unsigned MBBNum) const {
    std::pair<unsigned, unsigned> P = RegMaskBlocks[MBBNum];
    return getRegMaskBits().slice(P.first, P.second);
  }

  /// Test if \p LI is live across any register mask instructions, and
  /// compute a bit mask of physical registers that are not clobbered by any
  /// of them.
  ///
  /// Returns false if \p LI doesn't cross any register mask instructions. In
  /// that case, the bit vector is not filled in.
  ///
  /// \param LI Live interval tested for register-mask interference.
  /// \param UsableRegs Bit vector filled with registers not clobbered by any
  ///        crossed register mask.
  /// \return True if \p LI crosses any register mask instructions.
  LLVM_ABI bool checkRegMaskInterference(const LiveInterval &LI,
                                         BitVector &UsableRegs);

  // Register unit functions.
  //
  // Fixed interference occurs when MachineInstrs use physregs directly
  // instead of virtual registers. This typically happens when passing
  // arguments to a function call, or when instructions require operands in
  // fixed registers.
  //
  // Each physreg has one or more register units, see MCRegisterInfo. We
  // track liveness per register unit to handle aliasing registers more
  // efficiently.

  /// Return the live range for register unit \p Unit. It will be computed if
  /// it doesn't exist.
  ///
  /// \param Unit Register unit whose live range is returned or computed.
  /// \return Live range for \p Unit, computed on demand if necessary.
  LiveRange &getRegUnit(MCRegUnit Unit) {
    LiveRange *LR = RegUnitRanges[static_cast<unsigned>(Unit)];
    if (!LR) {
      // Compute missing ranges on demand.
      // Use segment set to speed-up initial computation of the live range.
      RegUnitRanges[static_cast<unsigned>(Unit)] = LR =
          new LiveRange(UseSegmentSetForPhysRegs);
      computeRegUnitRange(*LR, Unit);
    }
    return *LR;
  }

  /// Return the live range for register unit \p Unit if it has already been
  /// computed, or nullptr if it hasn't been computed yet.
  ///
  /// \param Unit Register unit whose cached live range is requested.
  /// \return Cached live range for \p Unit, or nullptr if not yet computed.
  LiveRange *getCachedRegUnit(MCRegUnit Unit) {
    return RegUnitRanges[static_cast<unsigned>(Unit)];
  }

  /// Return the live range for register unit \p Unit if it has already been
  /// computed, or nullptr if it hasn't been computed yet.
  ///
  /// \param Unit Register unit whose cached live range is requested.
  /// \return Cached live range for \p Unit, or nullptr if not yet computed.
  const LiveRange *getCachedRegUnit(MCRegUnit Unit) const {
    return RegUnitRanges[static_cast<unsigned>(Unit)];
  }

  /// Remove computed live range for register unit \p Unit. Subsequent uses
  /// should rely on on-demand recomputation.
  ///
  /// \param Unit Register unit whose cached live range is discarded.
  void removeRegUnit(MCRegUnit Unit) {
    delete RegUnitRanges[static_cast<unsigned>(Unit)];
    RegUnitRanges[static_cast<unsigned>(Unit)] = nullptr;
  }

  /// Remove associated live ranges for the register units associated with \p
  /// Reg. Subsequent uses should rely on on-demand recomputation.  \note This
  /// method can result in inconsistent liveness tracking if multiple phyical
  /// registers share a regunit, and should be used cautiously.
  ///
  /// \param Reg Physical register whose register-unit live ranges are cleared.
  void removeAllRegUnitsForPhysReg(MCRegister Reg) {
    for (MCRegUnit Unit : TRI->regunits(Reg))
      removeRegUnit(Unit);
  }

  /// Remove value numbers and related live segments starting at position
  /// \p Pos that are part of any liverange of physical register \p Reg or one
  /// of its subregisters.
  ///
  /// \param Reg Physical register whose def at \p Pos is removed.
  /// \param Pos Slot index of the def to remove.
  LLVM_ABI void removePhysRegDefAt(MCRegister Reg, SlotIndex Pos);

  /// Remove value number and related live segments of \p LI and its subranges
  /// that start at position \p Pos.
  ///
  /// \param LI Live interval whose def at \p Pos is removed.
  /// \param Pos Slot index of the def to remove.
  LLVM_ABI void removeVRegDefAt(LiveInterval &LI, SlotIndex Pos);

  /// Split separate components in LiveInterval \p LI into separate intervals.
  ///
  /// \param LI Live interval whose connected components are split.
  /// \param SplitLIs Receives the newly created component intervals.
  LLVM_ABI void
  splitSeparateComponents(LiveInterval &LI,
                          SmallVectorImpl<LiveInterval *> &SplitLIs);

  /// Construct the main live range of \p LI from its SubRanges.
  ///
  /// For live interval \p LI with correct SubRanges construct matching
  /// information for the main live range. Expects the main live range to not
  /// have any segments or value numbers.
  ///
  /// \param LI Live interval whose main range is built from its subranges.
  LLVM_ABI void constructMainRangeFromSubranges(LiveInterval &LI);

private:
  /// Compute live intervals for all virtual registers.
  void computeVirtRegs();

  /// Compute RegMaskSlots and RegMaskBits.
  void computeRegMasks();

  /// Implementation of insertMBBInMaps(). \p MBB must contain no regmask
  /// operands when \p AssumeRegMaskEmpty is true.
  void insertMBBInMapsImpl(MachineBasicBlock *MBB, bool AssumeRegMaskEmpty);

  /// Updates the regmask table for \p Orig's instructions that are moved into
  /// \p SplitBB, so that the table is sliced across both blocks.
  void reassignRegMaskSlots(MachineBasicBlock &Orig,
                            MachineBasicBlock &SplitBB);

  /// Walk the values in \p LI and check for dead values:
  /// - Dead PHIDef values are marked as unused.
  /// - Dead operands are marked as such.
  /// - Completely dead machine instructions are added to the \p dead vector
  ///   if it is not nullptr.
  /// Returns true if any PHI value numbers have been removed which may
  /// have separated the interval into multiple connected components.
  bool computeDeadValues(LiveInterval &LI,
                         SmallVectorImpl<MachineInstr *> *dead);

  LLVM_ABI static LiveInterval *createInterval(Register Reg);

  void printInstrs(raw_ostream &O) const;
  void dumpInstrs() const;

  void computeLiveInRegUnits();
  LLVM_ABI void computeRegUnitRange(LiveRange &, MCRegUnit Unit);
  LLVM_ABI bool computeVirtRegInterval(LiveInterval &);

  using ShrinkToUsesWorkList = SmallVector<std::pair<SlotIndex, VNInfo *>, 16>;
  void extendSegmentsToUses(LiveRange &Segments, ShrinkToUsesWorkList &WorkList,
                            Register Reg, LaneBitmask LaneMask);

  /// Helper function for repairIntervalsInRange(), walks backwards and
  /// creates/modifies live segments in \p LR to match the operands found.
  /// Only full operands or operands with subregisters matching \p LaneMask
  /// are considered.
  void repairOldRegInRange(MachineBasicBlock::iterator Begin,
                           MachineBasicBlock::iterator End,
                           const SlotIndex endIdx, LiveRange &LR, Register Reg,
                           LaneBitmask LaneMask = LaneBitmask::getAll());

  class HMEditor;
};

/// Analysis pass that computes LiveIntervals for a MachineFunction.
class LiveIntervalsAnalysis : public AnalysisInfoMixin<LiveIntervalsAnalysis> {
  friend AnalysisInfoMixin<LiveIntervalsAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = LiveIntervals;
  /// Run the LiveIntervals analysis on \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MFAM Analysis manager for the machine function.
  /// \return Computed LiveIntervals for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// Printer pass for the \c LiveIntervalsAnalysis results.
class LiveIntervalsPrinterPass
    : public RequiredPassInfoMixin<LiveIntervalsPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes LiveIntervals to \p OS.
  ///
  /// \param OS Output stream for the printed live intervals.
  explicit LiveIntervalsPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print LiveIntervals for \p MF and preserve all analyses.
  ///
  /// \param MF Machine function whose live intervals are printed.
  /// \param MFAM Analysis manager providing LiveIntervalsAnalysis.
  /// \return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// Legacy MachineFunctionPass wrapper around LiveIntervals.
class LLVM_ABI LiveIntervalsWrapperPass : public MachineFunctionPass {
  LiveIntervals LIS;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy LiveIntervals wrapper pass.
  LiveIntervalsWrapperPass();

  /// Declare analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Release memory held by the LiveIntervals analysis.
  void releaseMemory() override { LIS.clear(); }

  /// Pass entry point; Calculates LiveIntervals.
  ///
  /// \param MF Machine function to analyze.
  /// \return False; this pass does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &MF) override;

  /// Implement the dump method.
  ///
  /// \param O Output stream for the dump.
  /// \param M Optional module; unused by this pass.
  void print(raw_ostream &O, const Module *M = nullptr) const override {
    LIS.print(O);
  }

  /// Return the LiveIntervals analysis result.
  ///
  /// \return Reference to the LiveIntervals analysis owned by this pass.
  LiveIntervals &getLIS() { return LIS; }
};

} // end namespace llvm

#endif
