//===- LowerMatrixIntrinsics.h - Lower matrix intrinsics. -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers matrix intrinsics down to vector operations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOWERMATRIXINTRINSICS_H
#define LLVM_TRANSFORMS_SCALAR_LOWERMATRIXINTRINSICS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
/// Pass that lowers matrix intrinsics to vector operations.
class LowerMatrixIntrinsicsPass
    : public RequiredPassInfoMixin<LowerMatrixIntrinsicsPass> {
  bool Minimal;

public:
  /// Construct a pass that lowers matrix intrinsics.
  /// @param Minimal When true, run the minimal backend-pipeline variant that
  /// skips analysis-dependent lowering.
  LowerMatrixIntrinsicsPass(bool Minimal = false) : Minimal(Minimal) {}
  /// Run the pass over the function.
  /// @param F Function whose matrix intrinsics may be lowered.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);
};
} // namespace llvm

#endif
