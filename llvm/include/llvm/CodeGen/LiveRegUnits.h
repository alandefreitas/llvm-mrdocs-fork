//===- llvm/CodeGen/LiveRegUnits.h - Register Unit Set ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// A set of register units. It is intended for register liveness tracking.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVEREGUNITS_H
#define LLVM_CODEGEN_LIVEREGUNITS_H

#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

class MachineInstr;
class MachineBasicBlock;

/// A set of register units used to track register liveness.
class LiveRegUnits {
  const TargetRegisterInfo *TRI = nullptr;
  BitVector Units;

public:
  /// Constructs a new empty LiveRegUnits set.
  LiveRegUnits() = default;

  /// Constructs and initialize an empty LiveRegUnits set.
  ///
  /// \param TRI Target register info used to size the register unit universe.
  LiveRegUnits(const TargetRegisterInfo &TRI) {
    init(TRI);
  }

  /// Tracks used and modified register units for a machine instruction.
  ///
  /// For a machine instruction \p MI, adds all register units used in
  /// \p UsedRegUnits and defined or clobbered in \p ModifiedRegUnits. This is
  /// useful when walking over a range of instructions to track registers
  /// used or defined separately.
  ///
  /// \param MI Machine instruction whose operands are examined.
  /// \param ModifiedRegUnits Set that receives defined or clobbered units.
  /// \param UsedRegUnits Set that receives used units.
  /// \param TRI Target register info used for constant-register checks.
  static void accumulateUsedDefed(const MachineInstr &MI,
                                  LiveRegUnits &ModifiedRegUnits,
                                  LiveRegUnits &UsedRegUnits,
                                  const TargetRegisterInfo *TRI) {
    for (ConstMIBundleOperands O(MI); O.isValid(); ++O) {
      if (O->isRegMask())
        ModifiedRegUnits.addRegsInMask(O->getRegMask());
      if (!O->isReg())
        continue;
      Register Reg = O->getReg();
      if (!Reg.isPhysical())
        continue;
      if (O->isDef()) {
        // Some architectures (e.g. AArch64 XZR/WZR) have registers that are
        // constant and may be used as destinations to indicate the generated
        // value is discarded. No need to track such case as a def.
        if (!TRI->isConstantPhysReg(Reg))
          ModifiedRegUnits.addReg(Reg);
      } else {
        assert(O->isUse() && "Reg operand not a def and not a use");
        UsedRegUnits.addReg(Reg);
      }
    }
  }

  /// Initialize and clear the set.
  ///
  /// \param TRI Target register info used to size the register unit universe.
  void init(const TargetRegisterInfo &TRI) {
    this->TRI = &TRI;
    Units.reset();
    Units.resize(TRI.getNumRegUnits());
  }

  /// Clears the set.
  void clear() { Units.reset(); }

  /// Returns true if the set is empty.
  ///
  /// \return True if no register units are marked live.
  bool empty() const { return Units.none(); }

  /// Adds register units covered by physical register \p Reg.
  ///
  /// \param Reg Physical register whose units are added.
  void addReg(MCRegister Reg) {
    for (MCRegUnit Unit : TRI->regunits(Reg))
      Units.set(static_cast<unsigned>(Unit));
  }

  /// Adds register units covered by physical register \p Reg that are
  /// part of the lanemask \p Mask.
  ///
  /// \param Reg Physical register whose units are considered.
  /// \param Mask Lane mask selecting which units of \p Reg to add.
  void addRegMasked(MCRegister Reg, LaneBitmask Mask) {
    for (MCRegUnitMaskIterator I(Reg, TRI); I.isValid(); ++I) {
      auto [Unit, UnitMask] = *I;
      if ((UnitMask & Mask).any())
        Units.set(static_cast<unsigned>(Unit));
    }
  }

