//===- LowerSwitch.h - Eliminate Switch instructions ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The LowerSwitch transformation rewrites switch instructions with a sequence
// of branches, which allows targets to get away with not implementing the
// switch instruction until it is convenient.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOWERSWITCH_H
#define LLVM_TRANSFORMS_UTILS_LOWERSWITCH_H

#include "llvm/IR/PassManager.h"

namespace llvm {
/// Pass that rewrites switch instructions as a sequence of branches.
///
/// This allows targets to get away with not implementing the switch
/// instruction until it is convenient.
struct LowerSwitchPass : public RequiredPassInfoMixin<LowerSwitchPass> {
  /// Run the lower-switch pass over the function.
  /// @param F Function whose switch instructions should be lowered.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOWERSWITCH_H
