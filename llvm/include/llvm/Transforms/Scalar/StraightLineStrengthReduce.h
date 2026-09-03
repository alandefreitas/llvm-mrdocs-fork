//===- StraightLineStrengthReduce.h - -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_STRAIGHTLINESTRENGTHREDUCE_H
#define LLVM_TRANSFORMS_SCALAR_STRAIGHTLINESTRENGTHREDUCE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Performs straight-line strength reduction on arithmetic expressions.
///
/// Unlike loop strength reduction, this pass reduces arithmetic redundancy in
/// straight-line code (typically from unrolled loops) by rewriting dominated
/// Add/Mul/GEP candidates relative to an earlier basis.
class StraightLineStrengthReducePass
    : public OptionalPassInfoMixin<StraightLineStrengthReducePass> {
public:
  /// Run straight-line strength reduction over the function.
  /// @param F Function whose straight-line arithmetic may be strength-reduced.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_STRAIGHTLINESTRENGTHREDUCE_H
