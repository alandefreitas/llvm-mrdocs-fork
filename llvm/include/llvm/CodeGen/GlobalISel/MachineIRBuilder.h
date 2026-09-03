//===-- llvm/CodeGen/GlobalISel/MachineIRBuilder.h - MIBuilder --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares the MachineIRBuilder class.
/// This is a helper class to build MachineInstr.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_MACHINEIRBUILDER_H
#define LLVM_CODEGEN_GLOBALISEL_MACHINEIRBUILDER_H

#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

// Forward declarations.
class APInt;
class BlockAddress;
class Constant;
class ConstantFP;
class ConstantInt;
class DataLayout;
class GISelCSEInfo;
class GlobalValue;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class MachineFunction;
class MachineInstr;
class TargetInstrInfo;
class GISelChangeObserver;

/// Holds insertion and metadata state for a MachineIRBuilder.
///
/// Since MachineIRBuilders only store state in this object, builder state
/// can be transferred between different kinds of MachineIRBuilders.
struct MachineIRBuilderState {
  /// MachineFunction under construction.
  MachineFunction *MF = nullptr;
  /// Information used to access the description of the opcodes.
  const TargetInstrInfo *TII = nullptr;
  /// Information used to verify types are consistent and to create virtual registers.
  MachineRegisterInfo *MRI = nullptr;
  /// Debug location to be set to any instruction we create.
  DebugLoc DL;
  /// PC sections metadata to be set to any instruction we create.
  MDNode *PCSections = nullptr;
  /// MMRA Metadata to be set on any instruction we create.
  MDNode *MMRA = nullptr;
  /// Optional deactivation symbol for built instructions.
  Value *DS = nullptr;

  /// \name Fields describing the insertion point.
  /// @{
  /// Basic block receiving newly built instructions.
  MachineBasicBlock *MBB = nullptr;
  /// Insertion point iterator inside MBB.
  MachineBasicBlock::iterator II;
  /// @}

  /// Optional observer notified when instructions are created.
  GISelChangeObserver *Observer = nullptr;

  /// Optional CSE info consulted while building instructions.
  GISelCSEInfo *CSEInfo = nullptr;
};

/// Destination operand accepted by MachineIRBuilder helpers.
class DstOp {
  union {
    /// Explicit LLT when the destination type is given directly.
    LLT LLTTy;
    /// Existing destination register.
    Register Reg;
    /// Register class used to create a new virtual register.
    const TargetRegisterClass *RC;
    /// Register attributes used to create a new virtual register.
    MachineRegisterInfo::VRegAttrs Attrs;
  };

public:
  /// Discriminator for the active union member of a DstOp.
  enum class DstType {
    Ty_LLT,       ///< Destination described by an LLT.
    Ty_Reg,       ///< Destination is an existing register.
    Ty_RC,        ///< Destination created from a register class.
    Ty_VRegAttrs  ///< Destination created from VRegAttrs.
  };
  /// Construct a destination from a raw register number.
  /// \param R Register number used as the destination.
  DstOp(unsigned R) : Reg(R), Ty(DstType::Ty_Reg) {}
  /// Construct a destination from a Register.
  /// \param R Destination register.
  DstOp(Register R) : Reg(R), Ty(DstType::Ty_Reg) {}
  /// Construct a destination from a machine operand's register.
  /// \param Op Operand providing the destination register.
  DstOp(const MachineOperand &Op) : Reg(Op.getReg()), Ty(DstType::Ty_Reg) {}
  /// Construct a destination described only by an LLT.
  /// \param T Type used when creating a new virtual register.
  DstOp(const LLT T) : LLTTy(T), Ty(DstType::Ty_LLT) {}
  /// Construct a destination from a target register class.
  /// \param TRC Register class used to create a new virtual register.
  DstOp(const TargetRegisterClass *TRC) : RC(TRC), Ty(DstType::Ty_RC) {}
  /// Construct a destination from virtual-register attributes.
  /// \param Attrs Attributes used to create a new virtual register.
  DstOp(MachineRegisterInfo::VRegAttrs Attrs)
      : Attrs(Attrs), Ty(DstType::Ty_VRegAttrs) {}
  /// Construct a destination from a regclass/bank and type.
  /// \param RCOrRB Register class or bank for the new virtual register.
  /// \param Ty Type of the new virtual register.
  DstOp(RegClassOrRegBank RCOrRB, LLT Ty)
      : Attrs({RCOrRB, Ty}), Ty(DstType::Ty_VRegAttrs) {}

  /// Add this destination as a definition on \p MIB.
  /// \param MRI Register info used to create virtual registers.
  /// \param MIB Instruction builder receiving the definition.
  void addDefToMIB(MachineRegisterInfo &MRI, MachineInstrBuilder &MIB) const {
    switch (Ty) {
    case DstType::Ty_Reg:
      MIB.addDef(Reg);
      break;
    case DstType::Ty_LLT:
      MIB.addDef(MRI.createGenericVirtualRegister(LLTTy));
      break;
    case DstType::Ty_RC:
      MIB.addDef(MRI.createVirtualRegister(RC));
      break;
    case DstType::Ty_VRegAttrs:
      MIB.addDef(MRI.createVirtualRegister(Attrs));
      break;
    }
  }

  /// Return the LLT of this destination.
  /// \param MRI Register info used when the destination is a register.
  /// \return The LLT of this destination.
  LLT getLLTTy(const MachineRegisterInfo &MRI) const {
    switch (Ty) {
    case DstType::Ty_RC:
      return LLT{};
    case DstType::Ty_LLT:
      return LLTTy;
    case DstType::Ty_Reg:
      return MRI.getType(Reg);
    case DstType::Ty_VRegAttrs:
      return Attrs.Ty;
    }
    llvm_unreachable("Unrecognised DstOp::DstType enum");
  }

  /// Return the destination register.
  /// \pre The destination kind is Ty_Reg.
  /// \return The destination register.
  Register getReg() const {
    assert(Ty == DstType::Ty_Reg && "Not a register");
    return Reg;
  }

  /// Return the destination register class.
  /// \pre The destination kind is Ty_RC.
  /// \return The destination register class.
  const TargetRegisterClass *getRegClass() const {
    assert(Ty == DstType::Ty_RC && "Not a RC Operand");
    return RC;
  }

  /// Return the destination virtual-register attributes.
  /// \pre The destination kind is Ty_VRegAttrs.
  /// \return The destination virtual-register attributes.
  MachineRegisterInfo::VRegAttrs getVRegAttrs() const {
    assert(Ty == DstType::Ty_VRegAttrs && "Not a VRegAttrs Operand");
    return Attrs;
  }

  /// Return the discriminator for the active destination representation.
  /// \return The discriminator for the active destination representation.
  DstType getDstOpKind() const { return Ty; }

private:
  DstType Ty;
};

/// Source operand accepted by MachineIRBuilder helpers.
class SrcOp {
  union {
    /// Instruction whose first def is used as the source register.
    MachineInstrBuilder SrcMIB;
    /// Existing source register.
    Register Reg;
    /// Packed compare predicate.
    CmpInst::Predicate Pred;
    /// Packed immediate value.
    int64_t Imm;
  };

public:
  /// Discriminator for the active union member of a SrcOp.
  enum class SrcType {
    Ty_Reg,       ///< Source is an existing register.
    Ty_MIB,       ///< Source is the first def of a MachineInstrBuilder.
    Ty_Predicate, ///< Source is a compare predicate.
    Ty_Imm        ///< Source is an immediate.
  };
  /// Construct a source from a Register.
  /// \param R Source register.
  SrcOp(Register R) : Reg(R), Ty(SrcType::Ty_Reg) {}
  /// Construct a source from a machine operand's register.
  /// \param Op Operand providing the source register.
  SrcOp(const MachineOperand &Op) : Reg(Op.getReg()), Ty(SrcType::Ty_Reg) {}
  /// Construct a source from a MachineInstrBuilder's first definition.
  /// \param MIB Builder whose first def is used as the source.
  SrcOp(const MachineInstrBuilder &MIB) : SrcMIB(MIB), Ty(SrcType::Ty_MIB) {}
  /// Construct a source from a compare predicate.
  /// \param P Predicate packed as the source operand.
  SrcOp(const CmpInst::Predicate P) : Pred(P), Ty(SrcType::Ty_Predicate) {}
  /// Deleted: integer register numbers are ambiguous with immediates.
  ///
  /// Use of registers held in unsigned integer variables (or more rarely
  /// signed integers) is no longer permitted to avoid ambiguity with
  /// upcoming support for immediates.
  /// \param Unused Ignored; this overload is deleted.
  SrcOp(unsigned Unused) = delete;
  /// Deleted: integer register numbers are ambiguous with immediates.
  /// \param Unused Ignored; this overload is deleted.
  SrcOp(int Unused) = delete;
  /// Construct a source from an unsigned immediate.
  /// \param V Immediate value packed as the source operand.
  SrcOp(uint64_t V) : Imm(V), Ty(SrcType::Ty_Imm) {}
  /// Construct a source from a signed immediate.
  /// \param V Immediate value packed as the source operand.
  SrcOp(int64_t V) : Imm(V), Ty(SrcType::Ty_Imm) {}

  /// Add this source as a use on \p MIB.
  /// \param MIB Instruction builder receiving the use.
  void addSrcToMIB(MachineInstrBuilder &MIB) const {
    switch (Ty) {
    case SrcType::Ty_Predicate:
      MIB.addPredicate(Pred);
      break;
    case SrcType::Ty_Reg:
      MIB.addUse(Reg);
      break;
    case SrcType::Ty_MIB:
      MIB.addUse(SrcMIB->getOperand(0).getReg());
      break;
    case SrcType::Ty_Imm:
      MIB.addImm(Imm);
      break;
    }
  }

  /// Return the LLT of this register-like source.
  /// \param MRI Register info used to look up register types.
  /// \return The LLT of this register-like source.
  LLT getLLTTy(const MachineRegisterInfo &MRI) const {
    switch (Ty) {
    case SrcType::Ty_Predicate:
    case SrcType::Ty_Imm:
      llvm_unreachable("Not a register operand");
    case SrcType::Ty_Reg:
      return MRI.getType(Reg);
    case SrcType::Ty_MIB:
      return MRI.getType(SrcMIB->getOperand(0).getReg());
    }
    llvm_unreachable("Unrecognised SrcOp::SrcType enum");
  }

  /// Return the source register.
  /// \pre The source is a register or MachineInstrBuilder.
  /// \return The source register.
  Register getReg() const {
    switch (Ty) {
    case SrcType::Ty_Predicate:
    case SrcType::Ty_Imm:
      llvm_unreachable("Not a register operand");
    case SrcType::Ty_Reg:
      return Reg;
    case SrcType::Ty_MIB:
      return SrcMIB->getOperand(0).getReg();
    }
    llvm_unreachable("Unrecognised SrcOp::SrcType enum");
  }

  /// Return the packed compare predicate.
  /// \pre The source kind is Ty_Predicate.
  /// \return The packed compare predicate.
  CmpInst::Predicate getPredicate() const {
    switch (Ty) {
    case SrcType::Ty_Predicate:
      return Pred;
    default:
      llvm_unreachable("Not a register operand");
    }
  }

  /// Return the packed immediate value.
  /// \pre The source kind is Ty_Imm.
  /// \return The packed immediate value.
  int64_t getImm() const {
    switch (Ty) {
    case SrcType::Ty_Imm:
      return Imm;
    default:
      llvm_unreachable("Not an immediate");
    }
  }

  /// Return the discriminator for the active source representation.
  /// \return The discriminator for the active source representation.
  SrcType getSrcOpKind() const { return Ty; }

private:
  SrcType Ty;
};

/// Helper for constructing MachineInstrs with a shared insertion point.
///
/// It keeps internally the insertion point and debug location for all the
/// new instructions we want to create. This information can be modified via
/// the related setters.
/// \param SrcOps Source operands used by the instruction.
class LLVM_ABI MachineIRBuilder {

  MachineIRBuilderState State;

  unsigned getOpcodeForMerge(const DstOp &DstOp, ArrayRef<SrcOp> SrcOps) const;

protected:
  /// Validate types for a truncate or extend between \p Dst and \p Src.
  /// \param Dst Destination type.
  /// \param Src Source type.
  /// \param IsExtend True when validating an extend rather than a trunc.
  void validateTruncExt(const LLT Dst, const LLT Src, bool IsExtend);

