//===- DFAJumpThreading.h - Threads a switch statement inside a loop ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the DFAJumpThreading pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_DFAJUMPTHREADING_H
#define LLVM_TRANSFORMS_SCALAR_DFAJUMPTHREADING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass that jump-threads a predictable switch used as a DFA inside a loop.
///
/// When a switch variable in a loop is decided by the control-flow path taken,
/// this pass clones paths so successors branch unconditionally to the next
/// case, effectively threading the DFA.
struct DFAJumpThreadingPass : OptionalPassInfoMixin<DFAJumpThreadingPass> {
  /// Run DFA jump threading over the function.
  /// @param F Function whose loop switches may be jump-threaded.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_DFAJUMPTHREADING_H
