//===- CodeGen/MachineInstrBuilder.h - Simplify creation of MIs --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes a function named BuildMI, which is useful for dramatically
// simplifying how MachineInstr's are created.  It allows use of code like this:
//
//   MIMetadata MIMD(MI);  // Propagates DebugLoc and other metadata
//   M = BuildMI(MBB, MI, MIMD, TII.get(X86::ADD8rr), Dst)
//           .addReg(argVal1)
//           .addReg(argVal2);
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEINSTRBUILDER_H
#define LLVM_CODEGEN_MACHINEINSTRBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>

namespace llvm {

class MCInstrDesc;
class MDNode;

/// Flags to represent properties of register accesses.
///
/// These used to be represented with `unsigned`, but the underlying type of
/// this enum class is `uint16_t` because these flags are serialized into 2-byte
/// fields in the GlobalISel table emitters.
///
/// Keep this in sync with the table in MIRLangReg.rst
enum class RegState : uint16_t {
  /// No Specific Flags
  NoFlags = 0x0,
  // Reserved value, to detect if someone is passing `true` rather than this
  // enum.
  _Reserved = 0x1,
  /// Register definition.
  Define = 0x2,
  /// Not emitted register (e.g. carry, or temporary result).
  Implicit = 0x4,
  /// The last use of a register.
  Kill = 0x8,
  /// Unused definition.
  Dead = 0x10,
  /// Value of the register doesn't matter.
  Undef = 0x20,
  /// Register definition happens before uses.
  EarlyClobber = 0x40,
  /// Register 'use' is for debugging purpose.
  Debug = 0x80,
  /// Register reads a value that is defined inside the same instruction or
  /// bundle.
  InternalRead = 0x100,
  /// Register that may be renamed.
  Renamable = 0x200,

  LLVM_MARK_AS_BITMASK_ENUM(Renamable),

  // Combinations of above flags
  DefineNoRead = Define | Undef,
  ImplicitDefine = Implicit | Define,
  ImplicitKill = Implicit | Kill
};

/// Return \c RegState::Define when \p B is true, else \c RegState::NoFlags.
///
/// \param B True to include the definition flag.
/// \return \c RegState::Define when \p B is true, otherwise \c RegState::NoFlags.
constexpr RegState getDefRegState(bool B) {
  return B ? RegState::Define : RegState::NoFlags;
}
/// Return \c RegState::Implicit when \p B is true, else \c RegState::NoFlags.
///
/// \param B True to include the implicit flag.
/// \return \c RegState::Implicit when \p B is true, otherwise \c RegState::NoFlags.
constexpr RegState getImplRegState(bool B) {
  return B ? RegState::Implicit : RegState::NoFlags;
}
/// Return \c RegState::Kill when \p B is true, else \c RegState::NoFlags.
///
/// \param B True to include the kill flag.
/// \return \c RegState::Kill when \p B is true, otherwise \c RegState::NoFlags.
constexpr RegState getKillRegState(bool B) {
  return B ? RegState::Kill : RegState::NoFlags;
}
/// Return \c RegState::Dead when \p B is true, else \c RegState::NoFlags.
///
/// \param B True to include the dead flag.
/// \return \c RegState::Dead when \p B is true, otherwise \c RegState::NoFlags.
constexpr RegState getDeadRegState(bool B) {
  return B ? RegState::Dead : RegState::NoFlags;
}
/// Return \c RegState::Undef when \p B is true, else \c RegState::NoFlags.
///
/// \param B True to include the undef flag.
/// \return \c RegState::Undef when \p B is true, otherwise \c RegState::NoFlags.
constexpr RegState getUndefRegState(bool B) {
  return B ? RegState::Undef : RegState::NoFlags;
}
/// Return \c EarlyClobber when \p B is true, else \c RegState::NoFlags.
///
/// \param B True to include the early-clobber flag.
/// \return \c RegState::EarlyClobber when \p B is true, otherwise \c RegState::NoFlags.
constexpr RegState getEarlyClobberRegState(bool B) {
  return B ? RegState::EarlyClobber : RegState::NoFlags;
}
/// Return \c RegState::Debug when \p B is true, else \c RegState::NoFlags.
///
/// \param B True to include the debug flag.
/// \return \c RegState::Debug when \p B is true, otherwise \c RegState::NoFlags.
constexpr RegState getDebugRegState(bool B) {
  return B ? RegState::Debug : RegState::NoFlags;
}
/// Return \c InternalRead when \p B is true, else \c RegState::NoFlags.
///
/// \param B True to include the internal-read flag.
/// \return \c RegState::InternalRead when \p B is true, otherwise \c RegState::NoFlags.
constexpr RegState getInternalReadRegState(bool B) {
  return B ? RegState::InternalRead : RegState::NoFlags;
}
/// Return \c Renamable when \p B is true, else \c RegState::NoFlags.
///
/// \param B True to include the renamable flag.
/// \return \c RegState::Renamable when \p B is true, otherwise \c RegState::NoFlags.
constexpr RegState getRenamableRegState(bool B) {
  return B ? RegState::Renamable : RegState::NoFlags;
}

/// Return true if \p Value includes every flag in \p Test.
///
/// \param Value Combined register-state flags to inspect.
/// \param Test Flags that must all be present in \p Value.
/// \return True if \p Value includes every flag in \p Test.
constexpr bool hasRegState(RegState Value, RegState Test) {
  return (Value & Test) == Test;
}

/// Get all register state flags from machine operand \p RegOp.
///
/// \param RegOp Register operand to inspect.
/// \return Combined register-state flags from \p RegOp.
inline RegState getRegState(const MachineOperand &RegOp) {
  assert(RegOp.isReg() && "Not a register operand");
  return getDefRegState(RegOp.isDef()) | getImplRegState(RegOp.isImplicit()) |
         getKillRegState(RegOp.isKill()) | getDeadRegState(RegOp.isDead()) |
         getUndefRegState(RegOp.isUndef()) |
         // FIXME: why is this not included
         // getEarlyClobberRegState(RegOp.isEarlyClobber()) |
         getInternalReadRegState(RegOp.isInternalRead()) |
         getDebugRegState(RegOp.isDebug()) |
         getRenamableRegState(RegOp.getReg().isPhysical() &&
                              RegOp.isRenamable());
}

/// Set of metadata that should be preserved when using BuildMI(). This provides
/// a more convenient way of preserving certain data from the original
/// instruction.
class MIMetadata {
public:
  /// Construct empty metadata with a default debug location.
  MIMetadata() = default;
  /// Construct metadata from a debug location and optional extra fields.
  ///
  /// \param DL Debug location to attach.
  /// \param PCSections Optional PC-sections metadata.
  /// \param MMRA Optional MMRA metadata.
  /// \param DeactivationSymbol Optional deactivation symbol.
  MIMetadata(DebugLoc DL, MDNode *PCSections = nullptr, MDNode *MMRA = nullptr,
             Value *DeactivationSymbol = nullptr)
      : DL(std::move(DL)), PCSections(PCSections), MMRA(MMRA),
        DeactivationSymbol(DeactivationSymbol) {}
  /// Construct metadata from a DILocation and optional extra fields.
  ///
  /// \param DI Debug location to attach.
  /// \param PCSections Optional PC-sections metadata.
  /// \param MMRA Optional MMRA metadata.
  MIMetadata(const DILocation *DI, MDNode *PCSections = nullptr,
             MDNode *MMRA = nullptr)
      : DL(DI), PCSections(PCSections), MMRA(MMRA) {}
  /// Copy debug location and related metadata from an IR instruction.
  ///
  /// \param From IR instruction to copy metadata from.
  explicit MIMetadata(const Instruction &From)
      : DL(From.getDebugLoc()),
        PCSections(From.getMetadata(LLVMContext::MD_pcsections)),
        DeactivationSymbol(getDeactivationSymbol(&From)) {}
  /// Copy debug location and related metadata from a machine instruction.
  ///
  /// \param From Machine instruction to copy metadata from.
  explicit MIMetadata(const MachineInstr &From)
      : DL(From.getDebugLoc()), PCSections(From.getPCSections()),
        DeactivationSymbol(From.getDeactivationSymbol()) {}

