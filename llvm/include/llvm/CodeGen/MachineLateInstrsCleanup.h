//===- llvm/CodeGen/MachineLateInstrsCleanup.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_MACHINELATEINSTRSCLEANUP_H
#define LLVM_CODEGEN_MACHINELATEINSTRSCLEANUP_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that removes redundant identical instructions after register
/// allocation and rematerialization.
class MachineLateInstrsCleanupPass
    : public OptionalPassInfoMixin<MachineLateInstrsCleanupPass> {
public:
  /// Remove redundant identical instructions in \p MachineFunction.
  /// \param MachineFunction Machine function whose redundant late instructions
  ///        are cleaned up.
  /// \param MachineFunctionAM Machine function analysis manager providing
  ///        required analyses.
  /// \return The set of analyses preserved after late instruction cleanup.
  LLVM_ABI PreservedAnalyses
  run(MachineFunction &MachineFunction,
      MachineFunctionAnalysisManager &MachineFunctionAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Late instruction cleanup expects that the function contains no virtual
  /// registers.
  /// \return Required properties, with NoVRegs set.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINELATEINSTRSCLEANUP_H
