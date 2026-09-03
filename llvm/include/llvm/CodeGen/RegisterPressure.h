//===- RegisterPressure.h - Dynamic Register Pressure -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the RegisterPressure class which can be used to track
// MachineInstr level register pressure.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTERPRESSURE_H
#define LLVM_CODEGEN_REGISTERPRESSURE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace llvm {

class LiveIntervals;
class MachineFunction;
class MachineInstr;
class MachineRegisterInfo;
class RegisterClassInfo;

/// Virtual register or register unit paired with a lane mask.
struct VRegMaskOrUnit {
  /// Virtual register or physical register unit.
  VirtRegOrUnit VRegOrUnit;
  /// Lanes of the virtual register or register unit.
  LaneBitmask LaneMask;

  /// Construct a register-or-unit and lane-mask pair.
  ///
  /// \param VRegOrUnit Virtual register or physical register unit.
  /// \param LaneMask Lanes associated with \p VRegOrUnit.
  VRegMaskOrUnit(VirtRegOrUnit VRegOrUnit, LaneBitmask LaneMask)
      : VRegOrUnit(VRegOrUnit), LaneMask(LaneMask) {}
};

/// Base class for register pressure results.
struct RegisterPressure {
  /// Map of max reg pressure indexed by pressure set ID, not class ID.
  std::vector<unsigned> MaxSetPressure;

  /// List of live in virtual registers or physical register units.
  SmallVector<VRegMaskOrUnit, 8> LiveInRegs;
  /// List of live-out virtual registers or physical register units.
  SmallVector<VRegMaskOrUnit, 8> LiveOutRegs;

  /// Dump max pressure and live in/out registers to the debug stream.
  ///
  /// \param TRI Target register info used to print register names.
  LLVM_ABI void dump(const TargetRegisterInfo *TRI) const;
};

/// Register pressure computed over a SlotIndex-delimited instruction region.
///
/// During pressure computation, the maximum pressure per register pressure set
/// is increased. Once pressure within a region is fully computed, the live-in
/// and live-out sets are recorded.
///
/// This is preferable to RegionPressure when LiveIntervals are available,
/// because delimiting regions by SlotIndex is more robust and convenient than
/// holding block iterators. The block contents can change without invalidating
/// the pressure result.
struct IntervalPressure : RegisterPressure {
  /// Record the boundary of the region being tracked.
  SlotIndex TopIdx;
  /// Slot index of the bottom of the tracked region.
  SlotIndex BottomIdx;

  /// Clear this result so it can be used for another round of tracking.
  LLVM_ABI void reset();

  /// Open the top of the region if it is not at or before \p NextTop.
  ///
  /// \param NextTop Slot index of the next top instruction.
  LLVM_ABI void openTop(SlotIndex NextTop);

  /// Open the bottom of the region if it is not after \p PrevBottom.
  ///
  /// \param PrevBottom Slot index of the previous bottom instruction.
  LLVM_ABI void openBottom(SlotIndex PrevBottom);
};

/// Register pressure computed over an iterator-delimited instruction region.
///
/// This is a less precise version of IntervalPressure for use when
/// LiveIntervals are unavailable.
struct RegionPressure : RegisterPressure {
  /// Record the boundary of the region being tracked.
  MachineBasicBlock::const_iterator TopPos;
  /// Block iterator at the bottom of the tracked region.
  MachineBasicBlock::const_iterator BottomPos;

  /// Clear this result so it can be used for another round of tracking.
  LLVM_ABI void reset();

  /// Open the top of the region if it currently equals \p PrevTop.
  ///
  /// \param PrevTop Iterator to the previous top instruction.
  LLVM_ABI void openTop(MachineBasicBlock::const_iterator PrevTop);

  /// Open the bottom of the region if it currently equals \p PrevBottom.
  ///
  /// \param PrevBottom Iterator to the previous bottom instruction.
  LLVM_ABI void openBottom(MachineBasicBlock::const_iterator PrevBottom);
};

/// Change in pressure for a single pressure set.
///
/// UnitInc may be expressed in terms of upward or downward pressure depending
/// on the client and will be dynamically adjusted for current liveness.
///
/// Pressure increments are tiny, typically 1-2 units, and this is only for
/// heuristics, so we don't check UnitInc overflow. Instead, we may have a
/// higher level assert that pressure is consistent within a region. We also
/// effectively ignore dead defs which don't affect heuristics much.
class PressureChange {
  uint16_t PSetID = 0; // ID+1. 0=Invalid.
  int16_t UnitInc = 0;

public:
  /// Construct an invalid pressure change.
  PressureChange() = default;
  /// Construct a pressure change for pressure set \p id.
  ///
  /// \param id Pressure set identifier.
  PressureChange(unsigned id): PSetID(id + 1) {
    assert(id < std::numeric_limits<uint16_t>::max() && "PSetID overflow.");
  }