  /// Return the debug location.
  ///
  /// \return The debug location.
  const DebugLoc &getDL() const { return DL; }
  /// Return the PC-sections metadata, or nullptr if none.
  ///
  /// \return The PC-sections metadata, or nullptr if none.
  MDNode *getPCSections() const { return PCSections; }
  /// Return the MMRA metadata, or nullptr if none.
  ///
  /// \return The MMRA metadata, or nullptr if none.
  MDNode *getMMRAMetadata() const { return MMRA; }
  /// Return the deactivation symbol, or nullptr if none.
  ///
  /// \return The deactivation symbol, or nullptr if none.
  Value *getDeactivationSymbol() const { return DeactivationSymbol; }

private:
  DebugLoc DL;
  MDNode *PCSections = nullptr;
  MDNode *MMRA = nullptr;
  Value *DeactivationSymbol = nullptr;

  static inline Value *getDeactivationSymbol(const Instruction *I) {
    if (auto *CB = dyn_cast<CallBase>(I))
      if (auto Bundle =
              CB->getOperandBundle(llvm::LLVMContext::OB_deactivation_symbol))
        return Bundle->Inputs[0].get();
    return nullptr;
  }
};

/// Helper class for constructing and mutating MachineInstrs.
class MachineInstrBuilder {
  MachineFunction *MF = nullptr;
  MachineInstr *MI = nullptr;

public:
  /// Construct a builder that does not refer to an instruction.
  MachineInstrBuilder() = default;

  /// Create a MachineInstrBuilder for manipulating an existing instruction.
  /// F must be the machine function that was used to allocate I.
  ///
  /// \param F Machine function that owns \p I.
  /// \param I Instruction to manipulate.
  MachineInstrBuilder(MachineFunction &F, MachineInstr *I) : MF(&F), MI(I) {}
  /// Create a MachineInstrBuilder for manipulating the instruction at \p I.
  ///
  /// \p F must be the machine function that was used to allocate \p I.
  ///
  /// \param F Machine function that owns \p I.
  /// \param I Iterator to the instruction to manipulate.
  MachineInstrBuilder(MachineFunction &F, MachineBasicBlock::iterator I)
      : MF(&F), MI(&*I) {}

  /// Allow automatic conversion to the machine instruction we are working on.
  ///
  /// \return Pointer to the machine instruction being built.
  operator MachineInstr*() const { return MI; }
  /// Access the machine instruction being built.
  ///
  /// \return Pointer to the machine instruction being built.
  MachineInstr *operator->() const { return MI; }
  /// Convert this builder to an iterator referring to the instruction.
  ///
  /// \return Iterator referring to the instruction being built.
  operator MachineBasicBlock::iterator() const { return MI; }

  /// If conversion operators fail, use this method to get the MachineInstr
  /// explicitly.
  ///
  /// \return Pointer to the machine instruction being built.
  MachineInstr *getInstr() const { return MI; }

