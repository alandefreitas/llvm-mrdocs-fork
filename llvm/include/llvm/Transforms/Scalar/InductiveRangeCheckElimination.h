//===- InductiveRangeCheckElimination.h - IRCE ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Inductive Range Check Elimination
// loop pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_INDUCTIVERANGECHECKELIMINATION_H
#define LLVM_TRANSFORMS_SCALAR_INDUCTIVERANGECHECKELIMINATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that eliminates inductive range checks from loops.
class IRCEPass : public OptionalPassInfoMixin<IRCEPass> {
public:
  /// Run inductive range check elimination over the function.
  /// @param F Function whose loops may have range checks eliminated.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_INDUCTIVERANGECHECKELIMINATION_H
