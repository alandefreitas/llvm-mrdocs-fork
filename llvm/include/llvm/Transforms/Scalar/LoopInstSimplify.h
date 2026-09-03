//===- LoopInstSimplify.h - Loop Inst Simplify Pass -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs lightweight instruction simplification on loop bodies.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPINSTSIMPLIFY_H
#define LLVM_TRANSFORMS_SCALAR_LOOPINSTSIMPLIFY_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Loop;
class LPMUpdater;

/// Performs Loop Inst Simplify Pass.
class LoopInstSimplifyPass
    : public OptionalPassInfoMixin<LoopInstSimplifyPass> {
public:
  /// Run lightweight instruction simplification over the loop body.
  /// @param L Loop whose body instructions may be simplified.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPINSTSIMPLIFY_H
