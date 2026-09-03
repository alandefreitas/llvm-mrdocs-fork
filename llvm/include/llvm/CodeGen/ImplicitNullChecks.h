//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_IMPLICITNULLCHECKS_H
#define LLVM_CODEGEN_IMPLICITNULLCHECKS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that folds null pointer checks into nearby memory operations.
class ImplicitNullChecksPass
    : public OptionalPassInfoMixin<ImplicitNullChecksPass> {
public:
  /// Fold null pointer checks into nearby memory operations in \p MF.
  /// \param MF Machine function whose null checks are folded.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after folding null checks.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Implicit null checks expect that the function contains no virtual
  /// registers.
  /// \return Required properties, with NoVRegs set.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_IMPLICITNULLCHECKS_H
