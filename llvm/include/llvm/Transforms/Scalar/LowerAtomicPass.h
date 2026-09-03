//===- LowerAtomicPass.h - Lower atomic intrinsics --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
// This pass lowers atomic intrinsics to non-atomic form for use in a known
// non-preemptible environment.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOWERATOMICPASS_H
#define LLVM_TRANSFORMS_SCALAR_LOWERATOMICPASS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// A pass that lowers atomic intrinsic into non-atomic intrinsics.
class LowerAtomicPass : public RequiredPassInfoMixin<LowerAtomicPass> {
public:
  /// Run atomic lowering over the function.
  /// @param F Function whose atomic operations may be lowered.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_LOWERATOMICPASS_H
