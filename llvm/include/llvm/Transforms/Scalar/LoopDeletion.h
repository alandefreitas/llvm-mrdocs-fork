//===- LoopDeletion.h - Loop Deletion ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Loop Deletion Pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPDELETION_H
#define LLVM_TRANSFORMS_SCALAR_LOOPDELETION_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Loop;
class LPMUpdater;

/// Pass that deletes loops that are provably dead.
class LoopDeletionPass : public OptionalPassInfoMixin<LoopDeletionPass> {
public:
  /// Construct a LoopDeletion pass.
  LoopDeletionPass() = default;

  /// Run loop deletion over the loop.
  /// @param L Loop that may be deleted if dead.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPDELETION_H