  /// Get the register for the operand index.
  /// The operand at the index should be a register (asserted by
  /// MachineOperand).
  ///
  /// \param Idx Operand index whose register is returned.
  /// \return Register stored in the operand at \p Idx.
  Register getReg(unsigned Idx) const { return MI->getOperand(Idx).getReg(); }

  /// Add a new virtual register operand.
  ///
  /// \param RegNo Register to add.
  /// \param Flags Register-state flags for the operand.
  /// \param SubReg Optional subregister index.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addReg(Register RegNo, RegState Flags = {},
                                    unsigned SubReg = 0) const {
    assert(!hasRegState(Flags, RegState::_Reserved) &&
           "Passing in 'true' to addReg is forbidden! Use enums instead.");
    MI->addOperand(*MF, MachineOperand::CreateReg(
                            RegNo, hasRegState(Flags, RegState::Define),
                            hasRegState(Flags, RegState::Implicit),
                            hasRegState(Flags, RegState::Kill),
                            hasRegState(Flags, RegState::Dead),
                            hasRegState(Flags, RegState::Undef),
                            hasRegState(Flags, RegState::EarlyClobber), SubReg,
                            hasRegState(Flags, RegState::Debug),
                            hasRegState(Flags, RegState::InternalRead),
                            hasRegState(Flags, RegState::Renamable)));

    return *this;
  }

