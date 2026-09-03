//===- GuardWidening.h - ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Guard widening is an optimization over the @llvm.experimental.guard intrinsic
// that (optimistically) combines multiple guards into one to have fewer checks
// at runtime.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_GUARDWIDENING_H
#define LLVM_TRANSFORMS_SCALAR_GUARDWIDENING_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class LPMUpdater;
class Loop;
class Function;

/// Pass that widens @llvm.experimental.guard intrinsics by combining them.
///
/// Guard widening optimistically merges multiple guards into one so fewer
/// checks remain at runtime.
struct GuardWideningPass : public OptionalPassInfoMixin<GuardWideningPass> {
  /// Run guard widening over the function.
  /// @param F Function whose guards may be widened.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  /// Run guard widening over the loop.
  /// @param L Loop whose guards may be widened.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_GUARDWIDENING_H
