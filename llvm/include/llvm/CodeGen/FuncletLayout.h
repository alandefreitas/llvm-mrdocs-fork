//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_FUNCLETLAYOUT_H
#define LLVM_CODEGEN_FUNCLETLAYOUT_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that lays out funclets contiguously.
///
/// Reorders basic blocks so that each funclet's blocks are contiguous, which
/// is required for funclet-based exception handling personalities.
class FuncletLayoutPass : public RequiredPassInfoMixin<FuncletLayoutPass> {
public:
  /// Contiguously lay out funclets in \p MF.
  /// \param MF Machine function whose funclets are laid out.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after laying out funclets.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Funclet layout expects that the function contains no virtual registers.
  /// \return Required properties, with NoVRegs set.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_FUNCLETLAYOUT_H
