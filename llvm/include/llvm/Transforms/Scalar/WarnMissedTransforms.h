//===- WarnMissedTransforms.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emit warnings if forced code transformations have not been performed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_WARNMISSEDTRANSFORMS_H
#define LLVM_TRANSFORMS_SCALAR_WARNMISSEDTRANSFORMS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
/// Pass that emits warnings when forced transformations were not performed.
class WarnMissedTransformationsPass
    : public OptionalPassInfoMixin<WarnMissedTransformationsPass> {
public:
  /// Construct a pass that warns about missed transformations.
  explicit WarnMissedTransformationsPass() = default;

  /// Emit warnings for forced transformations that were not applied.
  /// @param F Function to check for missed forced transformations.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_WARNMISSEDTRANSFORMS_H