  /// Add a virtual register definition operand.
  ///
  /// \param RegNo Register being defined.
  /// \param Flags Additional register-state flags; \c Define is implied.
  /// \param SubReg Optional subregister index.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addDef(Register RegNo, RegState Flags = {},
                                    unsigned SubReg = 0) const {
    return addReg(RegNo, Flags | RegState::Define, SubReg);
  }

  /// Add a virtual register use operand. It is an error for Flags to contain
  /// `RegState::Define` when calling this function.
  ///
  /// \param RegNo Register being used.
  /// \param Flags Register-state flags; must not include \c RegState::Define.
  /// \param SubReg Optional subregister index.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addUse(Register RegNo, RegState Flags = {},
                                    unsigned SubReg = 0) const {
    assert(!hasRegState(Flags, RegState::Define) &&
           "Misleading addUse defines register, use addReg instead.");
    return addReg(RegNo, Flags, SubReg);
  }

  /// Add a new immediate operand.
  ///
  /// \param Val Immediate value to append.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addImm(int64_t Val) const {
    MI->addOperand(*MF, MachineOperand::CreateImm(Val));
    return *this;
  }

  /// Add a constant-integer immediate operand.
  ///
  /// \param Val Immediate value to append.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addCImm(const ConstantInt *Val) const {
    MI->addOperand(*MF, MachineOperand::CreateCImm(Val));
    return *this;
  }

  /// Add a floating-point immediate operand.
  ///
  /// \param Val Floating-point immediate to append.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addFPImm(const ConstantFP *Val) const {
    MI->addOperand(*MF, MachineOperand::CreateFPImm(Val));
    return *this;
  }

  /// Add a machine basic-block operand.
  ///
  /// \param MBB Basic block to reference.
  /// \param TargetFlags Optional target-specific operand flags.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addMBB(MachineBasicBlock *MBB,
                                    unsigned TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateMBB(MBB, TargetFlags));
    return *this;
  }

  /// Add a frame-index operand.
  ///
  /// \param Idx Abstract stack frame index.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addFrameIndex(int Idx) const {
    MI->addOperand(*MF, MachineOperand::CreateFI(Idx));
    return *this;
  }

  /// Add a constant-pool index operand.
  ///
  /// \param Idx Constant-pool index.
  /// \param Offset Byte offset from the pool entry.
  /// \param TargetFlags Optional target-specific operand flags.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &
  addConstantPoolIndex(unsigned Idx, int Offset = 0,
                       unsigned TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateCPI(Idx, Offset, TargetFlags));
    return *this;
  }

  /// Add a target-index operand.
  ///
  /// \param Idx Target-dependent index.
  /// \param Offset Byte offset from the indexed location.
  /// \param TargetFlags Optional target-specific operand flags.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addTargetIndex(unsigned Idx, int64_t Offset = 0,
                                          unsigned TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateTargetIndex(Idx, Offset,
                                                          TargetFlags));
    return *this;
  }

  /// Add a jump-table index operand.
  ///
  /// \param Idx Jump-table index.
  /// \param TargetFlags Optional target-specific operand flags.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addJumpTableIndex(unsigned Idx,
                                               unsigned TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateJTI(Idx, TargetFlags));
    return *this;
  }

  /// Add a global-address operand.
  ///
  /// \param GV Global value being referenced.
  /// \param Offset Byte offset from the global.
  /// \param TargetFlags Optional target-specific operand flags.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addGlobalAddress(const GlobalValue *GV,
                                              int64_t Offset = 0,
                                              unsigned TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateGA(GV, Offset, TargetFlags));
    return *this;
  }

  /// Add an external-symbol operand.
  ///
  /// \param FnName Name of the external symbol.
  /// \param TargetFlags Optional target-specific operand flags.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addExternalSymbol(const char *FnName,
                                               unsigned TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateES(FnName, TargetFlags));
    return *this;
  }

  /// Add a block-address operand.
  ///
  /// \param BA IR block address being referenced.
  /// \param Offset Byte offset from the block.
  /// \param TargetFlags Optional target-specific operand flags.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addBlockAddress(const BlockAddress *BA,
                                             int64_t Offset = 0,
                                             unsigned TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateBA(BA, Offset, TargetFlags));
    return *this;
  }

  /// Add a register-mask operand of preserved registers.
  ///
  /// \param Mask Bitmask of registers preserved by this instruction.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addRegMask(const uint32_t *Mask) const {
    MI->addOperand(*MF, MachineOperand::CreateRegMask(Mask));
    return *this;
  }

  /// Add a memory operand to the instruction.
  ///
  /// \param MMO Memory operand describing a memory access.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addMemOperand(MachineMemOperand *MMO) const {
    MI->addMemOperand(*MF, MMO);
    return *this;
  }

  /// Replace the instruction's memory operands with \p MMOs.
  ///
  /// \param MMOs Memory operands to attach.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &
  setMemRefs(ArrayRef<MachineMemOperand *> MMOs) const {
    MI->setMemRefs(*MF, MMOs);
    return *this;
  }

  /// Copy memory operands from \p OtherMI onto this instruction.
  ///
  /// \param OtherMI Instruction whose memory operands are cloned.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &cloneMemRefs(const MachineInstr &OtherMI) const {
    MI->cloneMemRefs(*MF, OtherMI);
    return *this;
  }

  /// Merge and copy memory operands from \p OtherMIs onto this instruction.
  ///
  /// \param OtherMIs Instructions whose memory operands are merged and cloned.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &
  cloneMergedMemRefs(ArrayRef<const MachineInstr *> OtherMIs) const {
    MI->cloneMergedMemRefs(*MF, OtherMIs);
    return *this;
  }

  /// Add an existing machine operand to the instruction.
  ///
  /// \param MO Operand to append.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &add(const MachineOperand &MO) const {
    MI->addOperand(*MF, MO);
    return *this;
  }

  /// Add each machine operand in \p MOs to the instruction.
  ///
  /// \param MOs Operands to append in order.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &add(ArrayRef<MachineOperand> MOs) const {
    for (const MachineOperand &MO : MOs)
      MI->addOperand(*MF, MO);
    return *this;
  }

  /// Add a metadata operand.
  ///
  /// \param MD Metadata node to append.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addMetadata(const MDNode *MD) const {
    MI->addOperand(*MF, MachineOperand::CreateMetadata(MD));
    assert((MI->isDebugValueLike() ? static_cast<bool>(MI->getDebugVariable())
                                   : true) &&
           "first MDNode argument of a DBG_VALUE not a variable");
    assert((MI->isDebugLabel() ? static_cast<bool>(MI->getDebugLabel())
                               : true) &&
           "first MDNode argument of a DBG_LABEL not a label");
    return *this;
  }

  /// Add a CFI-instruction index operand.
  ///
  /// \param CFIIndex Index into the function's CFI instruction list.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addCFIIndex(unsigned CFIIndex) const {
    MI->addOperand(*MF, MachineOperand::CreateCFIIndex(CFIIndex));
    return *this;
  }

  /// Add an intrinsic-ID operand.
  ///
  /// \param ID Intrinsic identifier.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addIntrinsicID(Intrinsic::ID ID) const {
    MI->addOperand(*MF, MachineOperand::CreateIntrinsicID(ID));
    return *this;
  }

  /// Add an integer-comparison predicate operand.
  ///
  /// \param Pred Predicate to append.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addPredicate(CmpInst::Predicate Pred) const {
    MI->addOperand(*MF, MachineOperand::CreatePredicate(Pred));
    return *this;
  }

  /// Add a shuffle-mask operand.
  ///
  /// \param Val Shuffle-mask elements.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addShuffleMask(ArrayRef<int> Val) const {
    MI->addOperand(*MF, MachineOperand::CreateShuffleMask(Val));
    return *this;
  }

  /// Add a lane-mask operand.
  ///
  /// \param LaneMask Mask of active lanes.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addLaneMask(LaneBitmask LaneMask) const {
    MI->addOperand(*MF, MachineOperand::CreateLaneMask(LaneMask));
    return *this;
  }

  /// Add an MCSymbol operand.
  ///
  /// \param Sym Symbol to reference.
  /// \param TargetFlags Optional target-specific operand flags.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addSym(MCSymbol *Sym,
                                    unsigned char TargetFlags = 0) const {
    MI->addOperand(*MF, MachineOperand::CreateMCSymbol(Sym, TargetFlags));
    return *this;
  }

  /// Replace the instruction's MI flags with \p Flags.
  ///
  /// \param Flags Combined \c MachineInstr::MIFlag bits to store.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &setMIFlags(unsigned Flags) const {
    MI->setFlags(Flags);
    return *this;
  }

  /// Set a single MI flag on the instruction.
  ///
  /// \param Flag Flag bit to set.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &setMIFlag(MachineInstr::MIFlag Flag) const {
    MI->setFlag(Flag);
    return *this;
  }

  /// Mark the operand at \p OpIdx as a dead definition.
  ///
  /// \param OpIdx Index of the operand to mark dead.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &setOperandDead(unsigned OpIdx) const {
    MI->getOperand(OpIdx).setIsDead();
    return *this;
  }

  /// Add a displacement derived from \p Disp with an added offset.
  ///
  /// If \p TargetFlags is zero, the flags are copied from \p Disp. Callers
  /// that want to clear flags must pass them explicitly.
  ///
  /// \param Disp Existing address operand to offset.
  /// \param off Additional offset applied to \p Disp.
  /// \param TargetFlags Optional replacement target-specific flags.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &addDisp(const MachineOperand &Disp, int64_t off,
                                     unsigned char TargetFlags = 0) const {
    // If caller specifies new TargetFlags then use it, otherwise the
    // default behavior is to copy the target flags from the existing
    // MachineOperand. This means if the caller wants to clear the
    // target flags it needs to do so explicitly.
    if (0 == TargetFlags)
      TargetFlags = Disp.getTargetFlags();

    switch (Disp.getType()) {
      default:
        llvm_unreachable("Unhandled operand type in addDisp()");
      case MachineOperand::MO_Immediate:
        return addImm(Disp.getImm() + off);
      case MachineOperand::MO_ConstantPoolIndex:
        return addConstantPoolIndex(Disp.getIndex(), Disp.getOffset() + off,
                                    TargetFlags);
      case MachineOperand::MO_GlobalAddress:
        return addGlobalAddress(Disp.getGlobal(), Disp.getOffset() + off,
                                TargetFlags);
      case MachineOperand::MO_BlockAddress:
        return addBlockAddress(Disp.getBlockAddress(), Disp.getOffset() + off,
                               TargetFlags);
      case MachineOperand::MO_JumpTableIndex:
        assert(off == 0 && "cannot create offset into jump tables");
        return addJumpTableIndex(Disp.getIndex(), TargetFlags);
    }
  }

  /// Copy PC-sections, MMRA, and deactivation-symbol metadata from \p MIMD.
  ///
  /// \param MIMD Metadata bundle whose extra fields are copied.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &copyMIMetadata(const MIMetadata &MIMD) const {
    if (MIMD.getPCSections())
      MI->setPCSections(*MF, MIMD.getPCSections());
    if (MIMD.getMMRAMetadata())
      MI->setMMRAMetadata(*MF, MIMD.getMMRAMetadata());
    if (MIMD.getDeactivationSymbol())
      MI->setDeactivationSymbol(*MF, MIMD.getDeactivationSymbol());
    return *this;
  }

  /// Copy all the implicit operands from OtherMI onto this one.
  ///
  /// \param OtherMI Instruction whose implicit operands are copied.
  /// \return Reference to this builder for chaining.
  const MachineInstrBuilder &
  copyImplicitOps(const MachineInstr &OtherMI) const {
    MI->copyImplicitOps(*MF, OtherMI);
    return *this;
  }

  /// Constrain every register operand to the instruction's register classes.
  ///
  /// \param TII Target instruction info used to insert copies if needed.
  /// \param TRI Target register info for register-class queries.
  /// \param RBI Register-bank info used when constraining generic vregs.
  void constrainAllUses(const TargetInstrInfo &TII,
                        const TargetRegisterInfo &TRI,
                        const RegisterBankInfo &RBI) const {
    constrainSelectedInstRegOperands(*MI, TII, TRI, RBI);
  }
};

