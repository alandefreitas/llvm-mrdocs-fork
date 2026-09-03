//===- llvm/CodeGen/OptimizePHIs.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_OPTIMIZE_PHIS_H
#define LLVM_CODEGEN_OPTIMIZE_PHIS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that optimizes machine instruction PHIs.
class OptimizePHIsPass : public OptionalPassInfoMixin<OptimizePHIsPass> {
public:
  /// Optimize PHI instructions in \p MF.
  /// \param MF Machine function whose PHIs are optimized.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after optimizing PHIs.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_OPTIMIZE_PHIS_H