  /// Return true if this pressure change refers to a valid pressure set.
  ///
  /// \return True if this pressure change refers to a valid pressure set.
  bool isValid() const { return PSetID > 0; }

  /// Return the pressure set identifier.
  ///
  /// \return Pressure set identifier for this change.
  unsigned getPSet() const {
    assert(isValid() && "invalid PressureChange");
    return PSetID - 1;
  }

  /// Return the pressure set ID, or UINT16_MAX if this change is invalid.
  ///
  /// If PSetID is invalid, return UINT16_MAX to give it lowest priority.
  ///
  /// \return Pressure set ID, or UINT16_MAX if this change is invalid.
  unsigned getPSetOrMax() const {
    return (PSetID - 1) & std::numeric_limits<uint16_t>::max();
  }

  /// Return the pressure increment in register units.
  ///
  /// \return Pressure increment in register units.
  int getUnitInc() const { return UnitInc; }

  /// Set the pressure increment in register units.
  ///
  /// \param Inc New unit increment.
  void setUnitInc(int Inc) { UnitInc = Inc; }

  /// Return true if this pressure change equals \p RHS.
  ///
  /// \param RHS Pressure change to compare against.
  /// \return True if this pressure change equals \p RHS.
  bool operator==(const PressureChange &RHS) const {
    return PSetID == RHS.PSetID && UnitInc == RHS.UnitInc;
  }

  /// Dump this pressure change to the debug stream.
  LLVM_ABI void dump() const;
};

/// List of PressureChanges in order of increasing, unique PSetID.
///
/// Use a small fixed number, because we can fit more PressureChanges in an
/// empty SmallVector than ever need to be tracked per register class. If more
/// PSets are affected, then we only track the most constrained.
class PressureDiff {
  // The initial design was for MaxPSets=4, but that requires PSet partitions,
  // which are not yet implemented. (PSet partitions are equivalent PSets given
  // the register classes actually in use within the scheduling region.)
  enum { MaxPSets = 16 };

  PressureChange PressureChanges[MaxPSets];

  using iterator = PressureChange *;

  iterator nonconst_begin() { return &PressureChanges[0]; }
  iterator nonconst_end() { return &PressureChanges[MaxPSets]; }

public:
  /// Const iterator over this difference's pressure changes.
  using const_iterator = const PressureChange *;

  /// Iterator to the first pressure change.
  ///
  /// \return Const iterator to the first pressure change.
  const_iterator begin() const { return &PressureChanges[0]; }
  /// Iterator past the last pressure change slot.
  ///
  /// \return Const iterator past the last pressure change slot.
  const_iterator end() const { return &PressureChanges[MaxPSets]; }

  /// Add a pressure change for \p VRegOrUnit to this difference.
  ///
  /// \param VRegOrUnit Virtual register or register unit that changed.
  /// \param IsDec True if pressure decreases, false if it increases.
  /// \param MRI Machine register info used to look up pressure sets.
  LLVM_ABI void addPressureChange(VirtRegOrUnit VRegOrUnit, bool IsDec,
                                  const MachineRegisterInfo *MRI);

  /// Dump this pressure difference to the debug stream.
  ///
  /// \param TRI Target register info used to print pressure set names.
  LLVM_ABI void dump(const TargetRegisterInfo &TRI) const;
};

/// List of registers defined and used by a machine instruction.
class RegisterOperands {
public:
  /// List of virtual registers and register units read by the instruction.
  SmallVector<VRegMaskOrUnit, 8> Uses;
  /// List of virtual registers and register units defined by the
  /// instruction which are not dead.
  SmallVector<VRegMaskOrUnit, 8> Defs;
  /// List of virtual registers and register units defined by the
  /// instruction but dead.
  SmallVector<VRegMaskOrUnit, 8> DeadDefs;

  /// Analyze the given instruction \p MI and fill in the Uses, Defs and
  /// DeadDefs list based on the MachineOperand flags.
  ///
  /// \param MI Instruction whose operands are analyzed.
  /// \param TRI Target register info used to expand physical registers.
  /// \param MRI Machine register info used for lane masks.
  /// \param TrackLaneMasks True to collect per-lane masks instead of whole
  ///        regs.
  /// \param IgnoreDead True to omit dead definitions from DeadDefs.
  LLVM_ABI void collect(const MachineInstr &MI, const TargetRegisterInfo &TRI,
                        const MachineRegisterInfo &MRI, bool TrackLaneMasks,
                        bool IgnoreDead);

  /// Use liveness information to find dead defs not marked with a dead flag
  /// and move them to the DeadDefs vector.
  ///
  /// \param MI Instruction whose defs are checked for deadness.
  /// \param LIS Live intervals used to query dead defs.
  LLVM_ABI void detectDeadDefs(const MachineInstr &MI,
                               const LiveIntervals &LIS);

  /// Use liveness information to find out which uses/defs are partially
  /// undefined/dead at \p Pos and adjust the VRegMaskOrUnits accordingly.
  ///
  /// \param LIS Live intervals used to query lane liveness.
  /// \param MRI Machine register info used for lane queries.
  /// \param Pos Slot index at which lane liveness is queried.
  LLVM_ABI void adjustLaneLiveness(const LiveIntervals &LIS,
                                   const MachineRegisterInfo &MRI,
                                   SlotIndex Pos);

  /// Adjust use and def lane masks at \p MI and add missing flags.
  ///
  /// Use liveness information to find out which uses/defs are partially
  /// undefined/dead at the \p MI's position and adjust the VRegMaskOrUnits
  /// accordingly. Missing read-undef and dead flags are added to \p MI.
  ///
  /// \param LIS Live intervals used to query lane liveness.
  /// \param MRI Machine register info used for lane queries.
  /// \param MI Instruction whose operands and flags are updated.
  LLVM_ABI void adjustLaneLiveness(const LiveIntervals &LIS,
                                   const MachineRegisterInfo &MRI,
                                   MachineInstr &MI);

private:
  /// Adjusts the \p Def based on \p LiveAfterDef. The \p Def is removed from
  /// the Defs vector when no defined lane remains live after the def. Returns a
  /// pointer to the next definition to process in order in the Defs vector.
  VRegMaskOrUnit *adjustDef(VRegMaskOrUnit &Def, LaneBitmask LiveAfterDef);

  /// Use liveness information at \p Pos to adjust the lanemask of all uses.
  void adjustUses(const LiveIntervals &LIS, const MachineRegisterInfo &MRI,
                  SlotIndex Pos);
};

/// Array of PressureDiffs.
class PressureDiffs {
  PressureDiff *PDiffArray = nullptr;
  unsigned Size = 0;
  unsigned Max = 0;

public:
  /// Construct an empty, uninitialized pressure-diff array.
  PressureDiffs() = default;
  /// Deleted copy assignment.
  ///
  /// \param other Source PressureDiffs (unused).
  PressureDiffs &operator=(const PressureDiffs &other) = delete;
  /// Deleted copy constructor.
  ///
  /// \param other Source PressureDiffs (unused).
  PressureDiffs(const PressureDiffs &other) = delete;
  /// Free the allocated pressure-diff array.
  ~PressureDiffs() { free(PDiffArray); }

  /// Reset the number of valid pressure diffs to zero.
  void clear() { Size = 0; }

  /// Allocate or reuse storage for \p N pressure diffs.
  ///
  /// \param N Number of pressure diffs to store.
  LLVM_ABI void init(unsigned N);

  /// Return the pressure diff at \p Idx.
  ///
  /// \param Idx Index of the requested pressure diff.
  /// \return Mutable pressure diff at \p Idx.
  PressureDiff &operator[](unsigned Idx) {
    assert(Idx < Size && "PressureDiff index out of bounds");
    return PDiffArray[Idx];
  }
  /// Return the pressure diff at \p Idx.
  ///
  /// \param Idx Index of the requested pressure diff.
  /// \return Const pressure diff at \p Idx.
  const PressureDiff &operator[](unsigned Idx) const {
    return const_cast<PressureDiffs*>(this)->operator[](Idx);
  }

  /// Record pressure difference induced by the given operand list to
  /// node with index \p Idx.
  ///
  /// \param Idx Index of the node whose pressure diff is recorded.
  /// \param RegOpers Operand list whose defs and uses induce the diff.
  /// \param MRI Machine register info used to look up pressure sets.
  LLVM_ABI void addInstruction(unsigned Idx, const RegisterOperands &RegOpers,
                               const MachineRegisterInfo &MRI);
};

/// Store the effects of a change in pressure on things that MI scheduler cares
/// about.
///
/// Excess records the value of the largest difference in register units beyond
/// the target's pressure limits across the affected pressure sets, where
/// largest is defined as the absolute value of the difference. Negative
/// ExcessUnits indicates a reduction in pressure that had already exceeded the
/// target's limits.
///
/// CriticalMax records the largest increase in the tracker's max pressure that
/// exceeds the critical limit for some pressure set determined by the client.
///
/// CurrentMax records the largest increase in the tracker's max pressure that
/// exceeds the current limit for some pressure set determined by the client.
struct RegPressureDelta {
  /// Largest pressure-set difference beyond the target's limits.
  PressureChange Excess;
  /// Largest max-pressure increase beyond a client critical limit.
  PressureChange CriticalMax;
  /// Largest max-pressure increase beyond a client current limit.
  PressureChange CurrentMax;

