//===- llvm/CodeGen/PatchableFunction.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PATCHABLEFUNCTION_H
#define LLVM_CODEGEN_PATCHABLEFUNCTION_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that inserts patchable prologue markers for the
/// "patchable-function" and "patchable-function-entry" attributes.
class PatchableFunctionPass
    : public RequiredPassInfoMixin<PatchableFunctionPass> {
public:
  /// Insert patchable function markers into \p MF when requested by attributes.
  /// \param MF Machine function to make patchable.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after inserting patchable markers.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Patchable function insertion expects that the function contains no
  /// virtual registers.
  /// \return Required properties, with NoVRegs set.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_PATCHABLEFUNCTION_H
