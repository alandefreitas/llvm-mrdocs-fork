//===-- llvm/MC/MCInstBuilder.h - Simplify creation of MCInsts --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the MCInstBuilder class for convenient creation of
// MCInsts.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTBUILDER_H
#define LLVM_MC_MCINSTBUILDER_H

#include "llvm/MC/MCInst.h"

namespace llvm {

/// Convenience builder for constructing MCInst objects.
class MCInstBuilder {
  MCInst Inst;

public:
  /// Create a new MCInstBuilder for an MCInst with a specific opcode.
  ///
  /// \param Opcode - Opcode for the instruction being built.
  MCInstBuilder(unsigned Opcode) {
    Inst.setOpcode(Opcode);
  }

  /// Set the location.
  ///
  /// \param SM - Source location to attach to the instruction.
  /// \return Reference to this builder for chaining.
  MCInstBuilder &setLoc(SMLoc SM) {
    Inst.setLoc(SM);
    return *this;
  }

  /// Add a new register operand.
  ///
  /// \param Reg - Register to add as an operand.
  /// \return Reference to this builder for chaining.
  MCInstBuilder &addReg(MCRegister Reg) {
    Inst.addOperand(MCOperand::createReg(Reg));
    return *this;
  }

  /// Add a new integer immediate operand.
  ///
  /// \param Val - Immediate integer value to add as an operand.
  /// \return Reference to this builder for chaining.
  MCInstBuilder &addImm(int64_t Val) {
    Inst.addOperand(MCOperand::createImm(Val));
    return *this;
  }

  /// Add a new single floating point immediate operand.
  ///
  /// \param Val - Single-precision floating-point immediate bit pattern.
  /// \return Reference to this builder for chaining.
  MCInstBuilder &addSFPImm(uint32_t Val) {
    Inst.addOperand(MCOperand::createSFPImm(Val));
    return *this;
  }

  /// Add a new floating point immediate operand.
  ///
  /// \param Val - Double-precision floating-point immediate bit pattern.
  /// \return Reference to this builder for chaining.
  MCInstBuilder &addDFPImm(uint64_t Val) {
    Inst.addOperand(MCOperand::createDFPImm(Val));
    return *this;
  }

  /// Add a new MCExpr operand.
  ///
  /// \param Val - Expression to add as a relocatable operand.
  /// \return Reference to this builder for chaining.
  MCInstBuilder &addExpr(const MCExpr *Val) {
    Inst.addOperand(MCOperand::createExpr(Val));
    return *this;
  }

  /// Add a new MCInst operand.
  ///
  /// \param Val - Sub-instruction to add as an operand.
  /// \return Reference to this builder for chaining.
  MCInstBuilder &addInst(const MCInst *Val) {
    Inst.addOperand(MCOperand::createInst(Val));
    return *this;
  }

  /// Add an operand.
  ///
  /// \param Op - Operand to append to the instruction.
  /// \return Reference to this builder for chaining.
  MCInstBuilder &addOperand(const MCOperand &Op) {
    Inst.addOperand(Op);
    return *this;
  }

  /// Convert to a reference to the constructed MCInst.
  ///
  /// \return Reference to the constructed MCInst.
  operator MCInst&() {
    return Inst;
  }
};

} // end namespace llvm

#endif
