//===- InferAlignment.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Infer alignment for load, stores and other memory operations based on
// trailing zero known bits information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_INFERALIGNMENT_H
#define LLVM_TRANSFORMS_SCALAR_INFERALIGNMENT_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that infers stronger alignments for memory operations.
///
/// Uses trailing-zero known-bits information to raise the alignment of loads,
/// stores, and other memory operations where the pointer's low bits are known
/// to be zero.
struct InferAlignmentPass : public OptionalPassInfoMixin<InferAlignmentPass> {
  /// Run alignment inference over the function.
  /// @param F Function whose memory operations may be realigned.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_INFERALIGNMENT_H