  /// Construct a zero-initialized pressure delta.
  RegPressureDelta() = default;

  /// Return true if this delta equals \p RHS.
  ///
  /// \param RHS Pressure delta to compare against.
  /// \return True if this delta equals \p RHS.
  bool operator==(const RegPressureDelta &RHS) const {
    return Excess == RHS.Excess && CriticalMax == RHS.CriticalMax
      && CurrentMax == RHS.CurrentMax;
  }
  /// Return true if this delta differs from \p RHS.
  ///
  /// \param RHS Pressure delta to compare against.
  /// \return True if this delta differs from \p RHS.
  bool operator!=(const RegPressureDelta &RHS) const {
    return !operator==(RHS);
  }
  /// Dump this pressure delta to the debug stream.
  LLVM_ABI void dump() const;
};

/// A set of live virtual registers and physical register units.
///
/// This is a wrapper around a SparseSet which deals with mapping register unit
/// and virtual register indexes to an index usable by the sparse set.
class LiveRegSet {
private:
  struct IndexMaskPair {
    unsigned Index;
    LaneBitmask LaneMask;

    IndexMaskPair(unsigned Index, LaneBitmask LaneMask)
        : Index(Index), LaneMask(LaneMask) {}

    unsigned getSparseSetIndex() const {
      return Index;
    }
  };

  using RegSet = SparseSet<IndexMaskPair>;
  RegSet Regs;
  unsigned NumRegUnits = 0u;

  unsigned getSparseIndexFromVirtRegOrUnit(VirtRegOrUnit VRegOrUnit) const {
    if (VRegOrUnit.isVirtualReg())
      return VRegOrUnit.asVirtualReg().virtRegIndex() + NumRegUnits;
    assert(static_cast<unsigned>(VRegOrUnit.asMCRegUnit()) < NumRegUnits);
    return static_cast<unsigned>(VRegOrUnit.asMCRegUnit());
  }

  VirtRegOrUnit getVirtRegOrUnitFromSparseIndex(unsigned SparseIndex) const {
    if (SparseIndex >= NumRegUnits)
      return VirtRegOrUnit(Register::index2VirtReg(SparseIndex - NumRegUnits));
    return VirtRegOrUnit(static_cast<MCRegUnit>(SparseIndex));
  }

public:
  /// Remove all registers from the set.
  LLVM_ABI void clear();
  /// Initialize the set for the register universe of \p MRI.
  ///
  /// \param MRI Machine register info that defines the register universe.
  LLVM_ABI void init(const MachineRegisterInfo &MRI);

  /// Return the live lane mask of \p VRegOrUnit, or none if it is not live.
  ///
  /// \param VRegOrUnit Virtual register or register unit to query.
  /// \return Live lane mask of \p VRegOrUnit, or none if it is not live.
  LaneBitmask contains(VirtRegOrUnit VRegOrUnit) const {
    unsigned SparseIndex = getSparseIndexFromVirtRegOrUnit(VRegOrUnit);
    RegSet::const_iterator I = Regs.find(SparseIndex);
    if (I == Regs.end())
      return LaneBitmask::getNone();
    return I->LaneMask;
  }

  /// Mark the \p Pair.LaneMask lanes of \p Pair.Reg as live.
  /// Returns the previously live lanes of \p Pair.Reg.
  ///
  /// \param Pair Register-or-unit and lanes to mark live.
  /// \return Previously live lanes of \p Pair.Reg before the insert.
  LaneBitmask insert(VRegMaskOrUnit Pair) {
    unsigned SparseIndex = getSparseIndexFromVirtRegOrUnit(Pair.VRegOrUnit);
    auto InsertRes = Regs.insert(IndexMaskPair(SparseIndex, Pair.LaneMask));
    if (!InsertRes.second) {
      LaneBitmask PrevMask = InsertRes.first->LaneMask;
      InsertRes.first->LaneMask |= Pair.LaneMask;
      return PrevMask;
    }
    return LaneBitmask::getNone();
  }

  /// Clears the \p Pair.LaneMask lanes of \p Pair.Reg (mark them as dead).
  /// Returns the previously live lanes of \p Pair.Reg.
  ///
  /// \param Pair Register-or-unit and lanes to mark dead.
  /// \return Previously live lanes of \p Pair.Reg before the erase.
  LaneBitmask erase(VRegMaskOrUnit Pair) {
    unsigned SparseIndex = getSparseIndexFromVirtRegOrUnit(Pair.VRegOrUnit);
    RegSet::iterator I = Regs.find(SparseIndex);
    if (I == Regs.end())
      return LaneBitmask::getNone();
    LaneBitmask PrevMask = I->LaneMask;
    I->LaneMask &= ~Pair.LaneMask;
    return PrevMask;
  }

