//===- llvm/CodeGen/StackSlotColoring.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_STACKSLOTCOLORING_H
#define LLVM_CODEGEN_STACKSLOTCOLORING_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that colors stack slots to share non-overlapping spill slots.
///
/// Assigns the same physical stack slot to live intervals that do not overlap,
/// reducing frame size by reusing spill slots.
class StackSlotColoringPass
    : public OptionalPassInfoMixin<StackSlotColoringPass> {
public:
  /// Color stack slots in \p MF.
  /// \param MF Machine function whose spill slots are colored.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after stack slot coloring.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_STACKSLOTCOLORING_H
