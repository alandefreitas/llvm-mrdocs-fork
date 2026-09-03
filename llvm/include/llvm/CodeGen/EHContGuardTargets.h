//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_EHCONTGUARDTARGETS_H
#define LLVM_CODEGEN_EHCONTGUARDTARGETS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that records Windows EH Continuation Guard targets.
///
/// Inserts a symbol before each valid target where the Windows unwinder may
/// continue after an exception and stores these in the MachineFunction's
/// EHContTargets vector for use when emitting the /guard:ehcont table.
class EHContGuardTargetsPass
    : public RequiredPassInfoMixin<EHContGuardTargetsPass> {
public:
  /// Identify and record EH Continuation Guard targets in \p MF.
  /// \param MF Machine function whose EH continuation targets are recorded.
  /// \param MFAM Analysis manager providing required analyses.
  /// \return The set of analyses preserved after recording EH continuation
  /// targets.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_EHCONTGUARDTARGETS_H
