//==- CanonicalizeFreezeInLoop.h - Canonicalize freezes in a loop-*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file canonicalizes freeze instructions in a loop.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CANONICALIZEFREEZEINLOOPS_H
#define LLVM_TRANSFORMS_UTILS_CANONICALIZEFREEZEINLOOPS_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class Loop;
class LPMUpdater;

/// A pass that canonicalizes freeze instructions in a loop.
class CanonicalizeFreezeInLoopsPass
    : public OptionalPassInfoMixin<CanonicalizeFreezeInLoopsPass> {
public:
  /// Run the canonicalize-freeze-in-loops pass over the loop.
  /// @param L Loop whose freeze instructions are canonicalized.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CANONICALIZEFREEZEINLOOPS_H