  /// Return the number of entries in the set.
  ///
  /// \return Number of register entries currently in the set.
  size_t size() const {
    return Regs.size();
  }

  /// Append live register-or-unit entries with any live lanes to \p To.
  ///
  /// \param To Vector that receives live register entries.
  void appendTo(SmallVectorImpl<VRegMaskOrUnit> &To) const {
    for (const IndexMaskPair &P : Regs) {
      VirtRegOrUnit VRegOrUnit = getVirtRegOrUnitFromSparseIndex(P.Index);
      if (P.LaneMask.any())
        To.emplace_back(VRegOrUnit, P.LaneMask);
    }
  }
};

/// Tracker for register pressure at a position in the instruction stream.
///
/// Remembers the high water mark within the region traversed. This does not
/// automatically consider live-through ranges. The client may independently
/// adjust for global liveness.
///
/// Each RegPressureTracker only works within a MachineBasicBlock. Pressure can
/// be tracked across a larger region by storing a RegisterPressure result at
/// each block boundary and explicitly adjusting pressure to account for block
/// live-in and live-out register sets.
///
/// RegPressureTracker holds a reference to a RegisterPressure result that it
/// computes incrementally. During downward tracking, P.BottomIdx or P.BottomPos
/// is invalid until it reaches the end of the block or closeRegion() is
/// explicitly called. Similarly, P.TopIdx is invalid during upward
/// tracking. Changing direction has the side effect of closing region, and
/// traversing past TopIdx or BottomIdx reopens it.
class RegPressureTracker {
  const MachineFunction *MF = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  const RegisterClassInfo *RCI = nullptr;
  const MachineRegisterInfo *MRI = nullptr;
  const LiveIntervals *LIS = nullptr;

  /// We currently only allow pressure tracking within a block.
  const MachineBasicBlock *MBB = nullptr;

  /// Track the max pressure within the region traversed so far.
  RegisterPressure &P;

  /// Run in two modes dependending on whether constructed with IntervalPressure
  /// or RegisterPressure. If requireIntervals is false, LIS are ignored.
  bool RequireIntervals;

  /// True if UntiedDefs will be populated.
  bool TrackUntiedDefs = false;

  /// True if lanemasks should be tracked.
  bool TrackLaneMasks = false;

  /// Register pressure corresponds to liveness before this instruction
  /// iterator. It may point to the end of the block or a DebugValue rather than
  /// an instruction.
  MachineBasicBlock::const_iterator CurrPos;

  /// Pressure map indexed by pressure set ID, not class ID.
  std::vector<unsigned> CurrSetPressure;

  /// Set of live registers.
  LiveRegSet LiveRegs;

  /// Set of vreg defs that start a live range.
  SparseSet<Register, Register, VirtReg2IndexFunctor> UntiedDefs;
  /// Live-through pressure.
  std::vector<unsigned> LiveThruPressure;

public:
  /// Construct a tracker that records results in \p rp using live intervals.
  ///
  /// \param rp Interval pressure result updated by this tracker.
  RegPressureTracker(IntervalPressure &rp) : P(rp), RequireIntervals(true) {}
  /// Construct a tracker that records results in \p rp without live intervals.
  ///
  /// \param rp Region pressure result updated by this tracker.
  RegPressureTracker(RegionPressure &rp) : P(rp), RequireIntervals(false) {}

  /// Reset the tracker so it can be initialized for another region.
  LLVM_ABI void reset();

  /// Initialize pressure tracking at \p pos in \p mbb.
  ///
  /// \param mf Machine function containing the tracked block.
  /// \param rci Register class info providing pressure set limits.
  /// \param lis Live intervals, required when tracking IntervalPressure.
  /// \param mbb Machine basic block whose pressure is tracked.
  /// \param pos Instruction position at which tracking starts.
  /// \param TrackLaneMasks True to track subregister lane masks.
  /// \param TrackUntiedDefs True to record defs that start a live range.
  LLVM_ABI void init(const MachineFunction *mf, const RegisterClassInfo *rci,
                     const LiveIntervals *lis, const MachineBasicBlock *mbb,
                     MachineBasicBlock::const_iterator pos, bool TrackLaneMasks,
                     bool TrackUntiedDefs);

  /// Force the given registers to be live at the current position.
  ///
  /// Particularly useful to initialize the live-in/out state of the tracker
  /// before the first call to advance/recede.
  ///
  /// \param Regs Virtual registers or physical register units to mark live.
  LLVM_ABI void addLiveRegs(ArrayRef<VRegMaskOrUnit> Regs);

  /// Get the MI position corresponding to this register pressure.
  ///
  /// \return Const iterator to the current instruction position.
  MachineBasicBlock::const_iterator getPos() const { return CurrPos; }