  /// Validate types for a unary operation.
  /// \param Res Result type.
  /// \param Op0 Operand type.
  void validateUnaryOp(const LLT Res, const LLT Op0);
  /// Validate types for a binary operation.
  /// \param Res Result type.
  /// \param Op0 First operand type.
  /// \param Op1 Second operand type.
  void validateBinaryOp(const LLT Res, const LLT Op0, const LLT Op1);
  /// Validate types for a shift operation.
  /// \param Res Result type.
  /// \param Op0 Value being shifted.
  /// \param Op1 Shift amount type.
  void validateShiftOp(const LLT Res, const LLT Op0, const LLT Op1);

  /// Validate types for a select operation.
  /// \param ResTy Result type.
  /// \param TstTy Condition type.
  /// \param Op0Ty True-value type.
  /// \param Op1Ty False-value type.
  void validateSelectOp(const LLT ResTy, const LLT TstTy, const LLT Op0Ty,
                        const LLT Op1Ty);

  /// Notify the change observer that \p InsertedInstr was created.
  /// \param InsertedInstr Newly inserted instruction, if observation is enabled.
  void recordInsertion(MachineInstr *InsertedInstr) const {
    if (State.Observer)
      State.Observer->createdInstr(*InsertedInstr);
  }

public:
  /// Construct an empty builder with no insertion state.
  MachineIRBuilder() = default;
  /// Construct a builder for \p MF with no insertion point yet.
  /// \param MF Machine function under construction.
  MachineIRBuilder(MachineFunction &MF) { setMF(MF); }

  /// Construct a builder inserting into \p MBB at \p InsPt.
  /// \param MBB Basic block receiving new instructions.
  /// \param InsPt Insertion point inside \p MBB.
  MachineIRBuilder(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsPt) {
    setMF(*MBB.getParent());
    setInsertPt(MBB, InsPt);
  }

  /// Construct a builder inserting before \p MI using its debug location.
  /// \param MI Instruction providing the insertion point and debug loc.
  MachineIRBuilder(MachineInstr &MI) :
    MachineIRBuilder(*MI.getParent(), MI.getIterator()) {
    setInstr(MI);
    setDebugLoc(MI.getDebugLoc());
  }

  /// Construct a builder inserting before \p MI with a change observer.
  /// \param MI Instruction providing the insertion point and debug loc.
  /// \param Observer Change observer notified about insertions.
  MachineIRBuilder(MachineInstr &MI, GISelChangeObserver &Observer) :
    MachineIRBuilder(MI) {
    setChangeObserver(Observer);
  }

  /// Destroy the builder.
  virtual ~MachineIRBuilder() = default;

  /// Construct a builder from an existing builder state.
  /// \param BState State copied into this builder.
  MachineIRBuilder(const MachineIRBuilderState &BState) : State(BState) {}

  /// Return the target instruction info for the current function.
  /// \return The target instruction info for the current function.
  const TargetInstrInfo &getTII() {
    assert(State.TII && "TargetInstrInfo is not set");
    return *State.TII;
  }

  /// Getter for the function we currently build.
  /// \return The machine function currently under construction.
  MachineFunction &getMF() {
    assert(State.MF && "MachineFunction is not set");
    return *State.MF;
  }

  /// Return the machine function currently under construction.
  /// \return The machine function currently under construction.
  const MachineFunction &getMF() const {
    assert(State.MF && "MachineFunction is not set");
    return *State.MF;
  }

  /// Return the data layout of the function being built.
  /// \return The data layout of the function being built.
  const DataLayout &getDataLayout() const {
    return getMF().getFunction().getDataLayout();
  }

  /// Return the LLVM context of the function being built.
  /// \return The LLVM context of the function being built.
  LLVMContext &getContext() const {
    return getMF().getFunction().getContext();
  }

  /// Getter for DebugLoc
  /// \return The current debug location.
  const DebugLoc &getDL() { return State.DL; }

  /// Return the machine register info for the current function.
  /// \return The machine register info for the current function.
  MachineRegisterInfo *getMRI() { return State.MRI; }
  /// Return the machine register info for the current function.
  /// \return The machine register info for the current function.
  const MachineRegisterInfo *getMRI() const { return State.MRI; }

  /// Getter for the State
  /// \return The builder state.
  MachineIRBuilderState &getState() { return State; }

  /// Replace the builder state with \p NewState.
  /// \param NewState Builder state to install.
  void setState(const MachineIRBuilderState &NewState) { State = NewState; }

  /// Getter for the basic block we currently build.
  /// \return The basic block currently receiving new instructions.
  const MachineBasicBlock &getMBB() const {
    assert(State.MBB && "MachineBasicBlock is not set");
    return *State.MBB;
  }

  /// Return the basic block currently receiving new instructions.
  /// \return The basic block currently receiving new instructions.
  MachineBasicBlock &getMBB() {
    return const_cast<MachineBasicBlock &>(
        const_cast<const MachineIRBuilder *>(this)->getMBB());
  }

  /// Return the CSE info associated with this builder, if any.
  /// \return The CSE info associated with this builder, or null if none.
  GISelCSEInfo *getCSEInfo() { return State.CSEInfo; }
  /// Return the CSE info associated with this builder, if any.
  /// \return The CSE info associated with this builder, or null if none.
  const GISelCSEInfo *getCSEInfo() const { return State.CSEInfo; }

  /// Current insertion point for new instructions.
  /// \return The current insertion point for new instructions.
  MachineBasicBlock::iterator getInsertPt() { return State.II; }

  /// Set the insertion point before the specified position.
  /// \pre MBB must be in getMF().
  /// \pre II must be a valid iterator in MBB.
  /// \param MBB Basic block that must belong to getMF().
  /// \param II Valid iterator inside \p MBB naming the insertion point.
  void setInsertPt(MachineBasicBlock &MBB, MachineBasicBlock::iterator II) {
    assert(MBB.getParent() == &getMF() &&
           "Basic block is in a different function");
    State.MBB = &MBB;
    State.II = II;
  }

  /// @}

  /// Set the CSE info consulted while building instructions.
  /// \param Info CSE info to use, or null to clear it.
  void setCSEInfo(GISelCSEInfo *Info) { State.CSEInfo = Info; }

  /// \name Setters for the insertion point.
  /// @{
  /// Set the MachineFunction where to build instructions.
  /// \param MF Machine function under construction.
  void setMF(MachineFunction &MF);

  /// Set the insertion point to the end of \p MBB.
  /// \pre \p MBB must be contained by getMF().
  /// \param MBB Basic block that receives new instructions.
  void setMBB(MachineBasicBlock &MBB) {
    State.MBB = &MBB;
    State.II = MBB.end();
    assert(&getMF() == MBB.getParent() &&
           "Basic block is in a different function");
  }

  /// Set the insertion point to before MI.
  /// \pre MI must be in getMF().
  /// \param MI Instruction before which new instructions are inserted.
  void setInstr(MachineInstr &MI) {
    assert(MI.getParent() && "Instruction is not part of a basic block");
    setMBB(*MI.getParent());
    State.II = MI.getIterator();
    setPCSections(MI.getPCSections());
    setMMRAMetadata(MI.getMMRAMetadata());
    setDeactivationSymbol(MI.getDeactivationSymbol());
  }
  /// @}

  /// Set the insertion point to before MI, and set the debug loc to MI's loc.
  /// \pre MI must be in getMF().
  /// \param MI Instruction providing the insertion point and debug location.
  void setInstrAndDebugLoc(MachineInstr &MI) {
    setInstr(MI);
    setDebugLoc(MI.getDebugLoc());
  }

  /// Install a change observer for newly created instructions.
  /// \param Observer Observer notified about insertions.
  void setChangeObserver(GISelChangeObserver &Observer) {
    State.Observer = &Observer;
  }

  /// Return the active change observer, or null if none is set.
  /// \return The active change observer, or null if none is set.
  GISelChangeObserver *getObserver() { return State.Observer; }

  /// Clear the active change observer.
  void stopObservingChanges() { State.Observer = nullptr; }

  /// Return true if a change observer is currently installed.
  /// \return True if a change observer is currently installed.
  bool isObservingChanges() const { return State.Observer != nullptr; }
  /// @}

  /// Set the debug location to \p DL for all the next build instructions.
  /// \param DL Debug location applied to subsequently built instructions.
  void setDebugLoc(const DebugLoc &DL) { this->State.DL = DL; }

  /// Get the current instruction's debug location.
  /// \return The current instruction's debug location.
  const DebugLoc &getDebugLoc() { return State.DL; }

  /// Set the PC sections metadata to \p MD for all the next build instructions.
  /// \param MD PC sections metadata applied to subsequently built instructions.
  void setPCSections(MDNode *MD) { State.PCSections = MD; }

  /// Get the current instruction's PC sections metadata.
  /// \return The current instruction's PC sections metadata.
  MDNode *getPCSections() { return State.PCSections; }

  /// Set the MMRA metadata for all subsequently built instructions.
  /// \param MMRA MMRA metadata applied to subsequently built instructions.
  void setMMRAMetadata(MDNode *MMRA) { State.MMRA = MMRA; }

  /// Return the current deactivation symbol, if any.
  /// \return The current deactivation symbol, or null if none.
  Value *getDeactivationSymbol() { return State.DS; }
  /// Set the deactivation symbol for subsequently built instructions.
  /// \param DS Deactivation symbol to attach, or null to clear it.
  void setDeactivationSymbol(Value *DS) { State.DS = DS; }

  /// Get the current instruction's MMRA metadata.
  /// \return The current instruction's MMRA metadata.
  MDNode *getMMRAMetadata() { return State.MMRA; }

