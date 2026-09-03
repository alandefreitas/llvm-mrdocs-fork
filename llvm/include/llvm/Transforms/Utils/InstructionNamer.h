//===- InstructionNamer.h - Give anonymous instructions names -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_INSTRUCTIONNAMER_H
#define LLVM_TRANSFORMS_UTILS_INSTRUCTIONNAMER_H

#include "llvm/IR/PassManager.h"

namespace llvm {
/// Pass that assigns names to anonymous arguments, blocks, and instructions.
///
/// Useful when diffing the effect of an optimization, because deleting an
/// unnamed instruction can renumber other instructions and make the diff
/// noisy.
struct InstructionNamerPass : OptionalPassInfoMixin<InstructionNamerPass> {
  /// Run the instruction-namer pass over the function.
  /// @param F Function whose anonymous values should be named.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_INSTRUCTIONNAMER_H
