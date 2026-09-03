//===- UnifyLoopExits.h - Redirect exiting edges to one block -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_UNIFYLOOPEXITS_H
#define LLVM_TRANSFORMS_UTILS_UNIFYLOOPEXITS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that redirects each natural loop's exiting edges through one block.
///
/// For each natural loop with multiple exit blocks, creates a new block such
/// that all exiting blocks branch to it, then redistributes control flow to
/// the original exit blocks.
class UnifyLoopExitsPass : public RequiredPassInfoMixin<UnifyLoopExitsPass> {
public:
  /// Run the unify-loop-exits pass over the function.
  /// @param F Function whose loop exits should be unified.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_UNIFYLOOPEXITS_H
