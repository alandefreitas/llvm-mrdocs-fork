//===- LoopUnrollAndJamPass.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPUNROLLANDJAMPASS_H
#define LLVM_TRANSFORMS_SCALAR_LOOPUNROLLANDJAMPASS_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class LPMUpdater;
class LoopNest;

/// A simple loop rotation transformation.
class LoopUnrollAndJamPass
    : public OptionalPassInfoMixin<LoopUnrollAndJamPass> {
  const int OptLevel;

public:
  /// Construct a LoopUnrollAndJam pass with the given optimization level.
  /// @param OptLevel Optimization level used to tune unroll-and-jam decisions.
  explicit LoopUnrollAndJamPass(int OptLevel = 2) : OptLevel(OptLevel) {}
  /// Run loop unroll-and-jam over the loop nest.
  /// @param L Loop nest whose nested loops may be unrolled and jammed.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(LoopNest &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPUNROLLANDJAMPASS_H
