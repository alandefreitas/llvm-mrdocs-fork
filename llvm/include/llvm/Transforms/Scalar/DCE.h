//===- DCE.h - Dead code elimination ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Dead Code Elimination pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_DCE_H
#define LLVM_TRANSFORMS_SCALAR_DCE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Basic Dead Code Elimination pass.
class DCEPass : public OptionalPassInfoMixin<DCEPass> {
public:
  /// Run dead code elimination over the function.
  /// @param F Function to eliminate dead code from.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Pass that removes redundant debug instructions from a function.
///
/// Walks each basic block and deletes dbg.value instructions that are made
/// obsolete by later debug descriptions of the same variable.
class RedundantDbgInstEliminationPass
    : public OptionalPassInfoMixin<RedundantDbgInstEliminationPass> {
public:
  /// Run redundant debug-instruction elimination over the function.
  /// @param F Function whose debug instructions may be pruned.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_DCE_H
