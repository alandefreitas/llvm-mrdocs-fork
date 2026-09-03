//===- llvm/CodeGen/GlobalISel/InlineAsmLowering.h --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes how to lower LLVM inline asm to machine code INLINEASM.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_INLINEASMLOWERING_H
#define LLVM_CODEGEN_GLOBALISEL_INLINEASMLOWERING_H

#include "llvm/ADT/ArrayRef.h"
#include <functional>

namespace llvm {
class CallBase;
class MachineIRBuilder;
class MachineOperand;
class Register;
class TargetLowering;
class Value;

/// Interface for lowering LLVM inline asm to GlobalISel MIR.
class LLVM_ABI InlineAsmLowering {
  const TargetLowering *TLI;

  virtual void anchor();

public:
  /// Lower the given inline asm call instruction.
  /// \param MIRBuilder Builder used to insert the inline asm lowering.
  /// \param CB Call or invoke instruction representing the inline asm.
  /// \param GetOrCreateVRegs Callback to materialize a register for the
  ///        input and output operands of the inline asm.
  /// \return True if the lowering succeeds, false otherwise.
  bool lowerInlineAsm(MachineIRBuilder &MIRBuilder, const CallBase &CB,
                      std::function<ArrayRef<Register>(const Value &Val)>
                          GetOrCreateVRegs) const;

  /// Lower the specified operand into the Ops vector.
  /// \param Val IR input value to be lowered.
  /// \param Constraint User-supplied constraint string.
  /// \param Ops Vector to be filled with the lowered operands.
  /// \param MIRBuilder Builder used to insert supporting instructions.
  /// \return True if the lowering succeeds, false otherwise.
  virtual bool lowerAsmOperandForConstraint(Value *Val, StringRef Constraint,
                                            std::vector<MachineOperand> &Ops,
                                            MachineIRBuilder &MIRBuilder) const;

protected:
  /// Getter for generic TargetLowering class.
  /// \return Pointer to the TargetLowering used by this lowering.
  const TargetLowering *getTLI() const { return TLI; }

  /// Getter for target specific TargetLowering class.
  /// \return Pointer to the TargetLowering cast to \p XXXTargetLowering.
  template <class XXXTargetLowering> const XXXTargetLowering *getTLI() const {
    return static_cast<const XXXTargetLowering *>(TLI);
  }

public:
  /// Construct an InlineAsmLowering using the target's TargetLowering.
  /// \param TLI Target lowering info providing target hooks.
  InlineAsmLowering(const TargetLowering *TLI) : TLI(TLI) {}
  /// Virtual destructor for polymorphic InlineAsmLowering subclasses.
  virtual ~InlineAsmLowering() = default;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_INLINEASMLOWERING_H
