//===- ScalarizeMaskedMemIntrin.h - Scalarize unsupported masked mem ----===//
//                                    intrinsics
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass replaces masked memory intrinsics - when unsupported by the target
// - with a chain of basic blocks, that deal with the elements one-by-one if the
// appropriate mask bit is set.
//
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_TRANSFORMS_SCALAR_SCALARIZEMASKEDMEMINTRIN_H
#define LLVM_TRANSFORMS_SCALAR_SCALARIZEMASKEDMEMINTRIN_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that scalarizes masked memory intrinsics unsupported by the target.
///
/// Replaces those intrinsics with a chain of basic blocks that handle each
/// element one-by-one when the corresponding mask bit is set.
struct ScalarizeMaskedMemIntrinPass
    : public RequiredPassInfoMixin<ScalarizeMaskedMemIntrinPass> {
  /// Run masked-memory intrinsic scalarization over the function.
  /// @param F Function whose unsupported masked memory intrinsics may be
  /// scalarized.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif
