//===- IndVarSimplify.h - Induction Variable Simplification -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Induction Variable
// Simplification pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_INDVARSIMPLIFY_H
#define LLVM_TRANSFORMS_SCALAR_INDVARSIMPLIFY_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Loop;
class LPMUpdater;

/// Pass that simplifies induction variables in a loop.
class IndVarSimplifyPass : public OptionalPassInfoMixin<IndVarSimplifyPass> {
  /// Perform IV widening during the pass.
  bool WidenIndVars;

public:
  /// Construct an induction variable simplification pass.
  /// @param WidenIndVars When true, widen induction variables during the pass.
  IndVarSimplifyPass(bool WidenIndVars = true) : WidenIndVars(WidenIndVars) {}
  /// Run induction variable simplification over the loop.
  /// @param L Loop whose induction variables may be simplified.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_INDVARSIMPLIFY_H