/// Builder interface. Specify how to create the initial instruction itself.
///
/// \param MF Machine function that will own the new instruction.
/// \param MIMD Debug location and related metadata for the instruction.
/// \param MCID Opcode descriptor for the new instruction.
/// \return Builder for the newly created instruction.
inline MachineInstrBuilder BuildMI(MachineFunction &MF, const MIMetadata &MIMD,
                                   const MCInstrDesc &MCID) {
  return MachineInstrBuilder(MF, MF.CreateMachineInstr(MCID, MIMD.getDL()))
      .copyMIMetadata(MIMD);
}

/// This version of the builder sets up the first operand as a
/// destination virtual register.
///
/// \param MF Machine function that will own the new instruction.
/// \param MIMD Debug location and related metadata for the instruction.
/// \param MCID Opcode descriptor for the new instruction.
/// \param DestReg Destination virtual register used as the first operand.
/// \return Builder for the newly created instruction.
inline MachineInstrBuilder BuildMI(MachineFunction &MF, const MIMetadata &MIMD,
                                   const MCInstrDesc &MCID, Register DestReg) {
  return MachineInstrBuilder(MF, MF.CreateMachineInstr(MCID, MIMD.getDL()))
      .copyMIMetadata(MIMD)
      .addReg(DestReg, RegState::Define);
}

/// Insert a new instruction with a destination register before \p I.
///
/// This version of the builder inserts the newly-built instruction before
/// the given position in the given MachineBasicBlock, and sets up the first
/// operand as a destination virtual register.
///
/// \param BB Basic block that will contain the new instruction.
/// \param I Insertion point; the instruction is placed before this iterator.
/// \param MIMD Debug location and related metadata for the instruction.
/// \param MCID Opcode descriptor for the new instruction.
/// \param DestReg Destination virtual register used as the first operand.
/// \return Builder for the newly created instruction.
inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB,
                                   MachineBasicBlock::iterator I,
                                   const MIMetadata &MIMD,
                                   const MCInstrDesc &MCID, Register DestReg) {
  MachineFunction &MF = *BB.getParent();
  MachineInstr *MI = MF.CreateMachineInstr(MCID, MIMD.getDL());
  BB.insert(I, MI);
  return MachineInstrBuilder(MF, MI).copyMIMetadata(MIMD).addReg(
      DestReg, RegState::Define);
}

