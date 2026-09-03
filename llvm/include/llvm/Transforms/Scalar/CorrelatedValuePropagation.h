//===- CorrelatedValuePropagation.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_CORRELATEDVALUEPROPAGATION_H
#define LLVM_TRANSFORMS_SCALAR_CORRELATEDVALUEPROPAGATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass that propagates values correlated with control-flow conditions.
///
/// Uses LazyValueInfo to exploit edge conditions and range information,
/// replacing values with constants, simplifying phis, selects, comparisons,
/// and switches, converting signed operations to unsigned when operands are
/// non-negative, and inferring no-wrap flags.
struct CorrelatedValuePropagationPass
    : OptionalPassInfoMixin<CorrelatedValuePropagationPass> {
  /// Run correlated value propagation over the function.
  /// @param F Function whose values may be simplified using CFG-derived facts.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_CORRELATEDVALUEPROPAGATION_H
