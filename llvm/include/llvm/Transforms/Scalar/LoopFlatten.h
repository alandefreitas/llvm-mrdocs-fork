//===- LoopFlatten.h - Loop Flatten ----------------  -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Loop Flatten Pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPFLATTEN_H
#define LLVM_TRANSFORMS_SCALAR_LOOPFLATTEN_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class LPMUpdater;
class LoopNest;

/// Pass that flattens nested loops into a single loop.
class LoopFlattenPass : public OptionalPassInfoMixin<LoopFlattenPass> {
public:
  /// Construct a LoopFlatten pass.
  LoopFlattenPass() = default;

  /// Run loop flattening over the loop nest.
  /// @param LN Loop nest whose nested loops may be flattened.
  /// @param LAM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(LoopNest &LN, LoopAnalysisManager &LAM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPFLATTEN_H
