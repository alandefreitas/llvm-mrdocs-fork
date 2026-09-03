//===- LoopPredication.h - Guard based loop predication pass ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass tries to convert loop variant range checks to loop invariant by
// widening checks across loop iterations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPPREDICATION_H
#define LLVM_TRANSFORMS_SCALAR_LOOPPREDICATION_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class LPMUpdater;
class Loop;
/// Performs Loop Predication Pass.
class LoopPredicationPass : public OptionalPassInfoMixin<LoopPredicationPass> {
public:
  /// Run loop predication over the loop.
  /// @param L Loop whose range checks may be widened.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPPREDICATION_H
