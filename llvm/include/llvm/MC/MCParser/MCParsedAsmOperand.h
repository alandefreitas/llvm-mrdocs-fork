//===- llvm/MC/MCParsedAsmOperand.h - Asm Parser Operand --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_MCPARSEDASMOPERAND_H
#define LLVM_MC_MCPARSER_MCPARSEDASMOPERAND_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SMLoc.h"
#include <string>

namespace llvm {

class MCAsmInfo;
class MCRegister;
class raw_ostream;

/// Abstract representation of a source-level assembly instruction operand.
///
/// It should be subclassed by target-specific code. This base class is used by
/// target-independent clients and is the interface between parsing an asm
/// instruction and recognizing it.
class LLVM_ABI MCParsedAsmOperand {
  /// MCOperandNum - The corresponding MCInst operand number.  Only valid when
  /// parsing MS-style inline assembly.
  unsigned MCOperandNum = ~0u;

  /// Constraint - The constraint on this operand.  Only valid when parsing
  /// MS-style inline assembly.
  std::string Constraint;

protected:
  // This only seems to need to be movable (by ARMOperand) but ARMOperand has
  // lots of members and MSVC doesn't support defaulted move ops, so to avoid
  // that verbosity, just rely on defaulted copy ops. It's only the Constraint
  // string member that would benefit from movement anyway.
  /// Construct an empty parsed assembly operand.
  MCParsedAsmOperand() = default;
  /// Construct a copy of another parsed assembly operand.
  ///
  /// \param RHS Operand to copy from.
  MCParsedAsmOperand(const MCParsedAsmOperand &RHS) = default;
  /// Copy-assign from another parsed assembly operand.
  ///
  /// \param RHS Operand to copy from.
  /// \return A reference to this operand.
  MCParsedAsmOperand &operator=(const MCParsedAsmOperand &RHS) = default;

public:
  /// Destroy this parsed assembly operand.
  virtual ~MCParsedAsmOperand() = default;

  /// Set the MS-style inline assembly constraint for this operand.
  ///
  /// \param C Constraint string to store.
  void setConstraint(StringRef C) { Constraint = C.str(); }
  /// Return the MS-style inline assembly constraint for this operand.
  ///
  /// \return The constraint string for this operand.
  StringRef getConstraint() { return Constraint; }

  /// Set the corresponding MCInst operand number.
  ///
  /// Only valid when parsing MS-style inline assembly.
  ///
  /// \param OpNum Operand index in the MCInst.
  void setMCOperandNum (unsigned OpNum) { MCOperandNum = OpNum; }
  /// Return the corresponding MCInst operand number.
  ///
  /// Only valid when parsing MS-style inline assembly.
  ///
  /// \return The MCInst operand index for this operand.
  unsigned getMCOperandNum() { return MCOperandNum; }

  /// Return the symbol name for this operand, if any.
  ///
  /// \return The symbol name, or an empty string if none.
  virtual StringRef getSymName() { return StringRef(); }
  /// Return an opaque declaration pointer for this operand, if any.
  ///
  /// \return An opaque declaration pointer, or nullptr if none.
  virtual void *getOpDecl() { return nullptr; }

  /// isToken - Is this a token operand?
  ///
  /// \return True if this is a token operand.
  virtual bool isToken() const = 0;
  /// isImm - Is this an immediate operand?
  ///
  /// \return True if this is an immediate operand.
  virtual bool isImm() const = 0;
  /// isReg - Is this a register operand?
  ///
  /// \return True if this is a register operand.
  virtual bool isReg() const = 0;
  /// Return the register for this register operand.
  ///
  /// \return The register for this operand.
  virtual MCRegister getReg() const = 0;

  /// isMem - Is this a memory operand?
  ///
  /// \return True if this is a memory operand.
  virtual bool isMem() const = 0;

  /// Return true if this memory operand consumes registers in its address.
  ///
  /// For example, Intel MS inline asm may use ARR[baseReg + IndexReg + ...]
  /// which may use up regs in the [...] expression, so ARR[baseReg + IndexReg
  /// + ...] cannot use an extra reg for ARR. For example, calculating ARR
  /// address to a reg or use another base reg in PIC model.
  ///
  /// \return True if this memory operand consumes registers in its address.
  virtual bool isMemUseUpRegs() const { return false; }

  /// getStartLoc - Get the location of the first token of this operand.
  ///
  /// \return The source location of the first token of this operand.
  virtual SMLoc getStartLoc() const = 0;
  /// getEndLoc - Get the location of the last token of this operand.
  ///
  /// \return The source location of the last token of this operand.
  virtual SMLoc getEndLoc() const = 0;

  /// needAddressOf - Do we need to emit code to get the address of the
  /// variable/label?   Only valid when parsing MS-style inline assembly.
  ///
  /// \return True if code must be emitted to take the address of the
  /// variable or label.
  virtual bool needAddressOf() const { return false; }

  /// isOffsetOfLocal - Do we need to emit code to get the offset of the local
  /// variable, rather than its value?   Only valid when parsing MS-style inline
  /// assembly.
  ///
  /// \return True if code must be emitted to get the local variable's offset.
  virtual bool isOffsetOfLocal() const { return false; }

  /// getOffsetOfLoc - Get the location of the offset operator.
  ///
  /// \return The source location of the offset operator.
  virtual SMLoc getOffsetOfLoc() const { return SMLoc(); }

  /// Print a debug representation of the operand to the given stream.
  ///
  /// \param OS Stream to write the debug representation to.
  /// \param MAI Target assembly information used for printing.
  virtual void print(raw_ostream &OS, const MCAsmInfo &MAI) const = 0;

  /// dump - Print to the debug stream.
  virtual void dump() const;
};
} // end namespace llvm

#endif // LLVM_MC_MCPARSER_MCPARSEDASMOPERAND_H
