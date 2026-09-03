//===- RegisterScavenging.h - Machine register scavenging -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file declares the machine register scavenger class. It can provide
/// information such as unused register at any point in a machine basic block.
/// It also provides a mechanism to make registers available by evicting them
/// to spill slots.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTERSCAVENGING_H
#define LLVM_CODEGEN_REGISTERSCAVENGING_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/MC/LaneBitmask.h"

namespace llvm {

class MachineInstr;
class TargetInstrInfo;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class TargetRegisterInfo;

/// Tracks register liveness and scavenges free registers by spilling.
class RegScavenger {
  const TargetRegisterInfo *TRI = nullptr;
  const TargetInstrInfo *TII = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  MachineBasicBlock *MBB = nullptr;
  MachineBasicBlock::iterator MBBI;

  /// Information on scavenged registers (held in a spill slot).
  struct ScavengedInfo {
    ScavengedInfo(int FI = -1) : FrameIndex(FI) {}

    /// A spill slot used for scavenging a register post register allocation.
    int FrameIndex;

    /// If non-zero, the specific register is currently being
    /// scavenged. That is, it is spilled to this scavenging stack slot.
    Register Reg;

    /// The instruction that restores the scavenged register from stack.
    const MachineInstr *Restore = nullptr;
  };

  /// A vector of information on scavenged registers.
  SmallVector<ScavengedInfo, 2> Scavenged;

  LiveRegUnits LiveUnits;

public:
  /// Constructs an uninitialized register scavenger.
  RegScavenger() = default;

  /// Record that a register is in use at a scavenging frame index.
  ///
  /// This is for targets which need to directly manage the spilling process,
  /// and need to update the scavenger's internal state. It's expected this be
  /// called a second time with \p Restore set to a non-null value, so that the
  /// externally inserted restore instruction resets the scavenged slot
  /// liveness when encountered.
  ///
  /// \param FI Scavenging frame index where \p Reg is recorded as in use.
  /// \param Reg Register currently spilled into the scavenging slot.
  /// \param Restore Optional restore instruction that frees the slot again.
  void assignRegToScavengingIndex(int FI, Register Reg,
                                  MachineInstr *Restore = nullptr) {
    for (ScavengedInfo &Slot : Scavenged) {
      if (Slot.FrameIndex == FI) {
        assert(!Slot.Reg || Slot.Reg == Reg);
        Slot.Reg = Reg;
        Slot.Restore = Restore;
        return;
      }
    }

    llvm_unreachable("did not find scavenging index");
  }

  /// Start tracking liveness from the begin of basic block \p MBB.
  ///
  /// \param MBB Basic block whose beginning becomes the tracking position.
  LLVM_ABI void enterBasicBlock(MachineBasicBlock &MBB);

  /// Start tracking liveness from the end of basic block \p MBB.
  /// Use backward() to move towards the beginning of the block.
  ///
  /// \param MBB Basic block whose end becomes the tracking position.
  LLVM_ABI void enterBasicBlockEnd(MachineBasicBlock &MBB);

  /// Update internal register state and move MBB iterator backwards. This
  /// method gives precise results even in the absence of kill flags.
  LLVM_ABI void backward();

  /// Call backward() to update internal register state to just before \p *I.
  ///
  /// \param I Iterator to stop before when walking backwards.
  void backward(MachineBasicBlock::iterator I) {
    while (MBBI != I)
      backward();
  }

  /// Return if a specific register is currently used.
  ///
  /// \param Reg Register whose current use status is queried.
  /// \param includeReserved If true, reserved registers count as used.
  /// \return True if \p Reg is currently used.
  LLVM_ABI bool isRegUsed(Register Reg, bool includeReserved = true) const;

  /// Return all available registers in the register class in Mask.
  ///
  /// \param RC Register class whose available registers are returned.
  /// \return Bit vector of currently available registers in \p RC.
  LLVM_ABI BitVector getRegsAvailable(const TargetRegisterClass *RC);

  /// Find an unused register of the specified register class.
  ///
  /// \param RC Register class in which to search for an unused register.
  /// \return An unused register of class \p RC, or 0 if none is found.
  LLVM_ABI Register FindUnusedReg(const TargetRegisterClass *RC) const;

  /// Add a scavenging frame index.
  ///
  /// \param FI Frame index to register as a scavenging spill slot.
  void addScavengingFrameIndex(int FI) {
    Scavenged.push_back(ScavengedInfo(FI));
  }

  /// Query whether a frame index is a scavenging frame index.
  ///
  /// \param FI Frame index to test for membership in the scavenging slots.
  /// \return True if \p FI is a scavenging frame index.
  bool isScavengingFrameIndex(int FI) const {
    for (const ScavengedInfo &SI : Scavenged)
      if (SI.FrameIndex == FI)
        return true;

    return false;
  }

  /// Get an array of scavenging frame indices.
  ///
  /// \param A Vector that receives the scavenging frame indices.
  void getScavengingFrameIndices(SmallVectorImpl<int> &A) const {
    for (const ScavengedInfo &I : Scavenged)
      if (I.FrameIndex >= 0)
        A.push_back(I.FrameIndex);
  }

  /// Return the number of scavenging frame indices.
  ///
  /// \return The number of scavenging frame indices.
  size_t getNumScavengingFrameIndices() const { return Scavenged.size(); }

  /// Make a register of the given class available from the current position
  /// backwards.
  ///
  /// Availability extends to the place before \p To. If \p RestoreAfter is true
  /// this includes the instruction following the current position.
  /// SPAdj is the stack adjustment due to call frame, it's passed along to
  /// eliminateFrameIndex().
  ///
  /// If \p AllowSpill is false, fail if a spill is required to make the
  /// register available, and return NoRegister.
  ///
  /// \param RC Register class of the register to scavenge.
  /// \param To Earliest position the scavenged register must remain available.
  /// \param RestoreAfter If true, also cover the instruction after the current
  ///        position.
  /// \param SPAdj Stack pointer adjustment passed to eliminateFrameIndex().
  /// \param AllowSpill If false, do not spill; return NoRegister instead.
  /// \return The scavenged register, or NoRegister if spilling is disallowed
  ///         and required.
  LLVM_ABI Register scavengeRegisterBackwards(const TargetRegisterClass &RC,
                                              MachineBasicBlock::iterator To,
                                              bool RestoreAfter, int SPAdj,
                                              bool AllowSpill = true);

  /// Tell the scavenger a register is used.
  ///
  /// \param Reg Register marked as used.
  /// \param LaneMask Lanes of \p Reg that are considered used.
  LLVM_ABI void setRegUsed(Register Reg,
                           LaneBitmask LaneMask = LaneBitmask::getAll());

private:
  /// Returns true if a register is reserved. It is never "unused".
  bool isReserved(Register Reg) const { return MRI->isReserved(Reg); }

  /// Initialize RegisterScavenger.
  void init(MachineBasicBlock &MBB);

  /// Spill a register after position \p After and reload it before position
  /// \p UseMI.
  ScavengedInfo &spill(Register Reg, const TargetRegisterClass &RC, int SPAdj,
                       MachineBasicBlock::iterator Before,
                       MachineBasicBlock::iterator &UseMI);
};

/// Replaces all frame index virtual registers with physical registers. Uses the
/// register scavenger to find an appropriate register to use.
///
/// \param MF Machine function whose frame-index virtual registers are replaced.
/// \param RS Register scavenger used to find suitable physical registers.
LLVM_ABI void scavengeFrameVirtualRegs(MachineFunction &MF, RegScavenger &RS);

} // end namespace llvm

#endif // LLVM_CODEGEN_REGISTERSCAVENGING_H
