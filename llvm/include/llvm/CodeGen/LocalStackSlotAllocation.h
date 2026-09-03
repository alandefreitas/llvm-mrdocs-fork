//===- LocalStackSlotAllocation.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LOCALSTACKSLOTALLOCATION_H
#define LLVM_CODEGEN_LOCALSTACKSLOTALLOCATION_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that pre-allocates local frame indices to stack slots.
///
/// Assigns local frame indices relative to one another and allocates base
/// registers to access them when the target estimates they are out of range of
/// normal frame-pointer or stack-pointer addressing.
class LocalStackSlotAllocationPass
    : public RequiredPassInfoMixin<LocalStackSlotAllocationPass> {
public:
  /// Allocate local stack slots in \p MF.
  /// \param MF Machine function whose local frame indices are allocated.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after allocating local stack slots.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm
#endif // LLVM_CODEGEN_LOCALSTACKSLOTALLOCATION_H