/// Insert a new instruction with a destination register before \p I.
///
/// This version of the builder inserts the newly-built instruction before
/// the given position in the given MachineBasicBlock, and sets up the first
/// operand as a destination virtual register.
///
/// If \c I is inside a bundle, then the newly inserted \a MachineInstr is
/// added to the same bundle.
///
/// \param BB Basic block that will contain the new instruction.
/// \param I Insertion point; the instruction is placed before this iterator.
/// \param MIMD Debug location and related metadata for the instruction.
/// \param MCID Opcode descriptor for the new instruction.
/// \param DestReg Destination virtual register used as the first operand.
/// \return Builder for the newly created instruction.
inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB,
                                   MachineBasicBlock::instr_iterator I,
                                   const MIMetadata &MIMD,
                                   const MCInstrDesc &MCID, Register DestReg) {
  MachineFunction &MF = *BB.getParent();
  MachineInstr *MI = MF.CreateMachineInstr(MCID, MIMD.getDL());
  BB.insert(I, MI);
  return MachineInstrBuilder(MF, MI).copyMIMetadata(MIMD).addReg(
      DestReg, RegState::Define);
}

/// Insert a new instruction with a destination register before \p I.
///
/// If \p I is inside a bundle, the new instruction is added to that bundle.
///
/// \param BB Basic block that will contain the new instruction.
/// \param I Instruction to insert before.
/// \param MIMD Debug location and related metadata for the instruction.
/// \param MCID Opcode descriptor for the new instruction.
/// \param DestReg Destination virtual register used as the first operand.
/// \return Builder for the newly created instruction.
inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB, MachineInstr &I,
                                   const MIMetadata &MIMD,
                                   const MCInstrDesc &MCID, Register DestReg) {
  // Calling the overload for instr_iterator is always correct.  However, the
  // definition is not available in headers, so inline the check.
  if (I.isInsideBundle())
    return BuildMI(BB, MachineBasicBlock::instr_iterator(I), MIMD, MCID,
                   DestReg);
  return BuildMI(BB, MachineBasicBlock::iterator(I), MIMD, MCID, DestReg);
}

/// Insert a new instruction with a destination register before \p I.
///
/// \param BB Basic block that will contain the new instruction.
/// \param I Instruction to insert before.
/// \param MIMD Debug location and related metadata for the instruction.
/// \param MCID Opcode descriptor for the new instruction.
/// \param DestReg Destination virtual register used as the first operand.
/// \return Builder for the newly created instruction.
inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB, MachineInstr *I,
                                   const MIMetadata &MIMD,
                                   const MCInstrDesc &MCID, Register DestReg) {
  return BuildMI(BB, *I, MIMD, MCID, DestReg);
}

/// Insert a new instruction before \p I without a destination register.
///
/// This version of the builder inserts the newly-built instruction before the
/// given position in the given MachineBasicBlock, and does NOT take a
/// destination register.
///
/// \param BB Basic block that will contain the new instruction.
/// \param I Insertion point; the instruction is placed before this iterator.
/// \param MIMD Debug location and related metadata for the instruction.
/// \param MCID Opcode descriptor for the new instruction.
/// \return Builder for the newly created instruction.
inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB,
                                   MachineBasicBlock::iterator I,
                                   const MIMetadata &MIMD,
                                   const MCInstrDesc &MCID) {
  MachineFunction &MF = *BB.getParent();
  MachineInstr *MI = MF.CreateMachineInstr(MCID, MIMD.getDL());
  BB.insert(I, MI);
  return MachineInstrBuilder(MF, MI).copyMIMetadata(MIMD);
}

/// Insert a new instruction before \p I without a destination register.
///
/// \param BB Basic block that will contain the new instruction.
/// \param I Insertion point; the instruction is placed before this iterator.
/// \param MIMD Debug location and related metadata for the instruction.
/// \param MCID Opcode descriptor for the new instruction.
/// \return Builder for the newly created instruction.
inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB,
                                   MachineBasicBlock::instr_iterator I,
                                   const MIMetadata &MIMD,
                                   const MCInstrDesc &MCID) {
  MachineFunction &MF = *BB.getParent();
  MachineInstr *MI = MF.CreateMachineInstr(MCID, MIMD.getDL());
  BB.insert(I, MI);
  return MachineInstrBuilder(MF, MI).copyMIMetadata(MIMD);
}

/// Insert a new instruction before \p I without a destination register.
///
/// If \p I is inside a bundle, the new instruction is added to that bundle.
///
/// \param BB Basic block that will contain the new instruction.
/// \param I Instruction to insert before.
/// \param MIMD Debug location and related metadata for the instruction.
/// \param MCID Opcode descriptor for the new instruction.
/// \return Builder for the newly created instruction.
inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB, MachineInstr &I,
                                   const MIMetadata &MIMD,
                                   const MCInstrDesc &MCID) {
  // Calling the overload for instr_iterator is always correct.  However, the
  // definition is not available in headers, so inline the check.
  if (I.isInsideBundle())
    return BuildMI(BB, MachineBasicBlock::instr_iterator(I), MIMD, MCID);
  return BuildMI(BB, MachineBasicBlock::iterator(I), MIMD, MCID);
}

/// Insert a new instruction before \p I without a destination register.
///
/// \param BB Basic block that will contain the new instruction.
/// \param I Instruction to insert before.
/// \param MIMD Debug location and related metadata for the instruction.
/// \param MCID Opcode descriptor for the new instruction.
/// \return Builder for the newly created instruction.
inline MachineInstrBuilder BuildMI(MachineBasicBlock &BB, MachineInstr *I,
                                   const MIMetadata &MIMD,
                                   const MCInstrDesc &MCID) {
  return BuildMI(BB, *I, MIMD, MCID);
}

/// This version of the builder inserts the newly-built instruction at the end
/// of the given MachineBasicBlock, and does NOT take a destination register.
///
/// \param BB Basic block that receives the instruction at the end.
/// \param MIMD Debug location and related metadata for the instruction.
/// \param MCID Opcode descriptor for the new instruction.
/// \return Builder for the newly created instruction.
inline MachineInstrBuilder BuildMI(MachineBasicBlock *BB,
                                   const MIMetadata &MIMD,
                                   const MCInstrDesc &MCID) {
  return BuildMI(*BB, BB->end(), MIMD, MCID);
}

