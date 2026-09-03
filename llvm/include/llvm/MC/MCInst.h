//===- llvm/MC/MCInst.h - MCInst class --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCInst and MCOperand classes, which
// is the basic representation used to represent low-level machine code
// instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINST_H
#define LLVM_MC_MCINST_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/bit.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SMLoc.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace llvm {

class MCContext;
class MCExpr;
class MCInst;
class MCInstPrinter;
class MCRegisterInfo;
class raw_ostream;

/// Instances of this class represent operands of the MCInst class.
/// This is a simple discriminated union.
class MCOperand {
  enum MachineOperandType : unsigned char {
    kInvalid,      ///< Uninitialized.
    kRegister,     ///< Register operand.
    kImmediate,    ///< Immediate operand.
    kSFPImmediate, ///< Single-floating-point immediate operand.
    kDFPImmediate, ///< Double-Floating-point immediate operand.
    kExpr,         ///< Relocatable immediate operand.
    kInst          ///< Sub-instruction operand.
  };
  MachineOperandType Kind = kInvalid;

  union {
    /// Register number when Kind is kRegister.
    unsigned RegVal;
    /// Signed immediate value when Kind is kImmediate.
    int64_t ImmVal;
    /// Single-precision floating-point bit pattern when Kind is kSFPImmediate.
    uint32_t SFPImmVal;
    /// Double-precision floating-point bit pattern when Kind is kDFPImmediate.
    uint64_t FPImmVal;
    /// Relocatable expression when Kind is kExpr.
    const MCExpr *ExprVal;
    /// Nested sub-instruction when Kind is kInst.
    const MCInst *InstVal;
  };

public:
  /// Construct an invalid (empty) machine operand.
  MCOperand() : FPImmVal(0) {}

  /// Return true if this operand is initialized.
  ///
  /// \returns True if this operand is initialized.
  bool isValid() const { return Kind != kInvalid; }
  /// Return true if this is a register operand.
  ///
  /// \returns True if this is a register operand.
  bool isReg() const { return Kind == kRegister; }
  /// Return true if this is an immediate operand.
  ///
  /// \returns True if this is an immediate operand.
  bool isImm() const { return Kind == kImmediate; }
  /// Return true if this is a single-precision floating-point immediate.
  ///
  /// \returns True if this is a single-precision floating-point immediate.
  bool isSFPImm() const { return Kind == kSFPImmediate; }
  /// Return true if this is a double-precision floating-point immediate.
  ///
  /// \returns True if this is a double-precision floating-point immediate.
  bool isDFPImm() const { return Kind == kDFPImmediate; }
  /// Return true if this is a relocatable expression operand.
  ///
  /// \returns True if this is a relocatable expression operand.
  bool isExpr() const { return Kind == kExpr; }
  /// Return true if this is a sub-instruction operand.
  ///
  /// \returns True if this is a sub-instruction operand.
  bool isInst() const { return Kind == kInst; }

  /// Returns the register number.
  ///
  /// \returns The register number.
  MCRegister getReg() const {
    assert(isReg() && "This is not a register operand!");
    return RegVal;
  }

  /// Set the register number.
  ///
  /// \param Reg - Register to store in this operand.
  void setReg(MCRegister Reg) {
    assert(isReg() && "This is not a register operand!");
    RegVal = Reg.id();
  }

  /// Return the signed immediate value.
  ///
  /// \returns The signed immediate value.
  int64_t getImm() const {
    assert(isImm() && "This is not an immediate");
    return ImmVal;
  }

  /// Set the signed immediate value.
  ///
  /// \param Val - Immediate value to store.
  void setImm(int64_t Val) {
    assert(isImm() && "This is not an immediate");
    ImmVal = Val;
  }

  /// Return the single-precision floating-point immediate bit pattern.
  ///
  /// \returns The single-precision floating-point immediate bit pattern.
  uint32_t getSFPImm() const {
    assert(isSFPImm() && "This is not an SFP immediate");
    return SFPImmVal;
  }

  /// Set the single-precision floating-point immediate bit pattern.
  ///
  /// \param Val - Bit pattern of the single-precision float to store.
  void setSFPImm(uint32_t Val) {
    assert(isSFPImm() && "This is not an SFP immediate");
    SFPImmVal = Val;
  }

  /// Return the double-precision floating-point immediate bit pattern.
  ///
  /// \returns The double-precision floating-point immediate bit pattern.
  uint64_t getDFPImm() const {
    assert(isDFPImm() && "This is not an FP immediate");
    return FPImmVal;
  }

  /// Set the double-precision floating-point immediate bit pattern.
  ///
  /// \param Val - Bit pattern of the double-precision float to store.
  void setDFPImm(uint64_t Val) {
    assert(isDFPImm() && "This is not an FP immediate");
    FPImmVal = Val;
  }
  /// Set the double-precision floating-point immediate from a host double.
  ///
  /// \param Val - Double value whose bit pattern is stored.
  void setFPImm(double Val) {
    assert(isDFPImm() && "This is not an FP immediate");
    FPImmVal = bit_cast<uint64_t>(Val);
  }

