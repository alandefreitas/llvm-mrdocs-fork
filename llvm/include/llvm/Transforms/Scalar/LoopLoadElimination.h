//===- LoopLoadElimination.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This header defines the LoopLoadEliminationPass object. This pass forwards
/// loaded values around loop backedges to allow their use in subsequent
/// iterations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPLOADELIMINATION_H
#define LLVM_TRANSFORMS_SCALAR_LOOPLOADELIMINATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass to forward loads in a loop around the backedge to subsequent
/// iterations.
struct LoopLoadEliminationPass
    : public OptionalPassInfoMixin<LoopLoadEliminationPass> {
  /// Run loop load elimination over the function.
  /// @param F Function whose loops may have loads forwarded across backedges.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPLOADELIMINATION_H