  /// Reset the MI position corresponding to the register pressure.
  ///
  /// This allows schedulers to move instructions above the
  /// RegPressureTracker's CurrPos. Since the pressure is computed before
  /// CurrPos, the iterator position changes while pressure does not.
  ///
  /// \param Pos New instruction iterator; current pressure is unchanged.
  void setPos(MachineBasicBlock::const_iterator Pos) { CurrPos = Pos; }

  /// Recede across the previous instruction.
  ///
  /// \param LiveUses If non-null, receives registers made live by uses.
  LLVM_ABI void recede(SmallVectorImpl<VRegMaskOrUnit> *LiveUses = nullptr);

  /// Recede across the previous instruction using precomputed operands.
  ///
  /// This low-level variant assumes that recedeSkipDebugValues() was called
  /// previously and takes precomputed RegisterOperands for the instruction.
  ///
  /// \param RegOpers Precomputed uses, defs, and dead defs of the instruction.
  /// \param LiveUses If non-null, receives registers made live by uses.
  LLVM_ABI void recede(const RegisterOperands &RegOpers,
                       SmallVectorImpl<VRegMaskOrUnit> *LiveUses = nullptr);

  /// Recede until we find an instruction which is not a DebugValue.
  LLVM_ABI void recedeSkipDebugValues();

  /// Advance across the current instruction.
  LLVM_ABI void advance();

  /// Advance across the current instruction.
  /// This is a "low-level" variant of advance() which takes precomputed
  /// RegisterOperands of the instruction.
  ///
  /// \param RegOpers Precomputed uses, defs, and dead defs of the instruction.
  LLVM_ABI void advance(const RegisterOperands &RegOpers);

  /// Finalize the region boundaries and recored live ins and live outs.
  LLVM_ABI void closeRegion();

  /// Initialize the LiveThru pressure set based on the untied defs found in
  /// RPTracker.
  ///
  /// \param RPTracker Tracker whose untied defs identify live-through ranges.
  LLVM_ABI void initLiveThru(const RegPressureTracker &RPTracker);

  /// Copy an existing live thru pressure result.
  ///
  /// \param PressureSet Per-pressure-set live-through values to copy.
  void initLiveThru(ArrayRef<unsigned> PressureSet) {
    LiveThruPressure.assign(PressureSet.begin(), PressureSet.end());
  }

  /// Return the live-through pressure set values.
  ///
  /// \return Per-pressure-set live-through pressure values.
  ArrayRef<unsigned> getLiveThru() const { return LiveThruPressure; }

  /// Get the resulting register pressure over the traversed region.
  /// This result is complete if closeRegion() was explicitly invoked.
  ///
  /// \return Mutable register pressure for the traversed region.
  RegisterPressure &getPressure() { return P; }
  /// Return the register pressure recorded for the traversed region.
  ///
  /// \return Const register pressure for the traversed region.
  const RegisterPressure &getPressure() const { return P; }

  /// Get the register set pressure at the current position, which may be less
  /// than the pressure across the traversed region.
  ///
  /// \return Per-pressure-set values at the current instruction position.
  const std::vector<unsigned> &getRegSetPressureAtPos() const {
    return CurrSetPressure;
  }

  /// Return true if the top of the region has a valid position and live-ins.
  ///
  /// \return True if the region top is closed with a valid position and live-ins.
  LLVM_ABI bool isTopClosed() const;
  /// Return true if the bottom of the region has a valid position and
  /// live-outs.
  ///
  /// \return True if the region bottom is closed with a valid position and
  /// live-outs.
  LLVM_ABI bool isBottomClosed() const;

  /// Set the top boundary of the region and record live-in registers.
  LLVM_ABI void closeTop();
  /// Set the bottom boundary of the region and record live-out registers.
  LLVM_ABI void closeBottom();

  /// Compute the max upward pressure delta for \p MI.
  ///
  /// Consider the pressure increase caused by traversing this instruction
  /// bottom-up. Find the pressure set with the most change beyond its pressure
  /// limit based on the tracker's current pressure, and record the number of
  /// excess register units of that pressure set introduced by this instruction.
  ///
  /// \param MI Instruction traversed bottom-up.
  /// \param PDiff Optional pressure diff used to verify the result.
  /// \param Delta Pressure delta filled in by this query.
  /// \param CriticalPSets Pressure sets known to exceed a critical limit.
  /// \param MaxPressureLimit Max pressure per set within the region.
  LLVM_ABI void
  getMaxUpwardPressureDelta(const MachineInstr *MI, PressureDiff *PDiff,
                            RegPressureDelta &Delta,
                            ArrayRef<PressureChange> CriticalPSets,
                            ArrayRef<unsigned> MaxPressureLimit);

