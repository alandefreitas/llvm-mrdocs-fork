//===- llvm/CodeGen/PHIElimination.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PHIELIMINATION_H
#define LLVM_CODEGEN_PHIELIMINATION_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that eliminates machine instruction PHI nodes by inserting copies.
class PHIEliminationPass : public RequiredPassInfoMixin<PHIEliminationPass> {
public:
  /// Eliminate PHI instructions in \p MF by inserting copies.
  /// \param MF Machine function whose PHIs are eliminated.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after eliminating PHIs.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_PHIELIMINATION_H
