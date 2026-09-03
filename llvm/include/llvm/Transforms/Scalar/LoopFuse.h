//===- LoopFuse.h - Loop Fusion Pass ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the Loop Fusion pass.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPFUSE_H
#define LLVM_TRANSFORMS_SCALAR_LOOPFUSE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass that fuses adjacent loops to improve locality and reduce overhead.
class LoopFusePass : public OptionalPassInfoMixin<LoopFusePass> {
public:
  /// Run loop fusion over the function.
  /// @param F Function whose loops may be fused.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPFUSE_H
