//===- LiveRangeEdit.h - Basic tools for split and spill --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The LiveRangeEdit class represents changes done to a virtual register when it
// is spilled or split.
//
// The parent register is never changed. Instead, a number of new virtual
// registers are created and added to the newRegs vector.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVERANGEEDIT_H
#define LLVM_CODEGEN_LIVERANGEEDIT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include <cassert>

namespace llvm {

class LiveIntervals;
class MachineInstr;
class MachineOperand;
class TargetInstrInfo;
class TargetRegisterInfo;
class VirtRegMap;
class VirtRegAuxInfo;

/// Represents changes to a virtual register when it is spilled or split.
///
/// The parent register is never changed. Instead, new virtual registers are
/// created and added to the newRegs vector.
class LLVM_ABI LiveRangeEdit : private MachineRegisterInfo::Delegate {
public:
  /// Callback methods for LiveRangeEdit owners.
  class LLVM_ABI Delegate {
    virtual void anchor();

  public:
    /// Destroy this delegate.
    virtual ~Delegate() = default;

    /// Called immediately before erasing a dead machine instruction.
    ///
    /// \param MI Instruction about to be erased.
    virtual void LRE_WillEraseInstruction(MachineInstr *MI) {}

    /// Called when a virtual register is no longer used. Return false to defer
    /// its deletion from LiveIntervals.
    ///
    /// \param Reg Virtual register that is no longer used.
    /// \return True if \p Reg may be erased from LiveIntervals; false to defer.
    virtual bool LRE_CanEraseVirtReg(Register Reg) { return true; }

    /// Called before shrinking the live range of a virtual register.
    ///
    /// \param Reg Virtual register whose live range will shrink.
    virtual void LRE_WillShrinkVirtReg(Register Reg) {}

    /// Called after cloning a virtual register.
    ///
    /// This is used for new registers representing connected components of Old.
    ///
    /// \param New Newly created virtual register.
    /// \param Old Original virtual register that was cloned.
    virtual void LRE_DidCloneVirtReg(Register New, Register Old) {}
  };

private:
  const LiveInterval *const Parent;
  SmallVectorImpl<Register> &NewRegs;
  MachineRegisterInfo &MRI;
  LiveIntervals &LIS;
  VirtRegMap *VRM;
  const TargetInstrInfo &TII;
  Delegate *const TheDelegate;

  /// FirstNew - Index of the first register added to NewRegs.
  const unsigned FirstNew;

  /// DeadRemats - The saved instructions which have already been dead after
  /// rematerialization but not deleted yet -- to be done in postOptimization.
  SmallPtrSet<MachineInstr *, 32> *DeadRemats;

  /// Rematted - Values that were actually rematted, and so need to have their
  /// live range trimmed or entirely removed.
  SmallPtrSet<const VNInfo *, 4> Rematted;

  /// foldAsLoad - If LI has a single use and a single def that can be folded as
  /// a load, eliminate the register by folding the def into the use.
  bool foldAsLoad(LiveInterval *LI, SmallVectorImpl<MachineInstr *> &Dead);

  using ToShrinkSet = SmallSetVector<LiveInterval *, 8>;

  /// Helper for eliminateDeadDefs.
  void eliminateDeadDef(MachineInstr *MI, ToShrinkSet &ToShrink);

  /// MachineRegisterInfo callback to notify when new virtual
  /// registers are created.
  void MRI_NoteNewVirtualRegister(Register VReg) override;

  /// Check if MachineOperand \p MO is a last use/kill either in the
  /// main live range of \p LI or in one of the matching subregister ranges.
  bool useIsKill(const LiveInterval &LI, const MachineOperand &MO) const;

