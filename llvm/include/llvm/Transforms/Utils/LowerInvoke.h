//===- LowerInvoke.h - Eliminate Invoke instructions ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This transformation is designed for use by code generators which do not yet
// support stack unwinding.  This pass converts 'invoke' instructions to 'call'
// instructions, so that any exception-handling 'landingpad' blocks become dead
// code (which can be removed by running the '-simplifycfg' pass afterwards).
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_UTILS_LOWERINVOKE_H
#define LLVM_TRANSFORMS_UTILS_LOWERINVOKE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that converts invoke instructions to call instructions.
///
/// Designed for code generators that do not yet support stack unwinding.
/// After conversion, exception-handling landingpad blocks become dead code
/// (which can be removed by running the '-simplifycfg' pass afterwards).
class LowerInvokePass : public RequiredPassInfoMixin<LowerInvokePass> {
public:
  /// Run the lower-invoke pass over the function.
  /// @param F Function whose invoke instructions should be lowered.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif // LLVM_TRANSFORMS_UTILS_LOWERINVOKE_H
