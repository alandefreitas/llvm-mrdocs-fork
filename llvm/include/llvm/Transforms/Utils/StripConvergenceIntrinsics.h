//===- StripConvergenceIntrinsics.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This pass strips convergence intrinsics and operand bundles as those are
/// only useful when modifying the CFG during IR passes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_STRIPCONVERGENCEINTRINSICS_H
#define LLVM_TRANSFORMS_UTILS_STRIPCONVERGENCEINTRINSICS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that strips convergence intrinsics and operand bundles.
///
/// Convergence intrinsics and operand bundles are only useful when modifying
/// the CFG during IR passes, so this pass removes them afterward.
class StripConvergenceIntrinsicsPass
    : public OptionalPassInfoMixin<StripConvergenceIntrinsicsPass> {
public:
  /// Run the strip-convergence-intrinsics pass over the function.
  /// @param F Function whose convergence intrinsics and operand bundles are
  /// stripped.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_STRIPCONVERGENCEINTRINSICS_H
