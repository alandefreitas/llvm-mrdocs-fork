//===- llvm/CodeGen/MachineCopyPropagation.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINECOPYPROPAGATION_H
#define LLVM_CODEGEN_MACHINECOPYPROPAGATION_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that performs copy propagation on machine instructions.
class MachineCopyPropagationPass
    : public OptionalPassInfoMixin<MachineCopyPropagationPass> {
  bool UseCopyInstr;

public:
  /// Construct a MachineCopyPropagation pass.
  /// \param UseCopyInstr Also treat target-specific copy-like instructions as
  ///        copies.
  MachineCopyPropagationPass(bool UseCopyInstr = false)
      : UseCopyInstr(UseCopyInstr) {}

  /// Propagate copies through machine instructions in \p MF.
  /// \param MF Machine function whose copies are propagated.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after copy propagation.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Copy propagation expects that the function contains no virtual registers.
  /// \return Required properties, with NoVRegs set.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINECOPYPROPAGATION_H