/// Append a new instruction with a destination register to \p BB.
///
/// This version of the builder inserts the newly-built instruction at the
/// end of the given MachineBasicBlock, and sets up the first operand as a
/// destination virtual register.
///
/// \param BB Basic block that receives the instruction at the end.
/// \param MIMD Debug location and related metadata for the instruction.
/// \param MCID Opcode descriptor for the new instruction.
/// \param DestReg Destination virtual register used as the first operand.
/// \return Builder for the newly created instruction.
inline MachineInstrBuilder BuildMI(MachineBasicBlock *BB,
                                   const MIMetadata &MIMD,
                                   const MCInstrDesc &MCID, Register DestReg) {
  return BuildMI(*BB, BB->end(), MIMD, MCID, DestReg);
}

/// Build a DBG_VALUE for a register or a register-indirect address.
///
/// This version of the builder builds a DBG_VALUE intrinsic for either a
/// value in a register or a register-indirect address. The convention is
/// that a DBG_VALUE is indirect iff the second operand is an immediate.
///
/// \param MF Machine function that will own the new instruction.
/// \param DL Debug location for the new instruction.
/// \param MCID Opcode descriptor for the DBG_VALUE.
/// \param IsIndirect True if the DBG_VALUE is a register-indirect address.
/// \param Reg Register that holds the value or the indirect address.
/// \param Variable Metadata node identifying the source variable.
/// \param Expr Expression that computes the variable from the operands.
/// \return Builder for the newly created DBG_VALUE.
LLVM_ABI MachineInstrBuilder BuildMI(MachineFunction &MF, const DebugLoc &DL,
                                     const MCInstrDesc &MCID, bool IsIndirect,
                                     Register Reg, const MDNode *Variable,
                                     const MDNode *Expr);

/// This version of the builder builds a DBG_VALUE or DBG_VALUE_LIST intrinsic
/// for a MachineOperand.
///
/// \param MF Machine function that will own the new instruction.
/// \param DL Debug location for the new instruction.
/// \param MCID Opcode descriptor for the debug instruction.
/// \param IsIndirect True if the DBG_VALUE is a register-indirect address.
/// \param MOs Operands that locate the debug value.
/// \param Variable Metadata node identifying the source variable.
/// \param Expr Expression that computes the variable from the operands.
/// \return Builder for the newly created debug instruction.
LLVM_ABI MachineInstrBuilder BuildMI(MachineFunction &MF, const DebugLoc &DL,
                                     const MCInstrDesc &MCID, bool IsIndirect,
                                     ArrayRef<MachineOperand> MOs,
                                     const MDNode *Variable,
                                     const MDNode *Expr);

/// This version of the builder builds a DBG_VALUE intrinsic
/// for either a value in a register or a register-indirect
/// address and inserts it at position I.
///
/// \param BB Basic block that will contain the new instruction.
/// \param I Insertion point; the instruction is placed before this iterator.
/// \param DL Debug location for the new instruction.
/// \param MCID Opcode descriptor for the DBG_VALUE.
/// \param IsIndirect True if the DBG_VALUE is a register-indirect address.
/// \param Reg Register that holds the value or the indirect address.
/// \param Variable Metadata node identifying the source variable.
/// \param Expr Expression that computes the variable from the operands.
/// \return Builder for the newly created DBG_VALUE.
LLVM_ABI MachineInstrBuilder BuildMI(MachineBasicBlock &BB,
                                     MachineBasicBlock::iterator I,
                                     const DebugLoc &DL,
                                     const MCInstrDesc &MCID, bool IsIndirect,
                                     Register Reg, const MDNode *Variable,
                                     const MDNode *Expr);

/// This version of the builder builds a DBG_VALUE, DBG_INSTR_REF, or
/// DBG_VALUE_LIST intrinsic for a machine operand and inserts it at position I.
///
/// \param BB Basic block that will contain the new instruction.
/// \param I Insertion point; the instruction is placed before this iterator.
/// \param DL Debug location for the new instruction.
/// \param MCID Opcode descriptor for the debug instruction.
/// \param IsIndirect True if the DBG_VALUE is a register-indirect address.
/// \param MOs Operands that locate the debug value.
/// \param Variable Metadata node identifying the source variable.
/// \param Expr Expression that computes the variable from the operands.
/// \return Builder for the newly created debug instruction.
LLVM_ABI MachineInstrBuilder BuildMI(
    MachineBasicBlock &BB, MachineBasicBlock::iterator I, const DebugLoc &DL,
    const MCInstrDesc &MCID, bool IsIndirect, ArrayRef<MachineOperand> MOs,
    const MDNode *Variable, const MDNode *Expr);

/// Clone a DBG_VALUE whose value has been spilled to FrameIndex.
///
/// \param BB Basic block that will contain the cloned instruction.
/// \param I Insertion point for the cloned DBG_VALUE.
/// \param Orig Original DBG_VALUE being rewritten for the spill.
/// \param FrameIndex Frame index where the spilled value now lives.
/// \param SpillReg Register whose debug uses were spilled.
/// \return The cloned DBG_VALUE instruction.
LLVM_ABI MachineInstr *buildDbgValueForSpill(MachineBasicBlock &BB,
                                             MachineBasicBlock::iterator I,
                                             const MachineInstr &Orig,
                                             int FrameIndex, Register SpillReg);
