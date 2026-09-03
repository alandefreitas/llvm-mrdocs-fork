//===----------LoopIdiomVectorize.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_VECTORIZE_LOOPIDIOMVECTORIZE_H
#define LLVM_LIB_TRANSFORMS_VECTORIZE_LOOPIDIOMVECTORIZE_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {
/// Vectorization style used when rewriting recognized loop idioms.
enum class LoopIdiomVectorizeStyle {
  /// Use masked vector intrinsics.
  Masked,
  /// Use VP (predicated) intrinsics.
  Predicated
};

/// Pass that recognizes specific loop idioms and rewrites them with vector code.
class LoopIdiomVectorizePass
    : public OptionalPassInfoMixin<LoopIdiomVectorizePass> {
  LoopIdiomVectorizeStyle VectorizeStyle = LoopIdiomVectorizeStyle::Masked;

  // The VF used in vectorizing the byte compare pattern.
  unsigned ByteCompareVF = 16;

public:
  /// Construct a loop idiom vectorize pass with default style and VF.
  LoopIdiomVectorizePass() = default;
  /// Construct a loop idiom vectorize pass with the given vectorization style.
  /// @param S Vectorization style (masked or predicated).
  explicit LoopIdiomVectorizePass(LoopIdiomVectorizeStyle S)
      : VectorizeStyle(S) {}

  /// Construct a loop idiom vectorize pass with style and byte-compare VF.
  /// @param S Vectorization style (masked or predicated).
  /// @param BCVF Vectorization factor used for the byte-compare pattern.
  LoopIdiomVectorizePass(LoopIdiomVectorizeStyle S, unsigned BCVF)
      : VectorizeStyle(S), ByteCompareVF(BCVF) {}

  /// Run loop idiom vectorization over the loop.
  /// @param L Loop whose idioms may be recognized and vectorized.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};
} // namespace llvm
#endif // LLVM_LIB_TRANSFORMS_VECTORIZE_LOOPIDIOMVECTORIZE_H