  /// Create a new empty interval based on OldReg.
  LiveInterval &createEmptyIntervalFrom(Register OldReg, bool createSubRanges);

public:
  /// Create a LiveRangeEdit for breaking down parent into smaller pieces.
  /// @param parent The register being spilled or split.
  /// @param newRegs List to receive any new registers created. This needn't be
  ///                empty initially, any existing registers are ignored.
  /// @param MF The MachineFunction the live range edit is taking place in.
  /// @param lis The collection of all live intervals in this function.
  /// @param vrm Map of virtual registers to physical registers for this
  ///            function.  If NULL, no virtual register map updates will
  ///            be done.  This could be the case if called before Regalloc.
  /// @param delegate Optional callback target notified of edit events.
  /// @param deadRemats The collection of all the instructions defining an
  ///                   original reg and are dead after remat.
  LiveRangeEdit(const LiveInterval *parent, SmallVectorImpl<Register> &newRegs,
                MachineFunction &MF, LiveIntervals &lis, VirtRegMap *vrm,
                Delegate *delegate = nullptr,
                SmallPtrSet<MachineInstr *, 32> *deadRemats = nullptr)
      : Parent(parent), NewRegs(newRegs), MRI(MF.getRegInfo()), LIS(lis),
        VRM(vrm), TII(*MF.getSubtarget().getInstrInfo()), TheDelegate(delegate),
        FirstNew(newRegs.size()), DeadRemats(deadRemats) {
    MRI.addDelegate(this);
  }

  /// Destroy this live range edit and remove it as an MRI delegate.
  ~LiveRangeEdit() override { MRI.resetDelegate(this); }

  /// Return the parent live interval being edited.
  ///
  /// \return Const reference to the parent live interval.
  const LiveInterval &getParent() const {
    assert(Parent && "No parent LiveInterval");
    return *Parent;
  }

  /// Return the virtual register of the parent live interval.
  ///
  /// \return The parent virtual register.
  Register getReg() const { return getParent().reg(); }

  /// Iterator for accessing the new registers added by this edit.
  using iterator = SmallVectorImpl<Register>::const_iterator;
  /// Return an iterator to the first new register added by this edit.
  ///
  /// \return Const iterator to the first new register.
  iterator begin() const { return NewRegs.begin() + FirstNew; }
  /// Return an iterator past the last new register added by this edit.
  ///
  /// \return Const iterator past the last new register.
  iterator end() const { return NewRegs.end(); }
  /// Return the number of new registers added by this edit.
  ///
  /// \return Count of registers added since this edit began.
  unsigned size() const { return NewRegs.size() - FirstNew; }
  /// Return true if this edit has not added any new registers.
  ///
  /// \return True if no new registers have been added.
  bool empty() const { return size() == 0; }
  /// Return the new register at index \p idx among registers added by this
  /// edit.
  ///
  /// \param idx Zero-based index into the registers added by this edit.
  /// \return The new register at \p idx.
  Register get(unsigned idx) const { return NewRegs[idx + FirstNew]; }

  /// Drop the last new register from this edit.
  ///
  /// It allows LiveRangeEdit users to drop new registers. The context is when
  /// an original def instruction of a register is dead after rematerialization,
  /// we still want to keep it for following rematerializations. We save the def
  /// instruction in DeadRemats, and replace the original dst register with a
  /// new dummy register so the live range of original dst register can be
  /// shrinked normally. We don't want to allocate phys register for the dummy
  /// register, so we want to drop it from the NewRegs set.
  void pop_back() { NewRegs.pop_back(); }

  /// Return the new registers added by this edit.
  ///
  /// \return Array reference over the registers added by this edit.
  ArrayRef<Register> regs() const { return ArrayRef(NewRegs).slice(FirstNew); }

  /// createFrom - Create a new virtual register based on OldReg.
  ///
  /// \param OldReg Existing virtual register whose class and attributes are
  ///        used as a template for the new register.
  /// \return The newly created virtual register.
  Register createFrom(Register OldReg);

  /// create - Create a new register with the same class and original slot as
  /// parent.
  ///
  /// \return Empty live interval for the new register cloned from the parent.
  LiveInterval &createEmptyInterval() {
    return createEmptyIntervalFrom(getReg(), true);
  }

  /// Create a new virtual register based on the parent register.
  ///
  /// \return The newly created virtual register.
  Register create() { return createFrom(getReg()); }

  /// Remat - Information needed to rematerialize at a specific location.
  struct Remat {
    /// Parent live range's value number at the rematerialization location.
    const VNInfo *const ParentVNI;
    /// Instruction defining the original value; contains the remat expression.
    MachineInstr *OrigMI = nullptr;

