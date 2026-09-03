//===- llvm/CodeGen/TwoAddressInstructionPass.h -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TWOADDRESSINSTRUCTIONPASS_H
#define LLVM_CODEGEN_TWOADDRESSINSTRUCTIONPASS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that rewrites two-address instructions to use tied operands.
///
/// Reduces three-operand forms to two-address forms expected by register
/// allocators. This destroys SSA information but is desired by most
/// register allocators.
class TwoAddressInstructionPass
    : public RequiredPassInfoMixin<TwoAddressInstructionPass> {
public:
  /// Rewrite two-address instructions in \p MF.
  /// \param MF Machine function whose two-address instructions are rewritten.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after rewriting two-address instructions.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
  /// Return the properties this pass sets on the machine function.
  ///
  /// Marks that tied-def operands have been rewritten for constraints.
  /// \return Properties with TiedOpsRewritten set.
  MachineFunctionProperties getSetProperties() const {
    return MachineFunctionProperties().setTiedOpsRewritten();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_TWOADDRESSINSTRUCTIONPASS_H
