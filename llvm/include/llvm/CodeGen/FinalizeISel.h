//===-- llvm/CodeGen/FinalizeISel.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_FINALIZEISEL_H
#define LLVM_CODEGEN_FINALIZEISEL_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that finalizes ISel and expands pseudo-instructions.
///
/// Expands ISel pseudos (including those that introduce control flow),
/// finalizes target lowering, and may adjust machine frame information.
class FinalizeISelPass : public RequiredPassInfoMixin<FinalizeISelPass> {
public:
  /// Finalize ISel and expand pseudo-instructions in \p MF.
  /// \param MF Machine function whose ISel output is finalized.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after finalizing ISel.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_FINALIZEISEL_H