  /// Compute the upward pressure delta for \p MI from a cached pressure diff.
  ///
  /// \param MI Instruction whose pressure change is queried.
  /// \param PDiff Cached per-instruction pressure difference.
  /// \param Delta Pressure delta filled in by this query.
  /// \param CriticalPSets Pressure sets known to exceed a critical limit.
  /// \param MaxPressureLimit Max pressure per set within the region.
  LLVM_ABI void
  getUpwardPressureDelta(const MachineInstr *MI,
                         /*const*/ PressureDiff &PDiff, RegPressureDelta &Delta,
                         ArrayRef<PressureChange> CriticalPSets,
                         ArrayRef<unsigned> MaxPressureLimit) const;

  /// Compute the max downward pressure delta for \p MI.
  ///
  /// Consider the pressure increase caused by traversing this instruction
  /// top-down. Find the pressure set with the most change beyond its pressure
  /// limit based on the tracker's current pressure, and record the number of
  /// excess register units of that pressure set introduced by this instruction.
  ///
  /// \param MI Instruction traversed top-down.
  /// \param Delta Pressure delta filled in by this query.
  /// \param CriticalPSets Pressure sets known to exceed a critical limit.
  /// \param MaxPressureLimit Max pressure per set within the region.
  LLVM_ABI void
  getMaxDownwardPressureDelta(const MachineInstr *MI, RegPressureDelta &Delta,
                              ArrayRef<PressureChange> CriticalPSets,
                              ArrayRef<unsigned> MaxPressureLimit);

  /// Compute the max pressure delta for \p MI in the open tracking direction.
  ///
  /// Finds the pressure set with the most change beyond its pressure limit
  /// after traversing this instruction either upward or downward depending on
  /// the closed end of the current region.
  ///
  /// \param MI Instruction whose pressure change is queried.
  /// \param Delta Pressure delta filled in by this query.
  /// \param CriticalPSets Pressure sets known to exceed a critical limit.
  /// \param MaxPressureLimit Max pressure per set within the region.
  void getMaxPressureDelta(const MachineInstr *MI,
                           RegPressureDelta &Delta,
                           ArrayRef<PressureChange> CriticalPSets,
                           ArrayRef<unsigned> MaxPressureLimit) {
    if (isTopClosed())
      return getMaxDownwardPressureDelta(MI, Delta, CriticalPSets,
                                         MaxPressureLimit);

    assert(isBottomClosed() && "Uninitialized pressure tracker");
    return getMaxUpwardPressureDelta(MI, nullptr, Delta, CriticalPSets,
                                     MaxPressureLimit);
  }

  /// Get the pressure of each PSet after traversing this instruction bottom-up.
  ///
  /// \param MI Instruction traversed bottom-up.
  /// \param PressureResult Per-PSet pressure after traversing \p MI.
  /// \param MaxPressureResult Per-PSet max pressure after traversing \p MI.
  LLVM_ABI void getUpwardPressure(const MachineInstr *MI,
                                  std::vector<unsigned> &PressureResult,
                                  std::vector<unsigned> &MaxPressureResult);

  /// Get the pressure of each PSet after traversing this instruction top-down.
  ///
  /// \param MI Instruction traversed top-down.
  /// \param PressureResult Per-PSet pressure after traversing \p MI.
  /// \param MaxPressureResult Per-PSet max pressure after traversing \p MI.
  LLVM_ABI void getDownwardPressure(const MachineInstr *MI,
                                    std::vector<unsigned> &PressureResult,
                                    std::vector<unsigned> &MaxPressureResult);

  /// Get the pressure of each PSet after traversing this instruction.
  ///
  /// Traverses upward or downward depending on the closed end of the current
  /// region.
  ///
  /// \param MI Instruction whose resulting pressure is queried.
  /// \param PressureResult Per-PSet pressure after traversing \p MI.
  /// \param MaxPressureResult Per-PSet max pressure after traversing \p MI.
  void getPressureAfterInst(const MachineInstr *MI,
                            std::vector<unsigned> &PressureResult,
                            std::vector<unsigned> &MaxPressureResult) {
    if (isTopClosed())
      return getUpwardPressure(MI, PressureResult, MaxPressureResult);

    assert(isBottomClosed() && "Uninitialized pressure tracker");
    return getDownwardPressure(MI, PressureResult, MaxPressureResult);
  }

  /// Return true if \p VirtReg has an untied definition in the tracked region.
  ///
  /// \param VirtReg Virtual register to query.
  /// \return True if \p VirtReg has an untied definition in the tracked region.
  bool hasUntiedDef(Register VirtReg) const {
    return UntiedDefs.count(VirtReg);
  }

  /// Dump current and max register pressure to the debug stream.
  LLVM_ABI void dump() const;