  /// Return the relocatable expression stored in this operand.
  ///
  /// \returns The relocatable expression stored in this operand.
  const MCExpr *getExpr() const {
    assert(isExpr() && "This is not an expression");
    return ExprVal;
  }

  /// Set the relocatable expression stored in this operand.
  ///
  /// \param Val - Expression to store.
  void setExpr(const MCExpr *Val) {
    assert(isExpr() && "This is not an expression");
    ExprVal = Val;
  }

  /// Return the sub-instruction stored in this operand.
  ///
  /// \returns The sub-instruction stored in this operand.
  const MCInst *getInst() const {
    assert(isInst() && "This is not a sub-instruction");
    return InstVal;
  }

  /// Set the sub-instruction stored in this operand.
  ///
  /// \param Val - Sub-instruction to store.
  void setInst(const MCInst *Val) {
    assert(isInst() && "This is not a sub-instruction");
    InstVal = Val;
  }

  /// Create a register operand holding \p Reg.
  ///
  /// \param Reg - Register to store in the new operand.
  /// \returns A register operand holding \p Reg.
  static MCOperand createReg(MCRegister Reg) {
    MCOperand Op;
    Op.Kind = kRegister;
    Op.RegVal = Reg.id();
    return Op;
  }

  /// Create an immediate operand holding \p Val.
  ///
  /// \param Val - Immediate value to store in the new operand.
  /// \returns An immediate operand holding \p Val.
  static MCOperand createImm(int64_t Val) {
    MCOperand Op;
    Op.Kind = kImmediate;
    Op.ImmVal = Val;
    return Op;
  }

  /// Create a single-precision floating-point immediate operand.
  ///
  /// \param Val - Bit pattern of the single-precision float to store.
  /// \returns A single-precision floating-point immediate operand.
  static MCOperand createSFPImm(uint32_t Val) {
    MCOperand Op;
    Op.Kind = kSFPImmediate;
    Op.SFPImmVal = Val;
    return Op;
  }

  /// Create a double-precision floating-point immediate operand.
  ///
  /// \param Val - Bit pattern of the double-precision float to store.
  /// \returns A double-precision floating-point immediate operand.
  static MCOperand createDFPImm(uint64_t Val) {
    MCOperand Op;
    Op.Kind = kDFPImmediate;
    Op.FPImmVal = Val;
    return Op;
  }

  /// Create a relocatable expression operand holding \p Val.
  ///
  /// \param Val - Expression to store in the new operand.
  /// \returns A relocatable expression operand holding \p Val.
  static MCOperand createExpr(const MCExpr *Val) {
    MCOperand Op;
    Op.Kind = kExpr;
    Op.ExprVal = Val;
    return Op;
  }

  /// Create a sub-instruction operand holding \p Val.
  ///
  /// \param Val - Sub-instruction to store in the new operand.
  /// \returns A sub-instruction operand holding \p Val.
  static MCOperand createInst(const MCInst *Val) {
    MCOperand Op;
    Op.Kind = kInst;
    Op.InstVal = Val;
    return Op;
  }

  /// Print this operand to \p OS.
  ///
  /// \param OS - Stream to print to.
  /// \param Ctx - Optional context used when printing expressions.
  LLVM_ABI void print(raw_ostream &OS, const MCContext *Ctx = nullptr) const;
  /// Dump this operand to stderr for debugging.
  LLVM_ABI void dump() const;
  /// Return true if this operand is a bare symbol reference expression.
  ///
  /// \returns True if this operand is a bare symbol reference expression.
  LLVM_ABI bool isBareSymbolRef() const;
  /// Evaluate this operand as a constant immediate into \p Imm.
  ///
  /// \param Imm - Set to the constant value on success.
  /// \returns True if the operand evaluates to a constant immediate.
  LLVM_ABI bool evaluateAsConstantImm(int64_t &Imm) const;
};

/// Instances of this class represent a single low-level machine
/// instruction.
class MCInst {
  unsigned Opcode = 0;
  // These flags could be used to pass some info from one target subcomponent
  // to another, for example, from disassembler to asm printer. The values of
  // the flags have any sense on target level only (e.g. prefixes on x86).
  unsigned Flags = 0;

  SMLoc Loc;
  SmallVector<MCOperand, 6> Operands;

public:
  /// Construct an empty machine instruction with opcode zero.
  MCInst() = default;

  /// Set the opcode of this instruction.
  ///
  /// \param Op - Opcode value to store.
  void setOpcode(unsigned Op) { Opcode = Op; }
  /// Return the opcode of this instruction.
  ///
  /// \returns The opcode of this instruction.
  unsigned getOpcode() const { return Opcode; }

  /// Set the target-specific flags for this instruction.
  ///
  /// \param F - Flag bits to store.
  void setFlags(unsigned F) { Flags = F; }
  /// Return the target-specific flags for this instruction.
  ///
  /// \returns The target-specific flags for this instruction.
  unsigned getFlags() const { return Flags; }

  /// Set the source location associated with this instruction.
  ///
  /// \param loc - Source location to store.
  void setLoc(SMLoc loc) { Loc = loc; }
  /// Return the source location associated with this instruction.
  ///
  /// \returns The source location associated with this instruction.
  SMLoc getLoc() const { return Loc; }

