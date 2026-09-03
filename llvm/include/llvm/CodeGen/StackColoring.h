//===- llvm/CodeGen/StackColoring.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_STACKCOLORINGPASS_H
#define LLVM_CODEGEN_STACKCOLORINGPASS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that merges disjoint stack slots to reduce frame size.
///
/// Uses LIFETIME_START and LIFETIME_END markers to find non-overlapping
/// stack allocations and color them onto shared slots. Distinct from
/// StackSlotColoring, which optimizes spill slots.
class StackColoringPass : public RequiredPassInfoMixin<StackColoringPass> {
public:
  /// Merge disjoint stack slots in \p MF.
  /// \param MF Machine function whose stack frame is colored.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after stack coloring.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_STACKCOLORINGPASS_H