  /// Increase current and max pressure when \p VRegOrUnit becomes live.
  ///
  /// \param VRegOrUnit Virtual register or register unit whose pressure rises.
  /// \param PreviousMask Lanes that were already live.
  /// \param NewMask Lanes that are live after the increase.
  LLVM_ABI void increaseRegPressure(VirtRegOrUnit VRegOrUnit,
                                    LaneBitmask PreviousMask,
                                    LaneBitmask NewMask);
  /// Decrease current pressure when lanes of \p VRegOrUnit become dead.
  ///
  /// \param VRegOrUnit Virtual register or register unit whose pressure falls.
  /// \param PreviousMask Lanes that were live before the decrease.
  /// \param NewMask Lanes that remain live after the decrease.
  LLVM_ABI void decreaseRegPressure(VirtRegOrUnit VRegOrUnit,
                                    LaneBitmask PreviousMask,
                                    LaneBitmask NewMask);

protected:
  /// Add Reg to the live out set and increase max pressure.
  ///
  /// \param Pair Register-or-unit and lanes discovered live-out.
  LLVM_ABI void discoverLiveOut(VRegMaskOrUnit Pair);
  /// Add Reg to the live in set and increase max pressure.
  ///
  /// \param Pair Register-or-unit and lanes discovered live-in.
  LLVM_ABI void discoverLiveIn(VRegMaskOrUnit Pair);

  /// Get the SlotIndex for the first nondebug instruction including or
  /// after the current position.
  ///
  /// \return Slot index of the first non-debug instruction at or after CurrPos.
  LLVM_ABI SlotIndex getCurrSlot() const;

  /// Temporarily boost pressure for dead defs as a group, then drop it.
  ///
  /// \param DeadDefs Dead definitions whose pressure is applied together.
  LLVM_ABI void bumpDeadDefs(ArrayRef<VRegMaskOrUnit> DeadDefs);

  /// Speculatively apply \p MI's upward pressure without discovering live-ins.
  ///
  /// Leaves pressure inconsistent with the current position, so the caller must
  /// restore it.
  ///
  /// \param MI Instruction whose upward pressure impact is applied.
  LLVM_ABI void bumpUpwardPressure(const MachineInstr *MI);
  /// Speculatively apply \p MI's downward pressure without discovering
  /// live-outs.
  ///
  /// Leaves pressure inconsistent with the current position, so the caller must
  /// restore it.
  ///
  /// \param MI Instruction whose downward pressure impact is applied.
  LLVM_ABI void bumpDownwardPressure(const MachineInstr *MI);

  /// Add \p Pair to \p LiveInOrOut and increase max pressure for new lanes.
  ///
  /// \param Pair Register-or-unit and lanes discovered live in or live out.
  /// \param LiveInOrOut Live-in or live-out set to update.
  LLVM_ABI void
  discoverLiveInOrOut(VRegMaskOrUnit Pair,
                      SmallVectorImpl<VRegMaskOrUnit> &LiveInOrOut);

  /// Return lanes of \p VRegOrUnit last used at \p Pos.
  ///
  /// \param VRegOrUnit Virtual register or register unit to query.
  /// \param Pos Instruction slot at which last-use lanes are queried.
  /// \return Lane mask of lanes last used at \p Pos.
  LLVM_ABI LaneBitmask getLastUsedLanes(VirtRegOrUnit VRegOrUnit,
                                        SlotIndex Pos) const;
  /// Return lanes of \p VRegOrUnit that are live at \p Pos.
  ///
  /// \param VRegOrUnit Virtual register or register unit to query.
  /// \param Pos Slot index at which liveness is queried.
  /// \return Lane mask of lanes live at \p Pos.
  LLVM_ABI LaneBitmask getLiveLanesAt(VirtRegOrUnit VRegOrUnit,
                                      SlotIndex Pos) const;
  /// Return lanes of \p VRegOrUnit that live through the instruction at \p Pos.
  ///
  /// \param VRegOrUnit Virtual register or register unit to query.
  /// \param Pos Instruction slot at which live-through lanes are queried.
  /// \return Lane mask of lanes that live through the instruction at \p Pos.
  LLVM_ABI LaneBitmask getLiveThroughAt(VirtRegOrUnit VRegOrUnit,
                                        SlotIndex Pos) const;
};

/// Dump non-zero entries of \p SetPressure to the debug stream.
///
/// \param SetPressure Per-pressure-set values to print.
/// \param TRI Target register info used to print pressure set names.
LLVM_ABI void dumpRegSetPressure(ArrayRef<unsigned> SetPressure,
                                 const TargetRegisterInfo *TRI);

} // end namespace llvm

#endif // LLVM_CODEGEN_REGISTERPRESSURE_H
