//===- FixIrreducible.h - Convert irreducible control-flow into loops -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_FIXIRREDUCIBLE_H
#define LLVM_TRANSFORMS_UTILS_FIXIRREDUCIBLE_H

#include "llvm/IR/PassManager.h"

namespace llvm {
/// Pass that converts irreducible control-flow into natural loops.
struct FixIrreduciblePass : RequiredPassInfoMixin<FixIrreduciblePass> {
  /// Run the fix-irreducible pass over the function.
  /// @param F Function whose irreducible control-flow should be fixed.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_FIXIRREDUCIBLE_H
