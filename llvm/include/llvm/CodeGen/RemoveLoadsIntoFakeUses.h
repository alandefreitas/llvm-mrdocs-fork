//===- llvm/CodeGen/RemoveLoadsIntoFakeUses.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REMOVELOADSINTOFAKEUSES_H
#define LLVM_CODEGEN_REMOVELOADSINTOFAKEUSES_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that removes loads that are only used by FAKE_USE instructions.
class RemoveLoadsIntoFakeUsesPass
    : public OptionalPassInfoMixin<RemoveLoadsIntoFakeUsesPass> {
public:
  /// Remove loads into FAKE_USE instructions in \p MF.
  /// \param MF Machine function whose fake-use-only loads are removed.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after removing loads into fake uses.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Removing loads into fake uses expects that the function contains no
  /// virtual registers.
  /// \return Required properties, with NoVRegs set.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_REMOVELOADSINTOFAKEUSES_H
