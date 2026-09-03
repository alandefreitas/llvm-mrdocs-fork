//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CFIINSTBUILDER_H
#define LLVM_CODEGEN_CFIINSTBUILDER_H

#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCDwarf.h"

namespace llvm {

/// Helper class for creating CFI instructions and inserting them into MIR.
class CFIInstBuilder {
  MachineFunction &MF;
  MachineBasicBlock &MBB;
  MachineBasicBlock::iterator InsertPt;

  /// MIFlag to set on a MachineInstr. Typically, FrameSetup or FrameDestroy.
  MachineInstr::MIFlag MIFlag;

  /// Selects DWARF register numbering: debug or exception handling. Should be
  /// consistent with the choice of the ELF section (.debug_frame or .eh_frame)
  /// where CFI will be encoded.
  bool IsEH;

  // Cache frequently used variables.
  const TargetRegisterInfo &TRI;
  const MCInstrDesc &CFIID;
  const MIMetadata MIMD; // Default-initialized, no debug location desired.

public:
  /// Construct a CFI instruction builder.
  ///
  /// Generated instructions are inserted into \p MBB at \p InsertPt.
  ///
  /// \param MBB Machine basic block that receives the CFI instructions.
  /// \param InsertPt Insertion point within \p MBB.
  /// \param MIFlag Flag set on each built instruction, typically FrameSetup or
  ///        FrameDestroy.
  /// \param IsEH If true, use exception-handling DWARF register numbers
  ///        (.eh_frame); otherwise use debug register numbers (.debug_frame).
  CFIInstBuilder(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
                 MachineInstr::MIFlag MIFlag, bool IsEH = true)
      : MF(*MBB.getParent()), MBB(MBB), MIFlag(MIFlag), IsEH(IsEH),
        TRI(*MF.getSubtarget().getRegisterInfo()),
        CFIID(MF.getSubtarget().getInstrInfo()->get(
            TargetOpcode::CFI_INSTRUCTION)) {
    setInsertPoint(InsertPt);
  }

  /// Construct a CFI instruction builder.
  ///
  /// Generated instructions are inserted at the end of \p MBB.
  ///
  /// \param MBB Machine basic block that receives the CFI instructions.
  /// \param MIFlag Flag set on each built instruction, typically FrameSetup or
  ///        FrameDestroy.
  /// \param IsEH If true, use exception-handling DWARF register numbers
  ///        (.eh_frame); otherwise use debug register numbers (.debug_frame).
  CFIInstBuilder(MachineBasicBlock *MBB, MachineInstr::MIFlag MIFlag,
                 bool IsEH = true)
      : CFIInstBuilder(*MBB, MBB->end(), MIFlag, IsEH) {}

  /// Set the insertion point for subsequent CFI instructions.
  ///
  /// \param IP Iterator in the current machine basic block.
  void setInsertPoint(MachineBasicBlock::iterator IP) { InsertPt = IP; }

  /// Insert a CFI instruction at the current insertion point.
  ///
  /// \param CFIInst CFI instruction to add to the machine function and emit.
  void insertCFIInst(const MCCFIInstruction &CFIInst) const {
    BuildMI(MBB, InsertPt, MIMD, CFIID)
        .addCFIIndex(MF.addFrameInst(CFIInst))
        .setMIFlag(MIFlag);
  }

  /// Insert a CFI instruction that defines the CFA as \p Reg plus \p Offset.
  ///
  /// Corresponds to .cfi_def_cfa.
  ///
  /// \param Reg Register holding the CFA base address.
  /// \param Offset Offset added to \p Reg to compute the CFA.
  void buildDefCFA(MCRegister Reg, int64_t Offset) const {
    insertCFIInst(MCCFIInstruction::cfiDefCfa(
        nullptr, TRI.getDwarfRegNum(Reg, IsEH), Offset));
  }

  /// Insert a CFI instruction that changes the CFA register to \p Reg.
  ///
  /// The CFA offset is unchanged. Corresponds to .cfi_def_cfa_register.
  ///
  /// \param Reg Register that now holds the CFA base address.
  void buildDefCFARegister(MCRegister Reg) const {
    insertCFIInst(MCCFIInstruction::createDefCfaRegister(
        nullptr, TRI.getDwarfRegNum(Reg, IsEH)));
  }

  /// Insert a CFI instruction that sets the CFA offset to \p Offset.
  ///
  /// The CFA register is unchanged. \p Offset is the absolute displacement
  /// added to that register. Corresponds to .cfi_def_cfa_offset.
  ///
  /// \param Offset Absolute offset added to the CFA register.
  /// \param Label Optional label associated with this CFI instruction.
  void buildDefCFAOffset(int64_t Offset, MCSymbol *Label = nullptr) const {
    insertCFIInst(MCCFIInstruction::cfiDefCfaOffset(Label, Offset));
  }

  /// Insert a CFI instruction that adjusts the CFA offset by \p Adjustment.
  ///
  /// Corresponds to .cfi_adjust_cfa_offset.
  ///
  /// \param Adjustment Relative amount added to the current CFA offset.
  void buildAdjustCFAOffset(int64_t Adjustment) const {
    insertCFIInst(MCCFIInstruction::createAdjustCfaOffset(nullptr, Adjustment));
  }

  /// Insert a CFI instruction that records \p Reg saved at \p Offset from CFA.
  ///
  /// Corresponds to .cfi_offset.
  ///
  /// \param Reg Register whose previous value is saved.
  /// \param Offset Offset from the CFA where \p Reg is saved.
  void buildOffset(MCRegister Reg, int64_t Offset) const {
    insertCFIInst(MCCFIInstruction::createOffset(
        nullptr, TRI.getDwarfRegNum(Reg, IsEH), Offset));
  }

  /// Insert a CFI instruction that toggles the AArch64 RA sign state.
  ///
  /// Corresponds to .cfi_negate_ra_state.
  void buildNegateRAState() const {
    insertCFIInst(MCCFIInstruction::createNegateRAState(nullptr));
  }

  /// Insert a CFI instruction that toggles AArch64 RA sign state with PC.
  ///
  /// Corresponds to .cfi_negate_ra_state_with_pc.
  void buildNegateRAStateWithPC() const {
    insertCFIInst(MCCFIInstruction::createNegateRAStateWithPC(nullptr));
  }

  /// Insert a CFI instruction that sets the AArch64 RA sign state.
  ///
  /// \p PACSym is a symbolic offset to the signing instruction. Corresponds
  /// to .cfi_set_ra_state.
  ///
  /// \param State RA sign state (DW_AARCH64_RA_NOT_SIGNED,
  ///        DW_AARCH64_RA_SIGNED_SP, or DW_AARCH64_RA_SIGNED_SP_PC).
  /// \param PACSym Symbol of the pointer-authentication signing instruction.
  void buildSetRAState(unsigned State, MCSymbol *PACSym) const {
    insertCFIInst(MCCFIInstruction::createSetRAState(nullptr, State, PACSym));
  }

  /// Insert a CFI instruction that records \p Reg1 saved in \p Reg2.
  ///
  /// Corresponds to .cfi_register.
  ///
  /// \param Reg1 Register whose previous value is saved.
  /// \param Reg2 Register that now holds the previous value of \p Reg1.
  void buildRegister(MCRegister Reg1, MCRegister Reg2) const {
    insertCFIInst(MCCFIInstruction::createRegister(
        nullptr, TRI.getDwarfRegNum(Reg1, IsEH),
        TRI.getDwarfRegNum(Reg2, IsEH)));
  }

  /// Insert a CFI instruction that records a SPARC register-window save.
  ///
  /// Corresponds to .cfi_window_save.
  void buildWindowSave() const {
    insertCFIInst(MCCFIInstruction::createWindowSave(nullptr));
  }

  /// Insert a CFI instruction that restores the original CFI rule for \p Reg.
  ///
  /// The rule becomes the one in effect at the beginning of the function,
  /// after .cfi_startproc. Corresponds to .cfi_restore.
  ///
  /// \param Reg Register whose CFI rule is restored.
  void buildRestore(MCRegister Reg) const {
    insertCFIInst(MCCFIInstruction::createRestore(
        nullptr, TRI.getDwarfRegNum(Reg, IsEH)));
  }

  /// Insert a CFI instruction that marks the prior value of \p Reg unrestorable.
  ///
  /// Corresponds to .cfi_undefined.
  ///
  /// \param Reg Register whose previous value can no longer be restored.
  void buildUndefined(MCRegister Reg) const {
    insertCFIInst(MCCFIInstruction::createUndefined(
        nullptr, TRI.getDwarfRegNum(Reg, IsEH)));
  }

  /// Insert a CFI instruction that marks \p Reg unchanged from the caller.
  ///
  /// Corresponds to .cfi_same_value.
  ///
  /// \param Reg Register whose current value matches the previous frame.
  void buildSameValue(MCRegister Reg) const {
    insertCFIInst(MCCFIInstruction::createSameValue(
        nullptr, TRI.getDwarfRegNum(Reg, IsEH)));
  }

  /// Insert a CFI instruction that emits arbitrary unwind-info bytes.
  ///
  /// Corresponds to .cfi_escape.
  ///
  /// \param Bytes Bytes appended to the unwind information.
  /// \param Comment Optional comment attached to the escape.
  void buildEscape(StringRef Bytes, StringRef Comment = "") const {
    insertCFIInst(
        MCCFIInstruction::createEscape(nullptr, Bytes, SMLoc(), Comment));
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_CFIINSTBUILDER_H