  /// Build and insert <empty> = \p Opcode <empty>.
  /// The insertion point is the one set by the last call of either
  /// setBasicBlock or setMI.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  ///
  /// \param Opcode Target opcode for the instruction.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildInstr(unsigned Opcode) {
    return insertInstr(buildInstrNoInsert(Opcode));
  }

  /// Build but don't insert <empty> = \p Opcode <empty>.
  ///
  /// \pre setMF, setBasicBlock or setMI  must have been called.
  ///
  /// \param Opcode Target opcode for the instruction.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildInstrNoInsert(unsigned Opcode);

  /// Insert an existing instruction at the insertion point.
  /// \param MIB MachineInstrBuilder of the instruction to insert.
  /// \return A MachineInstrBuilder for the inserted instruction.
  MachineInstrBuilder insertInstr(MachineInstrBuilder MIB);

  /// Build and insert a DBG_VALUE instruction expressing the fact that the
  /// associated \p Variable lives in \p Reg (suitably modified by \p Expr).
  /// \param Reg Register holding the debug value or address.
  /// \param Variable DILocalVariable metadata describing the debug variable.
  /// \param Expr DIExpression applied to the debug value.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildDirectDbgValue(Register Reg, const MDNode *Variable,
                                          const MDNode *Expr);

  /// Build and insert a DBG_VALUE instruction expressing the fact that the
  /// associated \p Variable lives in memory at \p Reg (suitably modified by \p
  /// Expr).
  /// \param Reg Register holding the debug value or address.
  /// \param Variable DILocalVariable metadata describing the debug variable.
  /// \param Expr DIExpression applied to the debug value.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildIndirectDbgValue(Register Reg,
                                            const MDNode *Variable,
                                            const MDNode *Expr);

  /// Build and insert a DBG_VALUE instruction expressing the fact that the
  /// associated \p Variable lives in the stack slot specified by \p FI
  /// (suitably modified by \p Expr).
  /// \param FI Frame index of the stack slot.
  /// \param Variable DILocalVariable metadata describing the debug variable.
  /// \param Expr DIExpression applied to the debug value.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFIDbgValue(int FI, const MDNode *Variable,
                                      const MDNode *Expr);

  /// Build and insert a DBG_VALUE instructions specifying that \p Variable is
  /// given by \p C (suitably modified by \p Expr).
  /// \param C Constant providing the debug value.
  /// \param Variable DILocalVariable metadata describing the debug variable.
  /// \param Expr DIExpression applied to the debug value.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildConstDbgValue(const Constant &C,
                                         const MDNode *Variable,
                                         const MDNode *Expr);

  /// Build and insert a DBG_LABEL instructions specifying that \p Label is
  /// given. Convert "llvm.dbg.label Label" to "DBG_LABEL Label".
  /// \param Label DILabel metadata for the debug label.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildDbgLabel(const MDNode *Label);

  /// Build and insert \p Res = G_DYN_STACKALLOC \p Size, \p Align
  ///
  /// G_DYN_STACKALLOC does a dynamic stack allocation and writes the address of
  /// the allocated memory into \p Res.
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with pointer type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Size Size operand or allocation size.
  /// \param Alignment Required alignment.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildDynStackAlloc(const DstOp &Res, const SrcOp &Size,
                                         Align Alignment);

  /// Build and insert \p Res = G_FRAME_INDEX \p Idx
  ///
  /// G_FRAME_INDEX materializes the address of an alloca value or other
  /// stack-based object.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with pointer type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Idx Index operand or constant index.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFrameIndex(const DstOp &Res, int Idx);

  /// Build and insert \p Res = G_GLOBAL_VALUE \p GV
  ///
  /// G_GLOBAL_VALUE materializes the address of the specified global
  /// into \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with pointer type
  ///      in the same address space as \p GV.
  ///
  /// \param Res Destination operand for the result.
  /// \param GV Global value whose address is materialized.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildGlobalValue(const DstOp &Res, const GlobalValue *GV);

  /// Build and insert \p Res = G_CONSTANT_POOL \p Idx
  ///
  /// G_CONSTANT_POOL materializes the address of an object in the constant
  /// pool.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with pointer type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Idx Index operand or constant index.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildConstantPool(const DstOp &Res, unsigned Idx);

  /// Build and insert \p Res = G_PTR_ADD \p Op0, \p Op1
  ///
  /// G_PTR_ADD adds \p Op1 addressible units to the pointer specified by \p Op0,
  /// storing the resulting pointer in \p Res. Addressible units are typically
  /// bytes but this can vary between targets.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res and \p Op0 must be generic virtual registers with pointer
  ///      type.
  /// \pre \p Op1 must be a generic virtual register with scalar type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op0 First source operand.
  /// \param Op1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildPtrAdd(const DstOp &Res, const SrcOp &Op0,
                                  const SrcOp &Op1,
                                  std::optional<unsigned> Flags = std::nullopt);

  /// Build a nuw inbounds G_PTR_ADD for an in-object pointer offset.
  ///
  /// Builds \p Res = nuw inbounds G_PTR_ADD \p Op0, \p Op1. The value of
  /// \p Op0 must be a pointer into or just after an object; adding \p Op1 must
  /// yield a pointer into or just after the same object.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res and \p Op0 must be generic virtual registers with pointer
  ///      type.
  /// \pre \p Op1 must be a generic virtual register with scalar type.
  /// \param Res Destination pointer.
  /// \param Op0 Base object pointer.
  /// \param Op1 Offset in addressable units.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildObjectPtrOffset(const DstOp &Res, const SrcOp &Op0,
                                           const SrcOp &Op1);

  /// Materialize and insert \p Res = G_PTR_ADD \p Op0, (G_CONSTANT \p Value)
  ///
  /// G_PTR_ADD adds \p Value bytes to the pointer specified by \p Op0,
  /// storing the resulting pointer in \p Res. If \p Value is zero then no
  /// G_PTR_ADD or G_CONSTANT will be created and \pre Op0 will be assigned to
  /// \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Op0 must be a generic virtual register with pointer type.
  /// \pre \p ValueTy must be a scalar type.
  /// \pre \p Res must be 0. This is to detect confusion between
  ///      materializePtrAdd() and buildPtrAdd().
  /// \post \p Res will either be a new generic virtual register of the same
  ///       type as \p Op0 or \p Op0 itself.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op0 First source operand.
  /// \param ValueTy Scalar type of the materialized offset constant.
  /// \param Value Immediate offset value in addressable units.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  std::optional<MachineInstrBuilder>
  materializePtrAdd(Register &Res, Register Op0, const LLT ValueTy,
                    uint64_t Value,
                    std::optional<unsigned> Flags = std::nullopt);

  /// Materialize a nuw inbounds G_PTR_ADD for an in-object constant offset.
  ///
  /// Builds \p Res = nuw inbounds G_PTR_ADD \p Op0, (G_CONSTANT \p Value).
  /// The value of \p Op0 must be a pointer into or just after an object; adding
  /// \p Value must yield a pointer into or just after the same object.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Op0 must be a generic virtual register with pointer type.
  /// \pre \p ValueTy must be a scalar type.
  /// \pre \p Res must be 0. This is to detect confusion between
  ///      materializeObjectPtrOffset() and buildObjectPtrOffset().
  /// \post \p Res will either be a new generic virtual register of the same
  ///       type as \p Op0 or \p Op0 itself.
  /// \param Res Set to the resulting pointer register.
  /// \param Op0 Base object pointer.
  /// \param ValueTy Scalar type of the materialized offset constant.
  /// \param Value Immediate offset in addressable units.
  /// \return a MachineInstrBuilder for the newly created instruction.
  std::optional<MachineInstrBuilder>
  materializeObjectPtrOffset(Register &Res, Register Op0, const LLT ValueTy,
                             uint64_t Value);

  /// Build and insert \p Res = G_PTRMASK \p Op0, \p Op1
  /// \param Res Destination operand for the result.
  /// \param Op0 First source operand.
  /// \param Op1 Second source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildPtrMask(const DstOp &Res, const SrcOp &Op0,
                                   const SrcOp &Op1) {
    return buildInstr(TargetOpcode::G_PTRMASK, {Res}, {Op0, Op1});
  }

  /// Build and insert \p Res = G_PTRMASK \p Op0, \p G_CONSTANT (1 << NumBits) - 1
  ///
  /// This clears the low bits of a pointer operand without destroying its
  /// pointer properties. This has the effect of rounding the address *down* to
  /// a specified alignment in bits.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res and \p Op0 must be generic virtual registers with pointer
  ///      type.
  /// \pre \p NumBits must be an integer representing the number of low bits to
  ///      be cleared in \p Op0.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op0 First source operand.
  /// \param NumBits Number of low pointer bits to clear.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildMaskLowPtrBits(const DstOp &Res, const SrcOp &Op0,
                                          uint32_t NumBits);

  /// Build and insert
  /// a, b, ..., x = G_UNMERGE_VALUES \p Op0
  /// \p Res = G_BUILD_VECTOR a, b, ..., x, undef, ..., undef
  ///
  /// Pad \p Op0 with undef elements to match number of elements in \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res and \p Op0 must be generic virtual registers with vector type,
  ///      same vector element type and Op0 must have fewer elements then Res.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op0 First source operand.
  /// \return a MachineInstrBuilder for the newly created build vector instr.
  MachineInstrBuilder buildPadVectorWithUndefElements(const DstOp &Res,
                                                      const SrcOp &Op0);

  /// Build and insert
  /// a, b, ..., x, y, z = G_UNMERGE_VALUES \p Op0
  /// \p Res = G_BUILD_VECTOR a, b, ..., x
  ///
  /// Delete trailing elements in \p Op0 to match number of elements in \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res and \p Op0 must be generic virtual registers with vector type,
  ///      same vector element type and Op0 must have more elements then Res.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op0 First source operand.
  /// \return a MachineInstrBuilder for the newly created build vector instr.
  MachineInstrBuilder buildDeleteTrailingVectorElements(const DstOp &Res,
                                                        const SrcOp &Op0);

  /// Build and insert \p Res, \p CarryOut = G_UADDO \p Op0, \p Op1
  ///
  /// G_UADDO sets \p Res to \p Op0 + \p Op1 (truncated to the bit width) and
  /// sets \p CarryOut to 1 if the result overflowed in unsigned arithmetic.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers with the
  /// same scalar type.
  ////\pre \p CarryOut must be generic virtual register with scalar type
  ///(typically s1)
  ///
  /// \param Res Destination operand for the result.
  /// \param CarryOut Destination for the carry or overflow flag.
  /// \param Op0 First source operand.
  /// \param Op1 Second source operand.
  /// \return The newly created instruction.
  MachineInstrBuilder buildUAddo(const DstOp &Res, const DstOp &CarryOut,
                                 const SrcOp &Op0, const SrcOp &Op1) {
    return buildInstr(TargetOpcode::G_UADDO, {Res, CarryOut}, {Op0, Op1});
  }

  /// Build and insert \p Res, \p CarryOut = G_USUBO \p Op0, \p Op1
  /// \param Res Destination operand for the result.
  /// \param CarryOut Destination for the carry or overflow flag.
  /// \param Op0 First source operand.
  /// \param Op1 Second source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUSubo(const DstOp &Res, const DstOp &CarryOut,
                                 const SrcOp &Op0, const SrcOp &Op1) {
    return buildInstr(TargetOpcode::G_USUBO, {Res, CarryOut}, {Op0, Op1});
  }

  /// Build and insert \p Res, \p CarryOut = G_SADDO \p Op0, \p Op1
  /// \param Res Destination operand for the result.
  /// \param CarryOut Destination for the carry or overflow flag.
  /// \param Op0 First source operand.
  /// \param Op1 Second source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSAddo(const DstOp &Res, const DstOp &CarryOut,
                                 const SrcOp &Op0, const SrcOp &Op1) {
    return buildInstr(TargetOpcode::G_SADDO, {Res, CarryOut}, {Op0, Op1});
  }

  /// Build and insert \p Res, \p CarryOut = G_SUBO \p Op0, \p Op1
  /// \param Res Destination operand for the result.
  /// \param CarryOut Destination for the carry or overflow flag.
  /// \param Op0 First source operand.
  /// \param Op1 Second source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSSubo(const DstOp &Res, const DstOp &CarryOut,
                                 const SrcOp &Op0, const SrcOp &Op1) {
    return buildInstr(TargetOpcode::G_SSUBO, {Res, CarryOut}, {Op0, Op1});
  }

  /// Build and insert \p Res, \p CarryOut = G_UADDE \p Op0,
  /// \p Op1, \p CarryIn
  ///
  /// G_UADDE sets \p Res to \p Op0 + \p Op1 + \p CarryIn (truncated to the bit
  /// width) and sets \p CarryOut to 1 if the result overflowed in unsigned
  /// arithmetic.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same scalar type.
  /// \pre \p CarryOut and \p CarryIn must be generic virtual
  ///      registers with the same scalar type (typically s1)
  ///
  /// \param Res Destination operand for the result.
  /// \param CarryOut Destination for the carry or overflow flag.
  /// \param Op0 First source operand.
  /// \param Op1 Second source operand.
  /// \param CarryIn Incoming carry flag.
  /// \return The newly created instruction.
  MachineInstrBuilder buildUAdde(const DstOp &Res, const DstOp &CarryOut,
                                 const SrcOp &Op0, const SrcOp &Op1,
                                 const SrcOp &CarryIn) {
    return buildInstr(TargetOpcode::G_UADDE, {Res, CarryOut},
                                             {Op0, Op1, CarryIn});
  }

  /// Build and insert \p Res, \p CarryOut = G_USUBE \p Op0, \p Op1, \p CarryInp
  /// \param Res Destination operand for the result.
  /// \param CarryOut Destination for the carry or overflow flag.
  /// \param Op0 First source operand.
  /// \param Op1 Second source operand.
  /// \param CarryIn Incoming carry flag.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUSube(const DstOp &Res, const DstOp &CarryOut,
                                 const SrcOp &Op0, const SrcOp &Op1,
                                 const SrcOp &CarryIn) {
    return buildInstr(TargetOpcode::G_USUBE, {Res, CarryOut},
                                             {Op0, Op1, CarryIn});
  }

  /// Build and insert \p Res, \p CarryOut = G_SADDE \p Op0, \p Op1, \p CarryInp
  /// \param Res Destination operand for the result.
  /// \param CarryOut Destination for the carry or overflow flag.
  /// \param Op0 First source operand.
  /// \param Op1 Second source operand.
  /// \param CarryIn Incoming carry flag.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSAdde(const DstOp &Res, const DstOp &CarryOut,
                                 const SrcOp &Op0, const SrcOp &Op1,
                                 const SrcOp &CarryIn) {
    return buildInstr(TargetOpcode::G_SADDE, {Res, CarryOut},
                                             {Op0, Op1, CarryIn});
  }

  /// Build and insert \p Res, \p CarryOut = G_SSUBE \p Op0, \p Op1, \p CarryInp
  /// \param Res Destination operand for the result.
  /// \param CarryOut Destination for the carry or overflow flag.
  /// \param Op0 First source operand.
  /// \param Op1 Second source operand.
  /// \param CarryIn Incoming carry flag.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSSube(const DstOp &Res, const DstOp &CarryOut,
                                 const SrcOp &Op0, const SrcOp &Op1,
                                 const SrcOp &CarryIn) {
    return buildInstr(TargetOpcode::G_SSUBE, {Res, CarryOut},
                                             {Op0, Op1, CarryIn});
  }

  /// Build and insert \p Res = G_ANYEXT \p Op0
  ///
  /// G_ANYEXT produces a register of the specified width, with bits 0 to
  /// sizeof(\p Ty) * 8 set to \p Op. The remaining bits are unspecified
  /// (i.e. this is neither zero nor sign-extension). For a vector register,
  /// each element is extended individually.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be smaller than \p Res
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return The newly created instruction.

  MachineInstrBuilder buildAnyExt(const DstOp &Res, const SrcOp &Op);

  /// Build and insert \p Res = G_SEXT \p Op
  ///
  /// G_SEXT produces a register of the specified width, with bits 0 to
  /// sizeof(\p Ty) * 8 set to \p Op. The remaining bits are duplicated from the
  /// high bit of \p Op (i.e. 2s-complement sign extended).
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be smaller than \p Res
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return The newly created instruction.
  MachineInstrBuilder buildSExt(const DstOp &Res, const SrcOp &Op);

  /// Build and insert \p Res = G_SEXT_INREG \p Op, ImmOp
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \param ImmOp Immediate describing the in-register width.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSExtInReg(const DstOp &Res, const SrcOp &Op, int64_t ImmOp) {
    return buildInstr(TargetOpcode::G_SEXT_INREG, {Res}, {Op, SrcOp(ImmOp)});
  }

  /// Build and insert \p Res = G_FPEXT \p Op
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFPExt(const DstOp &Res, const SrcOp &Op,
                                 std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FPEXT, {Res}, {Op}, Flags);
  }

  /// Build and insert a G_PTRTOINT instruction.
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildPtrToInt(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_PTRTOINT, {Dst}, {Src});
  }

  /// Build and insert a G_INTTOPTR instruction.
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildIntToPtr(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_INTTOPTR, {Dst}, {Src});
  }

  /// Build and insert \p Dst = G_BITCAST \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBitcast(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_BITCAST, {Dst}, {Src});
  }

  /// Build and insert \p Dst = G_ADDRSPACE_CAST \p Src.
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAddrSpaceCast(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_ADDRSPACE_CAST, {Dst}, {Src});
  }

  /// Return the opcode used to extend boolean values for this target.
  /// \param IsVec True when extending a vector boolean.
  /// \param IsFP True when extending a floating-point compare result.
  /// \return The opcode of the extension the target wants to use for boolean
  /// values.
  unsigned getBoolExtOp(bool IsVec, bool IsFP) const;

  /// Build and insert a boolean extend of \p Op into \p Res.
  ///
  /// Builds G_ANYEXT, G_SEXT, or G_ZEXT depending on how the target wants to
  /// extend boolean values.
  /// \param Res Destination of the extended value.
  /// \param Op Boolean source value.
  /// \param IsFP True when extending a floating-point compare result.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBoolExt(const DstOp &Res, const SrcOp &Op,
                                   bool IsFP);

  /// Build and insert an in-register boolean extend of \p Op into \p Res.
  ///
  /// Builds G_SEXT_INREG, G_AND, or COPY depending on how the target wants to
  /// extend boolean values, using the original register size.
  /// \param Res Destination of the extended value.
  /// \param Op Boolean source value.
  /// \param IsVector True when the value is a vector.
  /// \param IsFP True when extending a floating-point compare result.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBoolExtInReg(const DstOp &Res, const SrcOp &Op,
                                        bool IsVector,
                                        bool IsFP);

  /// Build and insert \p Res = G_ZEXT \p Op
  ///
  /// G_ZEXT produces a register of the specified width, with bits 0 to
  /// sizeof(\p Ty) * 8 set to \p Op. The remaining bits are 0. For a vector
  /// register, each element is extended individually.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be smaller than \p Res
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return The newly created instruction.
  MachineInstrBuilder buildZExt(const DstOp &Res, const SrcOp &Op,
                                std::optional<unsigned> Flags = std::nullopt);

  /// Build and insert \p Res = G_SEXT \p Op, \p Res = G_TRUNC \p Op, or
  /// \p Res = COPY \p Op depending on the differing sizes of \p Res and \p Op.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return The newly created instruction.
  MachineInstrBuilder buildSExtOrTrunc(const DstOp &Res, const SrcOp &Op);

  /// Build and insert \p Res = G_ZEXT \p Op, \p Res = G_TRUNC \p Op, or
  /// \p Res = COPY \p Op depending on the differing sizes of \p Res and \p Op.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return The newly created instruction.
  MachineInstrBuilder buildZExtOrTrunc(const DstOp &Res, const SrcOp &Op);

  // Build and insert \p Res = G_ANYEXT \p Op, \p Res = G_TRUNC \p Op, or
  /// \p Res = COPY \p Op depending on the differing sizes of \p Res and \p Op.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return The newly created instruction.
  MachineInstrBuilder buildAnyExtOrTrunc(const DstOp &Res, const SrcOp &Op);

  /// Build and insert \p Res = \p ExtOpc, \p Res = G_TRUNC \p
  /// Op, or \p Res = COPY \p Op depending on the differing sizes of \p Res and
  /// \p Op.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  ///
  /// \param ExtOpc Extension opcode to use when widening.
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return The newly created instruction.
  MachineInstrBuilder buildExtOrTrunc(unsigned ExtOpc, const DstOp &Res,
                                      const SrcOp &Op);

  /// Build and inserts \p Res = \p G_AND \p Op, \p LowBitsSet(ImmOp)
  /// Since there is no G_ZEXT_INREG like G_SEXT_INREG, the instruction is
  /// emulated using G_AND.
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \param ImmOp Immediate describing the in-register width.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildZExtInReg(const DstOp &Res, const SrcOp &Op,
                                     int64_t ImmOp);

  /// Build and insert \p Res = \p G_TRUNC_SSAT_S \p Op
  ///
  /// G_TRUNC_SSAT_S truncates the signed input, \p Op, to a signed result with
  /// saturation.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return The newly created instruction.
  MachineInstrBuilder buildTruncSSatS(const DstOp &Res, const SrcOp &Op) {
    return buildInstr(TargetOpcode::G_TRUNC_SSAT_S, {Res}, {Op});
  }

  /// Build and insert \p Res = \p G_TRUNC_SSAT_U \p Op
  ///
  /// G_TRUNC_SSAT_U truncates the signed input, \p Op, to an unsigned result
  /// with saturation.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return The newly created instruction.
  MachineInstrBuilder buildTruncSSatU(const DstOp &Res, const SrcOp &Op) {
    return buildInstr(TargetOpcode::G_TRUNC_SSAT_U, {Res}, {Op});
  }

  /// Build and insert \p Res = \p G_TRUNC_USAT_U \p Op
  ///
  /// G_TRUNC_USAT_U truncates the unsigned input, \p Op, to an unsigned result
  /// with saturation.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return The newly created instruction.
  MachineInstrBuilder buildTruncUSatU(const DstOp &Res, const SrcOp &Op) {
    return buildInstr(TargetOpcode::G_TRUNC_USAT_U, {Res}, {Op});
  }

  /// Build and insert an appropriate cast between two registers of equal size.
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildCast(const DstOp &Dst, const SrcOp &Src);

  /// Build and insert G_BR \p Dest
  ///
  /// G_BR is an unconditional branch to \p Dest.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  ///
  /// \param Dest Destination basic block.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBr(MachineBasicBlock &Dest);

  /// Build and insert G_BRCOND \p Tst, \p Dest
  ///
  /// G_BRCOND is a conditional branch to \p Dest.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Tst must be a generic virtual register with scalar
  ///      type. At the beginning of legalization, this will be a single
  ///      bit (s1). Targets with interesting flags registers may change
  ///      this. For a wider type, whether the branch is taken must only
  ///      depend on bit 0 (for now).
  ///
  /// \param Tst Condition or test operand.
  /// \param Dest Destination basic block.
  /// \return The newly created instruction.
  MachineInstrBuilder buildBrCond(const SrcOp &Tst, MachineBasicBlock &Dest);

  /// Build and insert G_BRINDIRECT \p Tgt
  ///
  /// G_BRINDIRECT is an indirect branch to \p Tgt.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Tgt must be a generic virtual register with pointer type.
  ///
  /// \param Tgt Indirect branch target.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBrIndirect(Register Tgt);

  /// Build and insert G_BRJT \p TablePtr, \p JTI, \p IndexReg
  ///
  /// G_BRJT is a jump table branch using a table base pointer \p TablePtr,
  /// jump table index \p JTI and index \p IndexReg
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p TablePtr must be a generic virtual register with pointer type.
  /// \pre \p JTI must be a jump table index.
  /// \pre \p IndexReg must be a generic virtual register with pointer type.
  ///
  /// \param TablePtr Base pointer of the jump table.
  /// \param JTI Jump table index.
  /// \param IndexReg Register holding the jump-table index.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBrJT(Register TablePtr, unsigned JTI,
                                Register IndexReg);

  /// Build and insert \p Res = G_CONSTANT \p Val
  ///
  /// G_CONSTANT is an integer constant with the specified size and value. \p
  /// Val will be extended or truncated to the size of \p Reg.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or pointer
  ///      type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Val Value operand.
  /// \return The newly created instruction.
  virtual MachineInstrBuilder buildConstant(const DstOp &Res,
                                            const ConstantInt &Val);

  /// Build and insert \p Res = G_CONSTANT \p Val
  ///
  /// G_CONSTANT is an integer constant with the specified size and value.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Val Value operand.
  /// \return The newly created instruction.
  MachineInstrBuilder buildConstant(const DstOp &Res, int64_t Val);
  /// Build and insert \p Res = G_CONSTANT \p Val.
  /// \param Res Destination operand for the result.
  /// \param Val Value operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildConstant(const DstOp &Res, const APInt &Val);

  /// Build and insert \p Res = G_FCONSTANT \p Val
  ///
  /// G_FCONSTANT is a floating-point constant with the specified size and
  /// value.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Val Value operand.
  /// \return The newly created instruction.
  virtual MachineInstrBuilder buildFConstant(const DstOp &Res,
                                             const ConstantFP &Val);

  /// Build and insert \p Res = G_FCONSTANT \p Val.
  /// \param Res Destination operand for the result.
  /// \param Val Value operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFConstant(const DstOp &Res, double Val);
  /// Build and insert \p Res = G_FCONSTANT \p Val.
  /// \param Res Destination operand for the result.
  /// \param Val Value operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFConstant(const DstOp &Res, const APFloat &Val);

  /// Build and insert G_PTRAUTH_GLOBAL_VALUE
  ///
  /// \param Res Destination operand for the result.
  /// \param CPA Constant pointer-authentication descriptor.
  /// \param Addr Address operand.
  /// \param AddrDisc Address discriminator register.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildConstantPtrAuth(const DstOp &Res,
                                           const ConstantPtrAuth *CPA,
                                           Register Addr, Register AddrDisc);

  /// Build and insert \p Res = COPY Op
  ///
  /// Register-to-register COPY sets \p Res to \p Op.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildCopy(const DstOp &Res, const SrcOp &Op);


  /// Build and insert G_ASSERT_SEXT, G_ASSERT_ZEXT, or G_ASSERT_ALIGN
  ///
  /// \param Opc Target opcode for the instruction.
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \param Val Value operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAssertInstr(unsigned Opc, const DstOp &Res,
                                       const SrcOp &Op, unsigned Val) {
    return buildInstr(Opc, Res, Op).addImm(Val);
  }

  /// Build and insert \p Res = G_ASSERT_ZEXT Op, Size
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \param Size Size operand or allocation size.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAssertZExt(const DstOp &Res, const SrcOp &Op,
                                      unsigned Size) {
    return buildAssertInstr(TargetOpcode::G_ASSERT_ZEXT, Res, Op, Size);
  }

  /// Build and insert \p Res = G_ASSERT_SEXT Op, Size
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \param Size Size operand or allocation size.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAssertSExt(const DstOp &Res, const SrcOp &Op,
                                      unsigned Size) {
    return buildAssertInstr(TargetOpcode::G_ASSERT_SEXT, Res, Op, Size);
  }

  /// Build and insert \p Res = G_ASSERT_ALIGN Op, AlignVal
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \param AlignVal Alignment asserted for the value.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAssertAlign(const DstOp &Res, const SrcOp &Op,
				       Align AlignVal) {
    return buildAssertInstr(TargetOpcode::G_ASSERT_ALIGN, Res, Op,
                            AlignVal.value());
  }

  /// Build and insert `Res = G_LOAD Addr, MMO`.
  ///
  /// Loads the value stored at \p Addr. Puts the result in \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Addr Address operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildLoad(const DstOp &Res, const SrcOp &Addr,
                                MachineMemOperand &MMO) {
    return buildLoadInstr(TargetOpcode::G_LOAD, Res, Addr, MMO);
  }

  /// Build and insert a G_LOAD instruction, while constructing the
  /// MachineMemOperand.
  /// \param Res Destination operand for the result.
  /// \param Addr Address operand.
  /// \param PtrInfo Machine pointer info for the memory access.
  /// \param Alignment Required alignment.
  /// \param MMOFlags MachineMemOperand flags for the access.
  /// \param AAInfo Optional AA metadata for the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildLoad(const DstOp &Res, const SrcOp &Addr, MachinePointerInfo PtrInfo,
            Align Alignment,
            MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
            const AAMDNodes &AAInfo = AAMDNodes());

  /// Build and insert `Res = <opcode> Addr, MMO`.
  ///
  /// Loads the value stored at \p Addr. Puts the result in \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  ///
  /// \param Opcode Target opcode for the instruction.
  /// \param Res Destination operand for the result.
  /// \param Addr Address operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildLoadInstr(unsigned Opcode, const DstOp &Res,
                                     const SrcOp &Addr, MachineMemOperand &MMO);

  /// Helper to create a load from a constant offset given a base address. Load
  /// the type of \p Dst from \p Offset from the given base address and memory
  /// operand.
  /// \param Dst Destination operand for the result.
  /// \param BasePtr Base pointer of the access.
  /// \param BaseMMO Memory operand for the base access.
  /// \param Offset Byte offset from the base pointer.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildLoadFromOffset(const DstOp &Dst,
                                          const SrcOp &BasePtr,
                                          MachineMemOperand &BaseMMO,
                                          int64_t Offset);

  /// Build and insert `G_STORE Val, Addr, MMO`.
  ///
  /// Stores the value \p Val to \p Addr.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Val must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  ///
  /// \param Val Value operand.
  /// \param Addr Address operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildStore(const SrcOp &Val, const SrcOp &Addr,
                                 MachineMemOperand &MMO);

  /// Build and insert `<opcode> Val, Addr, MMO`.
  ///
  /// Stores the value \p Val to \p Addr.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Val must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  ///
  /// \param Opcode Target opcode for the instruction.
  /// \param Val Value operand.
  /// \param Addr Address operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildStoreInstr(unsigned Opcode, const SrcOp &Val,
                                      const SrcOp &Addr,
                                      MachineMemOperand &MMO);

  /// Build and insert a G_STORE instruction, while constructing the
  /// MachineMemOperand.
  /// \param Val Value operand.
  /// \param Addr Address operand.
  /// \param PtrInfo Machine pointer info for the memory access.
  /// \param Alignment Required alignment.
  /// \param MMOFlags MachineMemOperand flags for the access.
  /// \param AAInfo Optional AA metadata for the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildStore(const SrcOp &Val, const SrcOp &Addr, MachinePointerInfo PtrInfo,
             Align Alignment,
             MachineMemOperand::Flags MMOFlags = MachineMemOperand::MONone,
             const AAMDNodes &AAInfo = AAMDNodes());

  /// Build and insert `Res0, ... = G_EXTRACT Src, Idx0`.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res and \p Src must be generic virtual registers.
  ///
  /// \param Res Destination operand for the result.
  /// \param Src Source operand.
  /// \param Index Index into the source value.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildExtract(const DstOp &Res, const SrcOp &Src, uint64_t Index);

  /// Build and insert \p Res = IMPLICIT_DEF.
  /// \param Res Destination operand for the result.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUndef(const DstOp &Res);

  /// Build and insert \p Res = G_MERGE_VALUES \p Op0, ...
  ///
  /// G_MERGE_VALUES combines the input elements contiguously into a larger
  /// register. It should only be used when the destination register is not a
  /// vector.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre The entire register \p Res (and no more) must be covered by the input
  ///      registers.
  /// \pre The type of all \p Ops registers must be identical.
  ///
  /// \param Res Destination operand for the result.
  /// \param Ops Source operands covering the destination.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildMergeValues(const DstOp &Res,
                                       ArrayRef<Register> Ops);

  /// Build and insert \p Res = G_MERGE_VALUES \p Op0, ...
  ///               or \p Res = G_BUILD_VECTOR \p Op0, ...
  ///               or \p Res = G_CONCAT_VECTORS \p Op0, ...
  ///
  /// G_MERGE_VALUES combines the input elements contiguously into a larger
  /// register. It is used when the destination register is not a vector.
  /// G_BUILD_VECTOR combines scalar inputs into a vector register.
  /// G_CONCAT_VECTORS combines vector inputs into a vector register.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre The entire register \p Res (and no more) must be covered by the input
  ///      registers.
  /// \pre The type of all \p Ops registers must be identical.
  ///
  /// \param Res Destination operand for the result.
  /// \param Ops Source operands covering the destination.
  /// \return a MachineInstrBuilder for the newly created instruction. The
  ///         opcode of the new instruction will depend on the types of both
  ///         the destination and the sources.
  MachineInstrBuilder buildMergeLikeInstr(const DstOp &Res,
                                          ArrayRef<Register> Ops);
  /// Build and insert a merge-like instruction from initializer-list sources.
  /// \param Res Destination operand for the result.
  /// \param Ops Source operands covering the destination.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildMergeLikeInstr(const DstOp &Res,
                                          std::initializer_list<SrcOp> Ops);

  /// Build and insert \p Res0, ... = G_UNMERGE_VALUES \p Op
  ///
  /// G_UNMERGE_VALUES splits contiguous bits of the input into multiple
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre The entire register \p Res (and no more) must be covered by the input
  ///      registers.
  /// \pre The type of all \p Res registers must be identical.
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUnmerge(ArrayRef<LLT> Res, const SrcOp &Op);
  /// Build and insert \p Res0, ... = G_UNMERGE_VALUES \p Op.
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUnmerge(ArrayRef<Register> Res, const SrcOp &Op);

  /// Build and insert an unmerge of \p Res sized pieces to cover \p Op
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUnmerge(LLT Res, const SrcOp &Op);

  /// Build and insert an unmerge of pieces with \p Attrs register attributes to
  /// cover \p Op
  /// \param Attrs Virtual-register attributes for each result piece.
  /// \param Op Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUnmerge(MachineRegisterInfo::VRegAttrs Attrs,
                                   const SrcOp &Op);

  /// Build and insert \p Res = G_BUILD_VECTOR \p Op0, ...
  ///
  /// G_BUILD_VECTOR creates a vector value from multiple scalar registers.
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre The entire register \p Res (and no more) must be covered by the
  ///      input scalar registers.
  /// \pre The type of all \p Ops registers must be identical.
  ///
  /// \param Res Destination operand for the result.
  /// \param Ops Source operands covering the destination.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBuildVector(const DstOp &Res,
                                       ArrayRef<Register> Ops);

  /// Build and insert \p Res = G_BUILD_VECTOR \p Op0, ... where each OpN is
  /// built with G_CONSTANT.
  /// \param Res Destination operand for the result.
  /// \param Ops Source operands covering the destination.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBuildVectorConstant(const DstOp &Res,
                                               ArrayRef<APInt> Ops);

  /// Build and insert \p Res = G_BUILD_VECTOR with \p Src replicated to fill
  /// the number of elements
  /// \param Res Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSplatBuildVector(const DstOp &Res, const SrcOp &Src);

  /// Build and insert \p Res = G_BUILD_VECTOR_TRUNC \p Op0, ...
  ///
  /// G_BUILD_VECTOR_TRUNC creates a vector value from multiple scalar registers
  /// which have types larger than the destination vector element type, and
  /// truncates the values to fit.
  ///
  /// If the operands given are already the same size as the vector elt type,
  /// then this method will instead create a G_BUILD_VECTOR instruction.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre The type of all \p Ops registers must be identical.
  ///
  /// \param Res Destination operand for the result.
  /// \param Ops Source operands covering the destination.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBuildVectorTrunc(const DstOp &Res,
                                            ArrayRef<Register> Ops);

  /// Build and insert a vector splat of a scalar \p Src using a
  /// G_INSERT_VECTOR_ELT and G_SHUFFLE_VECTOR idiom.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Src must have the same type as the element type of \p Dst
  ///
  /// \param Res Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildShuffleSplat(const DstOp &Res, const SrcOp &Src);

  /// Build and insert \p Res = G_SHUFFLE_VECTOR \p Src1, \p Src2, \p Mask
  ///
  /// \pre setBasicBlock or setMI must have been called.
  ///
  /// \param Res Destination operand for the result.
  /// \param Src1 Second source operand.
  /// \param Src2 Third source operand.
  /// \param Mask Shuffle mask or FP-class bit mask.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildShuffleVector(const DstOp &Res, const SrcOp &Src1,
                                         const SrcOp &Src2, ArrayRef<int> Mask);

  /// Build and insert \p Res = G_SPLAT_VECTOR \p Val
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with vector type.
  /// \pre \p Val must be a generic virtual register with scalar type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Val Value operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSplatVector(const DstOp &Res, const SrcOp &Val);

  /// Build and insert \p Res = G_CONCAT_VECTORS \p Op0, ...
  ///
  /// G_CONCAT_VECTORS creates a vector from the concatenation of 2 or more
  /// vectors.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre The entire register \p Res (and no more) must be covered by the input
  ///      registers.
  /// \pre The type of all source operands must be identical.
  ///
  /// \param Res Destination operand for the result.
  /// \param Ops Source operands covering the destination.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildConcatVectors(const DstOp &Res,
                                         ArrayRef<Register> Ops);

  /// Build and insert `Res = G_INSERT_SUBVECTOR Src0, Src1, Idx`.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Src0, and \p Src1 must be generic virtual registers with
  /// vector type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Index Index into the source value.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildInsertSubvector(const DstOp &Res, const SrcOp &Src0,
                                           const SrcOp &Src1, unsigned Index);

  /// Build and insert `Res = G_EXTRACT_SUBVECTOR Src, Idx0`.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res and \p Src must be generic virtual registers with vector type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Src Source operand.
  /// \param Index Index into the source value.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildExtractSubvector(const DstOp &Res, const SrcOp &Src,
                                            unsigned Index);

  /// Build and insert \p Res = G_INSERT \p Src, \p Op, \p Index.
  /// \param Res Destination operand for the result.
  /// \param Src Source operand.
  /// \param Op Source operand.
  /// \param Index Index into the source value.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildInsert(const DstOp &Res, const SrcOp &Src,
                                  const SrcOp &Op, unsigned Index);

  /// Build and insert \p Res = G_STEP_VECTOR \p Step
  ///
  /// G_STEP_VECTOR returns a scalable vector of linear sequence of step \p Step
  /// into \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalable vector type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Step Step between consecutive vector elements.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildStepVector(const DstOp &Res, unsigned Step);

  /// Build and insert \p Res = G_VSCALE \p MinElts
  ///
  /// G_VSCALE puts the value of the runtime vscale multiplied by \p MinElts
  /// into \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar type.
  ///
  /// \param Res Destination operand for the result.
  /// \param MinElts Minimum element count multiplier for vscale.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVScale(const DstOp &Res, unsigned MinElts);

  /// Build and insert \p Res = G_VSCALE \p MinElts
  ///
  /// G_VSCALE puts the value of the runtime vscale multiplied by \p MinElts
  /// into \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar type.
  ///
  /// \param Res Destination operand for the result.
  /// \param MinElts Minimum element count multiplier for vscale.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVScale(const DstOp &Res, const ConstantInt &MinElts);

  /// Build and insert \p Res = G_VSCALE \p MinElts
  ///
  /// G_VSCALE puts the value of the runtime vscale multiplied by \p MinElts
  /// into \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar type.
  ///
  /// \param Res Destination operand for the result.
  /// \param MinElts Minimum element count multiplier for vscale.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVScale(const DstOp &Res, const APInt &MinElts);

  /// Build and insert a G_INTRINSIC instruction.
  ///
  /// There are four different opcodes based on combinations of whether the
  /// intrinsic has side effects and whether it is convergent. These properties
  /// can be specified as explicit parameters, or else they are retrieved from
  /// the MCID for the intrinsic.
  ///
  /// The parameter \p Res provides the Registers or MOs that will be defined by
  /// this instruction.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  ///
  /// \param ID Intrinsic identifier.
  /// \param Res Destination operand for the result.
  /// \param HasSideEffects Whether the intrinsic has side effects.
  /// \param isConvergent Whether the intrinsic is convergent.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildIntrinsic(Intrinsic::ID ID, ArrayRef<Register> Res,
                                     bool HasSideEffects, bool isConvergent);
  /// Build and insert a G_INTRINSIC with register results.
  /// \param ID Intrinsic identifier.
  /// \param Res Destination operand for the result.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildIntrinsic(Intrinsic::ID ID, ArrayRef<Register> Res);
  /// Build and insert a G_INTRINSIC with explicit side-effect properties.
  /// \param ID Intrinsic identifier.
  /// \param Res Destination operand for the result.
  /// \param HasSideEffects Whether the intrinsic has side effects.
  /// \param isConvergent Whether the intrinsic is convergent.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildIntrinsic(Intrinsic::ID ID, ArrayRef<DstOp> Res,
                                     bool HasSideEffects, bool isConvergent);
  /// Build and insert a G_INTRINSIC with DstOp results.
  /// \param ID Intrinsic identifier.
  /// \param Res Destination operand for the result.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildIntrinsic(Intrinsic::ID ID, ArrayRef<DstOp> Res);

  /// Build and insert \p Res = G_FPTRUNC \p Op
  ///
  /// G_FPTRUNC converts a floating-point value into one with a smaller type.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  /// \pre \p Res must be smaller than \p Op
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return The newly created instruction.
  MachineInstrBuilder
  buildFPTrunc(const DstOp &Res, const SrcOp &Op,
               std::optional<unsigned> Flags = std::nullopt);

  /// Build and insert \p Res = G_TRUNC \p Op
  ///
  /// G_TRUNC extracts the low bits of a type. For a vector type each element is
  /// truncated independently before being packed into the destination.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or vector type.
  /// \pre \p Op must be a generic virtual register with scalar or vector type.
  /// \pre \p Res must be smaller than \p Op
  ///
  /// \param Res Destination operand for the result.
  /// \param Op Source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return The newly created instruction.
  MachineInstrBuilder buildTrunc(const DstOp &Res, const SrcOp &Op,
                                 std::optional<unsigned> Flags = std::nullopt);

  /// Build and insert a \p Res = G_ICMP \p Pred, \p Op0, \p Op1.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or
  ///      vector type. Typically this starts as s1 or <N x s1>.
  /// \pre \p Op0 and Op1 must be generic virtual registers with the
  ///      same number of elements as \p Res. If \p Res is a scalar,
  ///      \p Op0 must be either a scalar or pointer.
  /// \pre \p Pred must be an integer predicate.
  /// \param Pred Integer comparison predicate.
  /// \param Res Destination for the compare result.
  /// \param Op0 First compare operand.
  /// \param Op1 Second compare operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildICmp(CmpInst::Predicate Pred, const DstOp &Res,
                                const SrcOp &Op0, const SrcOp &Op1,
                                std::optional<unsigned> Flags = std::nullopt);

  /// Build and insert a \p Res = G_FCMP \p Pred, \p Op0, \p Op1.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or
  ///      vector type. Typically this starts as s1 or <N x s1>.
  /// \pre \p Op0 and Op1 must be generic virtual registers with the
  ///      same number of elements as \p Res (or scalar, if \p Res is
  ///      scalar).
  /// \pre \p Pred must be a floating-point predicate.
  /// \param Pred Floating-point comparison predicate.
  /// \param Res Destination for the compare result.
  /// \param Op0 First compare operand.
  /// \param Op1 Second compare operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFCmp(CmpInst::Predicate Pred, const DstOp &Res,
                                const SrcOp &Op0, const SrcOp &Op1,
                                std::optional<unsigned> Flags = std::nullopt);

  /// Build and insert a \p Res = G_SCMP \p Op0, \p Op1.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or
  ///      vector type. Typically this starts as s2 or <N x s2>.
  /// \pre \p Op0 and Op1 must be generic virtual registers with the
  ///      same number of elements as \p Res. If \p Res is a scalar,
  ///      \p Op0 must be a scalar.
  /// \param Res Destination for the three-way compare result.
  /// \param Op0 First compare operand.
  /// \param Op1 Second compare operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSCmp(const DstOp &Res, const SrcOp &Op0,
                                const SrcOp &Op1);

  /// Build and insert a \p Res = G_UCMP \p Op0, \p Op1.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar or
  ///      vector type. Typically this starts as s2 or <N x s2>.
  /// \pre \p Op0 and Op1 must be generic virtual registers with the
  ///      same number of elements as \p Res. If \p Res is a scalar,
  ///      \p Op0 must be a scalar.
  /// \param Res Destination for the three-way compare result.
  /// \param Op0 First compare operand.
  /// \param Op1 Second compare operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUCmp(const DstOp &Res, const SrcOp &Op0,
                                const SrcOp &Op1);

  /// Build and insert a \p Res = G_IS_FPCLASS \p Src, \p Mask
  /// \param Res Destination operand for the result.
  /// \param Src Source operand.
  /// \param Mask Shuffle mask or FP-class bit mask.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildIsFPClass(const DstOp &Res, const SrcOp &Src,
                                     unsigned Mask) {
    return buildInstr(TargetOpcode::G_IS_FPCLASS, {Res},
                      {Src, SrcOp(static_cast<int64_t>(Mask))});
  }

  /// Build and insert a \p Res = G_SELECT \p Tst, \p Op0, \p Op1
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same type.
  /// \pre \p Tst must be a generic virtual register with scalar, pointer or
  ///      vector type. If vector then it must have the same number of
  ///      elements as the other parameters.
  ///
  /// \param Res Destination operand for the result.
  /// \param Tst Condition or test operand.
  /// \param Op0 First source operand.
  /// \param Op1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSelect(const DstOp &Res, const SrcOp &Tst,
                                  const SrcOp &Op0, const SrcOp &Op1,
                                  std::optional<unsigned> Flags = std::nullopt);

  /// Build and insert \p Res = G_INSERT_VECTOR_ELT \p Val, \p Elt, \p Idx.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res and \p Val must be a generic virtual register
  ///      with the same vector type.
  /// \pre \p Elt and \p Idx must be a generic virtual register
  ///      with scalar type.
  /// \param Res Destination vector.
  /// \param Val Source vector being updated.
  /// \param Elt Element value to insert.
  /// \param Idx Element index.
  /// \return The newly created instruction.
  MachineInstrBuilder buildInsertVectorElement(const DstOp &Res,
                                               const SrcOp &Val,
                                               const SrcOp &Elt,
                                               const SrcOp &Idx);

  /// Build and insert \p Res = G_EXTRACT_VECTOR_ELT \p Val, \p Idx
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar type.
  /// \pre \p Val must be a generic virtual register with vector type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Val Value operand.
  /// \param Idx Index operand or constant index.
  /// \return The newly created instruction.
  MachineInstrBuilder buildExtractVectorElementConstant(const DstOp &Res,
                                                        const SrcOp &Val,
                                                        const int Idx) {
    const TargetLowering *TLI = getMF().getSubtarget().getTargetLowering();
    LLT IdxTy = TLI->getVectorIdxLLT(getDataLayout());
    return buildExtractVectorElement(Res, Val, buildConstant(IdxTy, Idx));
  }

  /// Build and insert \p Res = G_EXTRACT_VECTOR_ELT \p Val, \p Idx
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register with scalar type.
  /// \pre \p Val must be a generic virtual register with vector type.
  /// \pre \p Idx must be a generic virtual register with scalar type.
  ///
  /// \param Res Destination operand for the result.
  /// \param Val Value operand.
  /// \param Idx Index operand or constant index.
  /// \return The newly created instruction.
  MachineInstrBuilder buildExtractVectorElement(const DstOp &Res,
                                                const SrcOp &Val,
                                                const SrcOp &Idx);

  /// Build and insert `OldValRes<def>, SuccessRes<def> =
  /// G_ATOMIC_CMPXCHG_WITH_SUCCESS Addr, CmpVal, NewVal, MMO`.
  ///
  /// Atomically replace the value at \p Addr with \p NewVal if it is currently
  /// \p CmpVal otherwise leaves it unchanged. Puts the original value from \p
  /// Addr in \p Res, along with an s1 indicating whether it was replaced.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register of scalar type.
  /// \pre \p SuccessRes must be a generic virtual register of scalar type. It
  ///      will be assigned 0 on failure and 1 on success.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, \p CmpVal, and \p NewVal must be generic virtual
  ///      registers of the same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param SuccessRes Destination receiving the success flag.
  /// \param Addr Address operand.
  /// \param CmpVal Expected current value for the compare-exchange.
  /// \param NewVal Replacement value written on success.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildAtomicCmpXchgWithSuccess(const DstOp &OldValRes, const DstOp &SuccessRes,
                                const SrcOp &Addr, const SrcOp &CmpVal,
                                const SrcOp &NewVal, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMIC_CMPXCHG Addr, CmpVal, NewVal,
  /// MMO`.
  ///
  /// Atomically replace the value at \p Addr with \p NewVal if it is currently
  /// \p CmpVal otherwise leaves it unchanged. Puts the original value from \p
  /// Addr in \p Res.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register of scalar type.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, \p CmpVal, and \p NewVal must be generic virtual
  ///      registers of the same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param CmpVal Expected current value for the compare-exchange.
  /// \param NewVal Replacement value written on success.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicCmpXchg(const DstOp &OldValRes,
                                         const SrcOp &Addr, const SrcOp &CmpVal,
                                         const SrcOp &NewVal,
                                         MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_<Opcode> Addr, Val, MMO`.
  ///
  /// Atomically read-modify-update the value at \p Addr with \p Val. Puts the
  /// original value from \p Addr in \p OldValRes. The modification is
  /// determined by the opcode.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param Opcode Target opcode for the instruction.
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMW(unsigned Opcode, const DstOp &OldValRes,
                                     const SrcOp &Addr, const SrcOp &Val,
                                     MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_XCHG Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with \p Val. Puts the original
  /// value from \p Addr in \p OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWXchg(Register OldValRes, Register Addr,
                                         Register Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_ADD Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the addition of \p Val and
  /// the original value. Puts the original value from \p Addr in \p OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWAdd(Register OldValRes, Register Addr,
                                        Register Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_SUB Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the subtraction of \p Val and
  /// the original value. Puts the original value from \p Addr in \p OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWSub(Register OldValRes, Register Addr,
                                        Register Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_AND Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the bitwise and of \p Val and
  /// the original value. Puts the original value from \p Addr in \p OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWAnd(Register OldValRes, Register Addr,
                                        Register Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_NAND Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the bitwise nand of \p Val
  /// and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWNand(Register OldValRes, Register Addr,
                                         Register Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_OR Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the bitwise or of \p Val and
  /// the original value. Puts the original value from \p Addr in \p OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWOr(Register OldValRes, Register Addr,
                                       Register Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_XOR Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the bitwise xor of \p Val and
  /// the original value. Puts the original value from \p Addr in \p OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWXor(Register OldValRes, Register Addr,
                                        Register Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_MAX Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the signed maximum of \p
  /// Val and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWMax(Register OldValRes, Register Addr,
                                        Register Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_MIN Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the signed minimum of \p
  /// Val and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWMin(Register OldValRes, Register Addr,
                                        Register Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_UMAX Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the unsigned maximum of \p
  /// Val and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWUmax(Register OldValRes, Register Addr,
                                         Register Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_UMIN Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the unsigned minimum of \p
  /// Val and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWUmin(Register OldValRes, Register Addr,
                                         Register Val, MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_FADD Addr, Val, MMO`.
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWFAdd(
    const DstOp &OldValRes, const SrcOp &Addr, const SrcOp &Val,
    MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_FSUB Addr, Val, MMO`.
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWFSub(
        const DstOp &OldValRes, const SrcOp &Addr, const SrcOp &Val,
        MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_FMAX Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the floating point maximum of
  /// \p Val and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWFMax(
        const DstOp &OldValRes, const SrcOp &Addr, const SrcOp &Val,
        MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_FMIN Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the floating point minimum of
  /// \p Val and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWFMin(
        const DstOp &OldValRes, const SrcOp &Addr, const SrcOp &Val,
        MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_FMAXIMUM Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the floating point maximum of
  /// \p Val and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWFMaximum(const DstOp &OldValRes,
                                             const SrcOp &Addr,
                                             const SrcOp &Val,
                                             MachineMemOperand &MMO);

  /// Build and insert `OldValRes<def> = G_ATOMICRMW_FMINIMUM Addr, Val, MMO`.
  ///
  /// Atomically replace the value at \p Addr with the floating point minimum of
  /// \p Val and the original value. Puts the original value from \p Addr in \p
  /// OldValRes.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p OldValRes must be a generic virtual register.
  /// \pre \p Addr must be a generic virtual register with pointer type.
  /// \pre \p OldValRes, and \p Val must be generic virtual registers of the
  ///      same type.
  ///
  /// \param OldValRes Destination receiving the previous memory value.
  /// \param Addr Address operand.
  /// \param Val Value operand.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAtomicRMWFMinimum(const DstOp &OldValRes,
                                             const SrcOp &Addr,
                                             const SrcOp &Val,
                                             MachineMemOperand &MMO);

  /// Build and insert `G_FENCE Ordering, Scope`.
  /// \param Ordering Atomic ordering for the fence.
  /// \param Scope Synchronization scope for the fence.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFence(unsigned Ordering, unsigned Scope);

  /// Build and insert G_PREFETCH \p Addr, \p RW, \p Locality, \p CacheType
  /// \param Addr Address operand.
  /// \param RW Read/write hint for the prefetch.
  /// \param Locality Temporal locality hint for the prefetch.
  /// \param CacheType Cache level hint for the prefetch.
  /// \param MMO Memory operand describing the access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildPrefetch(const SrcOp &Addr, unsigned RW,
                                    unsigned Locality, unsigned CacheType,
                                    MachineMemOperand &MMO);

  /// Build and insert \p Dst = G_FREEZE \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFreeze(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_FREEZE, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_BLOCK_ADDR \p BA
  ///
  /// G_BLOCK_ADDR computes the address of a basic block.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res must be a generic virtual register of a pointer type.
  ///
  /// \param Res Destination operand for the result.
  /// \param BA Block address whose pointer is materialized.
  /// \return The newly created instruction.
  MachineInstrBuilder buildBlockAddress(Register Res, const BlockAddress *BA);

  /// Build and insert \p Res = G_ADD \p Op0, \p Op1
  ///
  /// G_ADD sets \p Res to the sum of integer parameters \p Op0 and \p Op1,
  /// truncated to their width.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same (scalar or vector) type).
  ///
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.

  MachineInstrBuilder buildAdd(const DstOp &Dst, const SrcOp &Src0,
                               const SrcOp &Src1,
                               std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_ADD, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_SUB \p Op0, \p Op1
  ///
  /// G_SUB sets \p Res to the difference of integer parameters \p Op0 and
  /// \p Op1, truncated to their width.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same (scalar or vector) type).
  ///
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.

  MachineInstrBuilder buildSub(const DstOp &Dst, const SrcOp &Src0,
                               const SrcOp &Src1,
                               std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_SUB, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_MUL \p Op0, \p Op1
  ///
  /// G_MUL sets \p Res to the product of integer parameters \p Op0 and \p Op1,
  /// truncated to their width.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same (scalar or vector) type).
  ///
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildMul(const DstOp &Dst, const SrcOp &Src0,
                               const SrcOp &Src1,
                               std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_MUL, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_ABDS \p Op0, \p Op1
  ///
  /// G_ABDS return the signed absolute difference of \p Op0 and \p Op1.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same (scalar or vector) type).
  ///
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAbds(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1) {
    return buildInstr(TargetOpcode::G_ABDS, {Dst}, {Src0, Src1});
  }

  /// Build and insert \p Res = G_ABDU \p Op0, \p Op1
  ///
  /// G_ABDU return the unsigned absolute difference of \p Op0 and \p Op1.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same (scalar or vector) type).
  ///
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAbdu(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1) {
    return buildInstr(TargetOpcode::G_ABDU, {Dst}, {Src0, Src1});
  }

  /// Build and insert \p Dst = G_UMULH \p Src0, \p Src1.
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUMulH(const DstOp &Dst, const SrcOp &Src0,
                                 const SrcOp &Src1,
                                 std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_UMULH, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Dst = G_SMULH \p Src0, \p Src1.
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSMulH(const DstOp &Dst, const SrcOp &Src0,
                                 const SrcOp &Src1,
                                 std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_SMULH, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_UREM \p Op0, \p Op1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildURem(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_UREM, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Dst = G_FMUL \p Src0, \p Src1.
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFMul(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FMUL, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Dst = G_FMINNUM \p Src0, \p Src1.
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildFMinNum(const DstOp &Dst, const SrcOp &Src0, const SrcOp &Src1,
               std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FMINNUM, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Dst = G_FMAXNUM \p Src0, \p Src1.
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildFMaxNum(const DstOp &Dst, const SrcOp &Src0, const SrcOp &Src1,
               std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FMAXNUM, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Dst = G_FMINNUM_IEEE \p Src0, \p Src1.
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildFMinNumIEEE(const DstOp &Dst, const SrcOp &Src0, const SrcOp &Src1,
                   std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FMINNUM_IEEE, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Dst = G_FMAXNUM_IEEE \p Src0, \p Src1.
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildFMaxNumIEEE(const DstOp &Dst, const SrcOp &Src0, const SrcOp &Src1,
                   std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FMAXNUM_IEEE, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Dst = G_SHL \p Src0, \p Src1.
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildShl(const DstOp &Dst, const SrcOp &Src0,
                               const SrcOp &Src1,
                               std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_SHL, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Dst = G_LSHR \p Src0, \p Src1.
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildLShr(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_LSHR, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Dst = G_ASHR \p Src0, \p Src1.
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAShr(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_ASHR, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_AND \p Op0, \p Op1
  ///
  /// G_AND sets \p Res to the bitwise and of integer parameters \p Op0 and \p
  /// Op1.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same (scalar or vector) type).
  ///
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.

  MachineInstrBuilder buildAnd(const DstOp &Dst, const SrcOp &Src0,
                               const SrcOp &Src1) {
    return buildInstr(TargetOpcode::G_AND, {Dst}, {Src0, Src1});
  }

  /// Build and insert \p Res = G_OR \p Op0, \p Op1
  ///
  /// G_OR sets \p Res to the bitwise or of integer parameters \p Op0 and \p
  /// Op1.
  ///
  /// \pre setBasicBlock or setMI must have been called.
  /// \pre \p Res, \p Op0 and \p Op1 must be generic virtual registers
  ///      with the same (scalar or vector) type).
  ///
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildOr(const DstOp &Dst, const SrcOp &Src0,
                              const SrcOp &Src1,
                              std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_OR, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_XOR \p Op0, \p Op1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildXor(const DstOp &Dst, const SrcOp &Src0,
                               const SrcOp &Src1) {
    return buildInstr(TargetOpcode::G_XOR, {Dst}, {Src0, Src1});
  }

  /// Build and insert a bitwise not,
  /// \p NegOne = G_CONSTANT -1
  /// \p Res = G_OR \p Op0, NegOne
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildNot(const DstOp &Dst, const SrcOp &Src0) {
    auto NegOne = buildConstant(Dst.getLLTTy(*getMRI()), -1);
    return buildInstr(TargetOpcode::G_XOR, {Dst}, {Src0, NegOne});
  }

  /// Build and insert integer negation
  /// \p Zero = G_CONSTANT 0
  /// \p Res = G_SUB Zero, \p Op0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildNeg(const DstOp &Dst, const SrcOp &Src0) {
    auto Zero = buildConstant(Dst.getLLTTy(*getMRI()), 0);
    return buildInstr(TargetOpcode::G_SUB, {Dst}, {Zero, Src0});
  }

  /// Build and insert \p Res = G_CTPOP \p Op0, \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildCTPOP(const DstOp &Dst, const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_CTPOP, {Dst}, {Src0});
  }

  /// Build and insert \p Res = G_CTLZ \p Op0, \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildCTLZ(const DstOp &Dst, const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_CTLZ, {Dst}, {Src0});
  }

  /// Build and insert \p Res = G_CTLZ_ZERO_POISON \p Op0, \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildCTLZ_ZERO_POISON(const DstOp &Dst,
                                            const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_CTLZ_ZERO_POISON, {Dst}, {Src0});
  }

  /// Build and insert \p Res = G_CTTZ \p Op0, \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildCTTZ(const DstOp &Dst, const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_CTTZ, {Dst}, {Src0});
  }

  /// Build and insert \p Res = G_CTTZ_ZERO_POISON \p Op0, \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildCTTZ_ZERO_POISON(const DstOp &Dst,
                                            const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_CTTZ_ZERO_POISON, {Dst}, {Src0});
  }

  /// Build and insert \p Res = G_CTLS \p Op0, \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildCTLS(const DstOp &Dst, const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_CTLS, {Dst}, {Src0});
  }

  /// Build and insert \p Dst = G_BSWAP \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBSwap(const DstOp &Dst, const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_BSWAP, {Dst}, {Src0});
  }

  /// Build and insert \p Res = G_FADD \p Op0, \p Op1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFAdd(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FADD, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_STRICT_FADD \p Op0, \p Op1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildStrictFAdd(const DstOp &Dst, const SrcOp &Src0, const SrcOp &Src1,
                  std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_STRICT_FADD, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_FSUB \p Op0, \p Op1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFSub(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FSUB, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_FDIV \p Op0, \p Op1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFDiv(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FDIV, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_FMA \p Op0, \p Op1, \p Op2
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Src2 Third source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFMA(const DstOp &Dst, const SrcOp &Src0,
                               const SrcOp &Src1, const SrcOp &Src2,
                               std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FMA, {Dst}, {Src0, Src1, Src2}, Flags);
  }

  /// Build and insert \p Res = G_FMAD \p Op0, \p Op1, \p Op2
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Src2 Third source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFMAD(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1, const SrcOp &Src2,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FMAD, {Dst}, {Src0, Src1, Src2}, Flags);
  }

  /// Build and insert \p Res = G_FNEG \p Op0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFNeg(const DstOp &Dst, const SrcOp &Src0,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FNEG, {Dst}, {Src0}, Flags);
  }

  /// Build and insert \p Res = G_FABS \p Op0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFAbs(const DstOp &Dst, const SrcOp &Src0,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FABS, {Dst}, {Src0}, Flags);
  }

  /// Build and insert \p Dst = G_FCANONICALIZE \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildFCanonicalize(const DstOp &Dst, const SrcOp &Src0,
                     std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FCANONICALIZE, {Dst}, {Src0}, Flags);
  }

  /// Build and insert \p Dst = G_INTRINSIC_TRUNC \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildIntrinsicTrunc(const DstOp &Dst, const SrcOp &Src0,
                      std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_INTRINSIC_TRUNC, {Dst}, {Src0}, Flags);
  }

  /// Build and insert \p Res = GFFLOOR \p Op0, \p Op1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildFFloor(const DstOp &Dst, const SrcOp &Src0,
              std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FFLOOR, {Dst}, {Src0}, Flags);
  }

  /// Build and insert \p Dst = G_FLOG \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFLog(const DstOp &Dst, const SrcOp &Src,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FLOG, {Dst}, {Src}, Flags);
  }

  /// Build and insert \p Dst = G_FLOG2 \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFLog2(const DstOp &Dst, const SrcOp &Src,
                                 std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FLOG2, {Dst}, {Src}, Flags);
  }

  /// Build and insert \p Dst = G_FEXP2 \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFExp2(const DstOp &Dst, const SrcOp &Src,
                                 std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FEXP2, {Dst}, {Src}, Flags);
  }

  /// Build and insert \p Dst = G_FPOW \p Src0, \p Src1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFPow(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FPOW, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Dst = G_FLDEXP \p Src0, \p Src1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildFLdexp(const DstOp &Dst, const SrcOp &Src0, const SrcOp &Src1,
              std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FLDEXP, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Fract, \p Exp = G_FFREXP \p Src
  /// \param Fract Destination receiving the fractional part.
  /// \param Exp Destination receiving the exponent.
  /// \param Src Source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildFFrexp(const DstOp &Fract, const DstOp &Exp, const SrcOp &Src,
              std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FFREXP, {Fract, Exp}, {Src}, Flags);
  }

  /// Build and insert \p Sin, \p Cos = G_FSINCOS \p Src
  /// \param Sin Destination receiving sine.
  /// \param Cos Destination receiving cosine.
  /// \param Src Source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildFSincos(const DstOp &Sin, const DstOp &Cos, const SrcOp &Src,
               std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FSINCOS, {Sin, Cos}, {Src}, Flags);
  }

  /// Build and insert \p Fract, \p Int = G_FMODF \p Src
  /// \param Fract Destination receiving the fractional part.
  /// \param Int Destination receiving the integral part.
  /// \param Src Source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildModf(const DstOp &Fract, const DstOp &Int,
                                const SrcOp &Src,
                                std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FMODF, {Fract, Int}, {Src}, Flags);
  }

  /// Build and insert \p Res = G_FCOPYSIGN \p Op0, \p Op1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildFCopysign(const DstOp &Dst, const SrcOp &Src0, const SrcOp &Src1,
                 std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FCOPYSIGN, {Dst}, {Src0, Src1}, Flags);
  }

  /// Build and insert \p Res = G_UITOFP \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUITOFP(const DstOp &Dst, const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_UITOFP, {Dst}, {Src0});
  }

  /// Build and insert \p Res = G_SITOFP \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSITOFP(const DstOp &Dst, const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_SITOFP, {Dst}, {Src0});
  }

  /// Build and insert \p Res = G_FPTOUI \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFPTOUI(const DstOp &Dst, const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_FPTOUI, {Dst}, {Src0});
  }

  /// Build and insert \p Res = G_FPTOSI \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFPTOSI(const DstOp &Dst, const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_FPTOSI, {Dst}, {Src0});
  }

  /// Build and insert \p Res = G_FPTOUI_SAT \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFPTOUI_SAT(const DstOp &Dst, const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_FPTOUI_SAT, {Dst}, {Src0});
  }

  /// Build and insert \p Res = G_FPTOSI_SAT \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFPTOSI_SAT(const DstOp &Dst, const SrcOp &Src0) {
    return buildInstr(TargetOpcode::G_FPTOSI_SAT, {Dst}, {Src0});
  }

  /// Build and insert \p Dst = G_FRINT \p Src0
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildFRint(const DstOp &Dst, const SrcOp &Src0,
                                 std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_FRINT, {Dst}, {Src0}, Flags);
  }

  /// Build and insert \p Dst = G_INTRINSIC_ROUNDEVEN \p Src0, \p Src1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder
  buildIntrinsicRoundeven(const DstOp &Dst, const SrcOp &Src0,
                          std::optional<unsigned> Flags = std::nullopt) {
    return buildInstr(TargetOpcode::G_INTRINSIC_ROUNDEVEN, {Dst}, {Src0},
                      Flags);
  }

  /// Build and insert \p Res = G_SMIN \p Op0, \p Op1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSMin(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1) {
    return buildInstr(TargetOpcode::G_SMIN, {Dst}, {Src0, Src1});
  }

  /// Build and insert \p Res = G_SMAX \p Op0, \p Op1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSMax(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1) {
    return buildInstr(TargetOpcode::G_SMAX, {Dst}, {Src0, Src1});
  }

  /// Build and insert \p Res = G_UMIN \p Op0, \p Op1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUMin(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1) {
    return buildInstr(TargetOpcode::G_UMIN, {Dst}, {Src0, Src1});
  }

  /// Build and insert \p Res = G_UMAX \p Op0, \p Op1
  /// \param Dst Destination operand for the result.
  /// \param Src0 First source operand.
  /// \param Src1 Second source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUMax(const DstOp &Dst, const SrcOp &Src0,
                                const SrcOp &Src1) {
    return buildInstr(TargetOpcode::G_UMAX, {Dst}, {Src0, Src1});
  }

  /// Build and insert \p Dst = G_ABS \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildAbs(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_ABS, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_JUMP_TABLE \p JTI
  ///
  /// G_JUMP_TABLE sets \p Res to the address of the jump table specified by
  /// the jump table index \p JTI.
  ///
  /// \param PtrTy Pointer type of the jump-table address result.
  /// \param JTI Jump table index.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildJumpTable(const LLT PtrTy, unsigned JTI);

  /// Build and insert \p Res = G_VECREDUCE_SEQ_FADD \p ScalarIn, \p VecIn
  ///
  /// \p ScalarIn is the scalar accumulator input to start the sequential
  /// reduction operation of \p VecIn.
  /// \param Dst Destination operand for the result.
  /// \param ScalarIn Scalar accumulator input to the reduction.
  /// \param VecIn Vector being reduced.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceSeqFAdd(const DstOp &Dst,
                                            const SrcOp &ScalarIn,
                                            const SrcOp &VecIn) {
    return buildInstr(TargetOpcode::G_VECREDUCE_SEQ_FADD, {Dst},
                      {ScalarIn, {VecIn}});
  }

  /// Build and insert \p Res = G_VECREDUCE_SEQ_FMUL \p ScalarIn, \p VecIn
  ///
  /// \p ScalarIn is the scalar accumulator input to start the sequential
  /// reduction operation of \p VecIn.
  /// \param Dst Destination operand for the result.
  /// \param ScalarIn Scalar accumulator input to the reduction.
  /// \param VecIn Vector being reduced.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceSeqFMul(const DstOp &Dst,
                                            const SrcOp &ScalarIn,
                                            const SrcOp &VecIn) {
    return buildInstr(TargetOpcode::G_VECREDUCE_SEQ_FMUL, {Dst},
                      {ScalarIn, {VecIn}});
  }

  /// Build and insert \p Res = G_VECREDUCE_FADD \p Src
  ///
  /// \p ScalarIn is the scalar accumulator input to the reduction operation of
  /// \p VecIn.
  /// \param Dst Destination operand for the result.
  /// \param ScalarIn Scalar accumulator input to the reduction.
  /// \param VecIn Vector being reduced.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceFAdd(const DstOp &Dst,
                                         const SrcOp &ScalarIn,
                                         const SrcOp &VecIn) {
    return buildInstr(TargetOpcode::G_VECREDUCE_FADD, {Dst}, {ScalarIn, VecIn});
  }

  /// Build and insert \p Res = G_VECREDUCE_FMUL \p Src
  ///
  /// \p ScalarIn is the scalar accumulator input to the reduction operation of
  /// \p VecIn.
  /// \param Dst Destination operand for the result.
  /// \param ScalarIn Scalar accumulator input to the reduction.
  /// \param VecIn Vector being reduced.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceFMul(const DstOp &Dst,
                                         const SrcOp &ScalarIn,
                                         const SrcOp &VecIn) {
    return buildInstr(TargetOpcode::G_VECREDUCE_FMUL, {Dst}, {ScalarIn, VecIn});
  }

  /// Build and insert \p Res = G_VECREDUCE_FMAX \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceFMax(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_FMAX, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_VECREDUCE_FMIN \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceFMin(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_FMIN, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_VECREDUCE_FMAXIMUM \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceFMaximum(const DstOp &Dst,
                                             const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_FMAXIMUM, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_VECREDUCE_FMINIMUM \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceFMinimum(const DstOp &Dst,
                                             const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_FMINIMUM, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_VECREDUCE_ADD \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceAdd(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_ADD, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_VECREDUCE_MUL \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceMul(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_MUL, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_VECREDUCE_AND \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceAnd(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_AND, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_VECREDUCE_OR \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceOr(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_OR, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_VECREDUCE_XOR \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceXor(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_XOR, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_VECREDUCE_SMAX \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceSMax(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_SMAX, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_VECREDUCE_SMIN \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceSMin(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_SMIN, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_VECREDUCE_UMAX \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceUMax(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_UMAX, {Dst}, {Src});
  }

  /// Build and insert \p Res = G_VECREDUCE_UMIN \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildVecReduceUMin(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_VECREDUCE_UMIN, {Dst}, {Src});
  }

  /// Build and insert G_MEMCPY or G_MEMMOVE
  /// \param Opcode Target opcode for the instruction.
  /// \param DstPtr Destination pointer of the memory transfer.
  /// \param SrcPtr Source pointer of the memory transfer.
  /// \param Size Size operand or allocation size.
  /// \param DstMMO Memory operand for the destination access.
  /// \param SrcMMO Memory operand for the source access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildMemTransferInst(unsigned Opcode, const SrcOp &DstPtr,
                                           const SrcOp &SrcPtr,
                                           const SrcOp &Size,
                                           MachineMemOperand &DstMMO,
                                           MachineMemOperand &SrcMMO) {
    auto MIB = buildInstr(
        Opcode, {}, {DstPtr, SrcPtr, Size, SrcOp(INT64_C(0) /*isTailCall*/)});
    MIB.addMemOperand(&DstMMO);
    MIB.addMemOperand(&SrcMMO);
    return MIB;
  }

  /// Build and insert a G_MEMCPY memory transfer.
  /// \param DstPtr Destination pointer of the memory transfer.
  /// \param SrcPtr Source pointer of the memory transfer.
  /// \param Size Size operand or allocation size.
  /// \param DstMMO Memory operand for the destination access.
  /// \param SrcMMO Memory operand for the source access.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildMemCpy(const SrcOp &DstPtr, const SrcOp &SrcPtr,
                                  const SrcOp &Size, MachineMemOperand &DstMMO,
                                  MachineMemOperand &SrcMMO) {
    return buildMemTransferInst(TargetOpcode::G_MEMCPY, DstPtr, SrcPtr, Size,
                                DstMMO, SrcMMO);
  }

  /// Build and insert G_TRAP or G_DEBUGTRAP
  /// \param Debug If true, build G_DEBUGTRAP instead of G_TRAP.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildTrap(bool Debug = false) {
    return buildInstr(Debug ? TargetOpcode::G_DEBUGTRAP : TargetOpcode::G_TRAP);
  }

  /// Build and insert \p Dst = G_SBFX \p Src, \p LSB, \p Width.
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \param LSB Least-significant bit of the bitfield.
  /// \param Width Width of the bitfield.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSbfx(const DstOp &Dst, const SrcOp &Src,
                                const SrcOp &LSB, const SrcOp &Width) {
    return buildInstr(TargetOpcode::G_SBFX, {Dst}, {Src, LSB, Width});
  }

  /// Build and insert \p Dst = G_UBFX \p Src, \p LSB, \p Width.
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \param LSB Least-significant bit of the bitfield.
  /// \param Width Width of the bitfield.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildUbfx(const DstOp &Dst, const SrcOp &Src,
                                const SrcOp &LSB, const SrcOp &Width) {
    return buildInstr(TargetOpcode::G_UBFX, {Dst}, {Src, LSB, Width});
  }

  /// Build and insert \p Dst = G_ROTR \p Src, \p Amt
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \param Amt Rotate amount.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildRotateRight(const DstOp &Dst, const SrcOp &Src,
                                       const SrcOp &Amt) {
    return buildInstr(TargetOpcode::G_ROTR, {Dst}, {Src, Amt});
  }

  /// Build and insert \p Dst = G_ROTL \p Src, \p Amt
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \param Amt Rotate amount.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildRotateLeft(const DstOp &Dst, const SrcOp &Src,
                                      const SrcOp &Amt) {
    return buildInstr(TargetOpcode::G_ROTL, {Dst}, {Src, Amt});
  }

  /// Build and insert \p Dst = G_BITREVERSE \p Src
  /// \param Dst Destination operand for the result.
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildBitReverse(const DstOp &Dst, const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_BITREVERSE, {Dst}, {Src});
  }

  /// Build and insert \p Dst = G_GET_FPENV
  /// \param Dst Destination operand for the result.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildGetFPEnv(const DstOp &Dst) {
    return buildInstr(TargetOpcode::G_GET_FPENV, {Dst}, {});
  }

  /// Build and insert G_SET_FPENV \p Src
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSetFPEnv(const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_SET_FPENV, {}, {Src});
  }

  /// Build and insert G_RESET_FPENV
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildResetFPEnv() {
    return buildInstr(TargetOpcode::G_RESET_FPENV, {}, {});
  }

  /// Build and insert \p Dst = G_GET_FPMODE
  /// \param Dst Destination operand for the result.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildGetFPMode(const DstOp &Dst) {
    return buildInstr(TargetOpcode::G_GET_FPMODE, {Dst}, {});
  }

  /// Build and insert G_SET_FPMODE \p Src
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSetFPMode(const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_SET_FPMODE, {}, {Src});
  }

  /// Build and insert G_RESET_FPMODE
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildResetFPMode() {
    return buildInstr(TargetOpcode::G_RESET_FPMODE, {}, {});
  }

  /// Build and insert \p Dst = G_GET_ROUNDING
  /// \param Dst Destination operand for the result.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildGetRounding(const DstOp &Dst) {
    return buildInstr(TargetOpcode::G_GET_ROUNDING, {Dst}, {});
  }

  /// Build and insert G_SET_ROUNDING
  /// \param Src Source operand.
  /// \return a MachineInstrBuilder for the newly created instruction.
  MachineInstrBuilder buildSetRounding(const SrcOp &Src) {
    return buildInstr(TargetOpcode::G_SET_ROUNDING, {}, {Src});
  }

  /// Build and insert an instruction with the given opcode and operands.
  /// \param Opc Target opcode for the instruction.
  /// \param DstOps Destination operands defined by the instruction.
  /// \param SrcOps Source operands used by the instruction.
  /// \param Flags Optional instruction flags to attach.
  /// \return a MachineInstrBuilder for the newly created instruction.
  virtual MachineInstrBuilder
  buildInstr(unsigned Opc, ArrayRef<DstOp> DstOps, ArrayRef<SrcOp> SrcOps,
             std::optional<unsigned> Flags = std::nullopt);
};

} // End namespace llvm.
#endif // LLVM_CODEGEN_GLOBALISEL_MACHINEIRBUILDER_H
