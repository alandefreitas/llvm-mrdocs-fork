//===- ConstraintElimination.h - Constraint elimination pass ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_CONSTRAINTELIMINATION_H
#define LLVM_TRANSFORMS_SCALAR_CONSTRAINTELIMINATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that eliminates conditions using constraints from dominating conditions.
///
/// Collects linear constraints from dominating compares, assumes, and similar
/// facts, then proves later conditions always true or false and removes them.
class ConstraintEliminationPass
    : public OptionalPassInfoMixin<ConstraintEliminationPass> {
public:
  /// Run constraint elimination over the function.
  /// @param F Function whose conditions may be simplified.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_CONSTRAINTELIMINATION_H