/// Clone a DBG_VALUE after spilling the given operands to \p FrameIndex.
///
/// \param BB Basic block that will contain the cloned instruction.
/// \param I Insertion point for the cloned DBG_VALUE.
/// \param Orig Original DBG_VALUE being rewritten for the spill.
/// \param FrameIndex Frame index where the spilled values now live.
/// \param SpilledOperands Operands of \p Orig that were spilled.
/// \return The cloned DBG_VALUE instruction.
LLVM_ABI MachineInstr *buildDbgValueForSpill(
    MachineBasicBlock &BB, MachineBasicBlock::iterator I,
    const MachineInstr &Orig, int FrameIndex,
    const SmallVectorImpl<const MachineOperand *> &SpilledOperands);

/// Update a DBG_VALUE whose value has been spilled to FrameIndex. Useful when
/// modifying an instruction in place while iterating over a basic block.
///
/// \param Orig DBG_VALUE to rewrite in place.
/// \param FrameIndex Frame index where the spilled value now lives.
/// \param Reg Register whose debug operands should become the frame index.
LLVM_ABI void updateDbgValueForSpill(MachineInstr &Orig, int FrameIndex,
                                     Register Reg);

/// Helper class for constructing bundles of MachineInstrs.
///
/// MIBundleBuilder can create a bundle from scratch by inserting new
/// MachineInstrs one at a time, or it can create a bundle from a sequence of
/// existing MachineInstrs in a basic block.
class MIBundleBuilder {
  MachineBasicBlock &MBB;
  MachineBasicBlock::instr_iterator Begin;
  MachineBasicBlock::instr_iterator End;

public:
  /// Create an MIBundleBuilder that inserts instructions into a new bundle in
  /// BB above the bundle or instruction at Pos.
  ///
  /// \param BB Basic block that will contain the new bundle.
  /// \param Pos Insertion point; the bundle is created before this position.
  MIBundleBuilder(MachineBasicBlock &BB, MachineBasicBlock::iterator Pos)
      : MBB(BB), Begin(Pos.getInstrIterator()), End(Begin) {}

  /// Create a bundle from the sequence of instructions between B and E.
  ///
  /// \param BB Basic block containing the instructions to bundle.
  /// \param B Iterator to the first instruction in the bundle (inclusive).
  /// \param E Iterator past the last instruction in the bundle (exclusive).
  MIBundleBuilder(MachineBasicBlock &BB, MachineBasicBlock::iterator B,
                  MachineBasicBlock::iterator E)
      : MBB(BB), Begin(B.getInstrIterator()), End(E.getInstrIterator()) {
    assert(B != E && "No instructions to bundle");
    ++B;
    while (B != E) {
      MachineInstr &MI = *B;
      ++B;
      MI.bundleWithPred();
    }
  }

  /// Create an MIBundleBuilder representing an existing instruction or bundle
  /// that has MI as its head.
  ///
  /// \param MI Head instruction of the existing instruction or bundle.
  explicit MIBundleBuilder(MachineInstr *MI)
      : MBB(*MI->getParent()), Begin(MI),
        End(getBundleEnd(MI->getIterator())) {}

  /// Return a reference to the basic block containing this bundle.
  ///
  /// \return Basic block that contains this bundle.
  MachineBasicBlock &getMBB() const { return MBB; }

  /// Return true if no instructions have been inserted in this bundle yet.
  /// Empty bundles aren't representable in a MachineBasicBlock.
  ///
  /// \return True if the bundle contains no instructions yet.
  bool empty() const { return Begin == End; }

  /// Return an iterator to the first bundled instruction.
  ///
  /// \return Iterator to the first bundled instruction.
  MachineBasicBlock::instr_iterator begin() const { return Begin; }

  /// Return an iterator beyond the last bundled instruction.
  ///
  /// \return Iterator past the last bundled instruction.
  MachineBasicBlock::instr_iterator end() const { return End; }

  /// Insert MI into this bundle before I which must point to an instruction in
  /// the bundle, or end().
  ///
  /// \param I Insertion iterator inside the bundle, or \c end().
  /// \param MI Instruction to insert into the bundle.
  /// \return Reference to this builder for chaining.
  MIBundleBuilder &insert(MachineBasicBlock::instr_iterator I,
                          MachineInstr *MI) {
    MBB.insert(I, MI);
    if (I == Begin) {
      if (!empty())
        MI->bundleWithSucc();
      Begin = MI->getIterator();
      return *this;
    }
    if (I == End) {
      MI->bundleWithPred();
      return *this;
    }
    // MI was inserted in the middle of the bundle, so its neighbors' flags are
    // already fine. Update MI's bundle flags manually.
    MI->setFlag(MachineInstr::BundledPred);
    MI->setFlag(MachineInstr::BundledSucc);
    return *this;
  }

  /// Insert MI into MBB by prepending it to the instructions in the bundle.
  /// MI will become the first instruction in the bundle.
  ///
  /// \param MI Instruction to insert at the front of the bundle.
  /// \return Reference to this builder for chaining.
  MIBundleBuilder &prepend(MachineInstr *MI) {
    return insert(begin(), MI);
  }

  /// Insert MI into MBB by appending it to the instructions in the bundle.
  /// MI will become the last instruction in the bundle.
  ///
  /// \param MI Instruction to insert at the end of the bundle.
  /// \return Reference to this builder for chaining.
  MIBundleBuilder &append(MachineInstr *MI) {
    return insert(end(), MI);
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEINSTRBUILDER_H