  /// Return the operand at index \p i.
  ///
  /// \param i - Zero-based operand index.
  /// \returns The operand at index \p i.
  const MCOperand &getOperand(unsigned i) const { return Operands[i]; }
  /// Return a mutable reference to the operand at index \p i.
  ///
  /// \param i - Zero-based operand index.
  /// \returns A mutable reference to the operand at index \p i.
  MCOperand &getOperand(unsigned i) { return Operands[i]; }
  /// Return the number of operands in this instruction.
  ///
  /// \returns The number of operands in this instruction.
  unsigned getNumOperands() const { return Operands.size(); }

  /// Return a read-only view of this instruction's operands.
  ///
  /// \returns A read-only view of this instruction's operands.
  ArrayRef<MCOperand> getOperands() const { return Operands; }
  /// Append \p Op to this instruction's operand list.
  ///
  /// \param Op - Operand to append.
  void addOperand(const MCOperand Op) { Operands.push_back(Op); }
  /// Replace this instruction's operands with \p Ops.
  ///
  /// \param Ops - New operand sequence.
  void setOperands(ArrayRef<MCOperand> Ops) {
    Operands.assign(Ops.begin(), Ops.end());
  }

  /// Mutable iterator over this instruction's operands.
  using iterator = SmallVectorImpl<MCOperand>::iterator;
  /// Const iterator over this instruction's operands.
  using const_iterator = SmallVectorImpl<MCOperand>::const_iterator;

  /// Remove all operands from this instruction.
  void clear() { Operands.clear(); }
  /// Erase the operand at iterator \p I.
  ///
  /// \param I - Iterator to the operand to erase.
  void erase(iterator I) { Operands.erase(I); }
  /// Erase the operands in the half-open range [\p First, \p Last).
  ///
  /// \param First - Iterator to the first operand to erase.
  /// \param Last - Iterator one past the last operand to erase.
  void erase(iterator First, iterator Last) { Operands.erase(First, Last); }
  /// Return the number of operands in this instruction.
  ///
  /// \returns The number of operands in this instruction.
  size_t size() const { return Operands.size(); }
  /// Return an iterator to the first operand.
  ///
  /// \returns An iterator to the first operand.
  iterator begin() { return Operands.begin(); }
  /// Return a const iterator to the first operand.
  ///
  /// \returns A const iterator to the first operand.
  const_iterator begin() const { return Operands.begin(); }
  /// Return an iterator past the last operand.
  ///
  /// \returns An iterator past the last operand.
  iterator end() { return Operands.end(); }
  /// Return a const iterator past the last operand.
  ///
  /// \returns A const iterator past the last operand.
  const_iterator end() const { return Operands.end(); }

  /// Insert \p Op before the operand at iterator \p I.
  ///
  /// \param I - Insertion position.
  /// \param Op - Operand to insert.
  /// \returns Iterator to the inserted operand.
  iterator insert(iterator I, const MCOperand &Op) {
    return Operands.insert(I, Op);
  }

  /// Print this instruction to \p OS.
  ///
  /// \param OS - Stream to print to.
  /// \param Ctx - Optional context used when printing operands.
  LLVM_ABI void print(raw_ostream &OS, const MCContext *Ctx = nullptr) const;
  /// Dump this instruction to stderr for debugging.
  LLVM_ABI void dump() const;

  /// Dump the MCInst as prettily as possible using the additional MC
  /// structures, if given. Operators are separated by the \p Separator
  /// string.
  ///
  /// \param OS - Stream to print to.
  /// \param Printer - Optional printer used to format the instruction.
  /// \param Separator - String inserted between operands.
  /// \param Ctx - Optional context used when printing operands.
  LLVM_ABI void dump_pretty(raw_ostream &OS,
                            const MCInstPrinter *Printer = nullptr,
                            StringRef Separator = " ",
                            const MCContext *Ctx = nullptr) const;
  /// Dump the MCInst using the given mnemonic \p Name instead of looking it
  /// up. Operators are separated by the \p Separator string.
  ///
  /// \param OS - Stream to print to.
  /// \param Name - Mnemonic to print instead of looking up the opcode.
  /// \param Separator - String inserted between operands.
  /// \param Ctx - Optional context used when printing operands.
  LLVM_ABI void dump_pretty(raw_ostream &OS, StringRef Name,
                            StringRef Separator = " ",
                            const MCContext *Ctx = nullptr) const;
};

/// Print \p MO to \p OS.
///
/// \param OS - Stream to print to.
/// \param MO - Operand to print.
/// \returns A reference to \p OS.
inline raw_ostream& operator<<(raw_ostream &OS, const MCOperand &MO) {
  MO.print(OS);
  return OS;
}

/// Print \p MI to \p OS.
///
/// \param OS - Stream to print to.
/// \param MI - Instruction to print.
/// \returns A reference to \p OS.
inline raw_ostream& operator<<(raw_ostream &OS, const MCInst &MI) {
  MI.print(OS);
  return OS;
}

} // end namespace llvm

#endif // LLVM_MC_MCINST_H
