//===- llvm/CodeGen/MachineCSE.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINECSE_H
#define LLVM_CODEGEN_MACHINECSE_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that performs global CSE on machine instructions.
class MachineCSEPass : public OptionalPassInfoMixin<MachineCSEPass> {
public:
  /// Perform common subexpression elimination on \p MF.
  /// \param MF Machine function whose redundant instructions are eliminated.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after common subexpression elimination.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Machine CSE expects the function to be in SSA form.
  /// \return Properties requiring the function to be in SSA form.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setIsSSA();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINECSE_H