  /// Removes all register units covered by physical register \p Reg.
  ///
  /// \param Reg Physical register whose units are removed.
  void removeReg(MCRegister Reg) {
    for (MCRegUnit Unit : TRI->regunits(Reg))
      Units.reset(static_cast<unsigned>(Unit));
  }

  /// Removes register units not preserved by the regmask \p RegMask.
  /// The regmask has the same format as the one in the RegMask machine operand.
  ///
  /// \param RegMask Register mask describing preserved registers.
  LLVM_ABI void removeRegsNotPreserved(const uint32_t *RegMask);

  /// Adds register units not preserved by the regmask \p RegMask.
  /// The regmask has the same format as the one in the RegMask machine operand.
  ///
  /// \param RegMask Register mask describing clobbered registers.
  LLVM_ABI void addRegsInMask(const uint32_t *RegMask);

  /// Returns true if no part of physical register \p Reg is live.
  ///
  /// \param Reg Physical register to test for availability.
  /// \return True if no register unit of \p Reg is live.
  bool available(MCRegister Reg) const {
    for (MCRegUnit Unit : TRI->regunits(Reg)) {
      if (Units.test(static_cast<unsigned>(Unit)))
        return false;
    }
    return true;
  }

  /// Updates liveness when stepping backwards over instruction \p MI.
  ///
  /// This removes all register units defined or clobbered in \p MI and then
  /// adds the units used (as in use operands) in \p MI.
  ///
  /// \param MI Instruction to step over backwards.
  LLVM_ABI void stepBackward(const MachineInstr &MI);

  /// Adds all register units used, defined or clobbered in \p MI.
  /// This is useful when walking over a range of instruction to find registers
  /// unused over the whole range.
  ///
  /// \param MI Instruction whose used, defined, and clobbered units are added.
  LLVM_ABI void accumulate(const MachineInstr &MI);

  /// Adds registers living out of block \p MBB.
  ///
  /// Live out registers are the union of the live-in registers of the successor
  /// blocks and pristine registers. Live out registers of the end block are the
  /// callee saved registers.
  ///
  /// \param MBB Basic block whose live-out registers are added.
  LLVM_ABI void addLiveOuts(const MachineBasicBlock &MBB);

  /// Adds registers living into block \p MBB.
  ///
  /// \param MBB Basic block whose live-in registers are added.
  LLVM_ABI void addLiveIns(const MachineBasicBlock &MBB);

  /// Adds all register units marked in the bitvector \p RegUnits.
  ///
  /// \param RegUnits Bitvector of register units to add.
  void addUnits(const BitVector &RegUnits) {
    Units |= RegUnits;
  }
  /// Removes all register units marked in the bitvector \p RegUnits.
  ///
  /// \param RegUnits Bitvector of register units to remove.
  void removeUnits(const BitVector &RegUnits) {
    Units.reset(RegUnits);
  }
  /// Return the internal bitvector representation of the set.
  ///
  /// \return Const reference to the bitvector of live register units.
  const BitVector &getBitVector() const {
    return Units;
  }

private:
  /// Adds pristine registers. Pristine registers are callee saved registers
  /// that are unused in the function.
  void addPristines(const MachineFunction &MF);
};

/// Returns an iterator range over all physical register and mask operands for
/// \p MI and bundled instructions. This also skips any debug operands.
///
/// \param MI Machine instruction (or bundle) whose operands are filtered.
/// \return Iterator range over physical register and mask operands of \p MI.
inline iterator_range<
    filter_iterator<ConstMIBundleOperands, bool (*)(const MachineOperand &)>>
phys_regs_and_masks(const MachineInstr &MI) {
  auto Pred = [](const MachineOperand &MOP) {
    return MOP.isRegMask() ||
           (MOP.isReg() && !MOP.isDebug() && MOP.getReg().isPhysical());
  };
  return make_filter_range(const_mi_bundle_ops(MI),
                           static_cast<bool (*)(const MachineOperand &)>(Pred));
}

} // end namespace llvm

#endif // LLVM_CODEGEN_LIVEREGUNITS_H