    /// Construct remat info for the given parent value number.
    ///
    /// \param ParentVNI Parent value at the rematerialization location.
    explicit Remat(const VNInfo *ParentVNI) : ParentVNI(ParentVNI) {}
  };

  /// canRematerializeAt - Determine if RM.Orig can be rematerialized at
  /// UseIdx. It is assumed that parent_.getVNINfoAt(UseIdx) == ParentVNI.
  ///
  /// \param RM Rematerialization info for the value under consideration.
  /// \param UseIdx Slot index at which rematerialization is requested.
  /// \return True if the value can be rematerialized at \p UseIdx.
  bool canRematerializeAt(Remat &RM, SlotIndex UseIdx);

  /// Rematerialize RM.ParentVNI into DestReg before MI in MBB.
  ///
  /// The new instruction is mapped, but liveness is not updated. If
  /// ReplaceIndexMI is not null it will be replaced by new MI in the index map.
  /// UsedLanes is a bitmask of the lanes that are live at the rematerialization
  /// point, forwarded to TII.reMaterialize. Return the SlotIndex of the new
  /// instruction.
  ///
  /// \param MBB Basic block that receives the rematerialized instruction.
  /// \param MI Insertion point; the new instruction is inserted before this.
  /// \param DestReg Destination virtual register for the rematerialized value.
  /// \param RM Rematerialization info describing the parent value and original
  ///        defining instruction.
  /// \param TRI Target register info used by the rematerialization helper.
  /// \param Late Whether to prefer a late insertion slot when applicable.
  /// \param SubIdx Optional subregister index on the destination.
  /// \param ReplaceIndexMI If non-null, replaced by the new instruction in the
  ///        slot index map.
  /// \param UsedLanes Bitmask of lanes live at the rematerialization point.
  /// \return Slot index of the newly rematerialized instruction.
  SlotIndex rematerializeAt(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI, Register DestReg,
                            const Remat &RM, const TargetRegisterInfo &TRI,
                            bool Late = false, unsigned SubIdx = 0,
                            MachineInstr *ReplaceIndexMI = nullptr,
                            LaneBitmask UsedLanes = LaneBitmask::getAll());

  /// markRematerialized - explicitly mark a value as rematerialized after doing
  /// it manually.
  ///
  /// \param ParentVNI Parent value number that was rematerialized.
  void markRematerialized(const VNInfo *ParentVNI) {
    Rematted.insert(ParentVNI);
  }

  /// didRematerialize - Return true if ParentVNI was rematerialized anywhere.
  ///
  /// \param ParentVNI Parent value number to query.
  /// \return True if \p ParentVNI was rematerialized anywhere.
  bool didRematerialize(const VNInfo *ParentVNI) const {
    return Rematted.count(ParentVNI);
  }

  /// eraseVirtReg - Notify the delegate that Reg is no longer in use, and try
  /// to erase it from LIS.
  ///
  /// \param Reg Virtual register that is no longer in use.
  void eraseVirtReg(Register Reg);

  /// Try to delete machine instructions that are now dead.
  ///
  /// Instructions where allDefsAreDead returns true may be deleted. This may
  /// cause live intervals to be trimmed and further dead defs to be eliminated.
  /// RegsBeingSpilled lists registers currently being spilled by the register
  /// allocator. These registers should not be split into new intervals as
  /// currently those new intervals are not guaranteed to spill.
  ///
  /// \param Dead Instructions that may be dead and candidates for deletion.
  /// \param RegsBeingSpilled Registers currently being spilled; avoid splitting
  ///        them into new intervals.
  void eliminateDeadDefs(SmallVectorImpl<MachineInstr *> &Dead,
                         ArrayRef<Register> RegsBeingSpilled = {});

  /// calculateRegClassAndHint - Recompute register class and hint for each new
  /// register.
  ///
  /// \param MF Machine function containing the new registers.
  /// \param VRAI Auxiliary virtual register info used for hint calculation.
  void calculateRegClassAndHint(MachineFunction &MF, VirtRegAuxInfo &VRAI);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_LIVERANGEEDIT_H
