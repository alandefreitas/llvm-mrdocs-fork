//===- llvm/CodeGen/BranchRelaxation.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_BRANCHRELAXATION_H
#define LLVM_CODEGEN_BRANCHRELAXATION_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that relaxes out-of-range machine branches.
///
/// Replaces branches that need to jump further than is supported by a
/// branch instruction.
class BranchRelaxationPass
    : public RequiredPassInfoMixin<BranchRelaxationPass> {
public:
  /// Relax out-of-range branches in \p MF.
  /// \param MF Machine function whose branches are relaxed.
  /// \param MFAM Analysis manager providing required analyses.
  /// \return The set of analyses preserved after branch relaxation.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_BRANCHRELAXATION_H
