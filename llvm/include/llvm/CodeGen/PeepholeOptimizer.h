//===- llvm/CodeGen/PeepholeOptimizer.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PEEPHOLEOPTIMIZER_H
#define LLVM_CODEGEN_PEEPHOLEOPTIMIZER_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that peep-hole optimizes machine instructions.
class PeepholeOptimizerPass
    : public OptionalPassInfoMixin<PeepholeOptimizerPass> {
public:
  /// Run peep-hole optimizations on machine instructions in \p MF.
  /// \param MF Machine function whose instructions are optimized.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after peep-hole optimization.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// The peep-hole optimizer expects the function to be in SSA form.
  /// \return Properties requiring the function to be in SSA form.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setIsSSA();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_PEEPHOLEOPTIMIZER_H
