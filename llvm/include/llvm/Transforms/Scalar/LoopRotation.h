//===- LoopRotation.h - Loop Rotation -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Loop Rotation pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPROTATION_H
#define LLVM_TRANSFORMS_SCALAR_LOOPROTATION_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class LPMUpdater;
class Loop;

/// A simple loop rotation transformation.
class LoopRotatePass : public OptionalPassInfoMixin<LoopRotatePass> {
public:
  /// Construct a loop rotation pass with the given options.
  /// @param EnableHeaderDuplication When true, allow rotating by duplicating
  /// the loop header up to the default size threshold.
  /// @param PrepareForLTO When true, run in the prepare-for-LTO stage (e.g.
  /// defer rotating loops with inline candidates).
  /// @param CheckExitCount When true, consider exit-count information when
  /// deciding whether to rotate.
  LLVM_ABI LoopRotatePass(bool EnableHeaderDuplication = true,
                          bool PrepareForLTO = false,
                          bool CheckExitCount = false);
  /// Run loop rotation over the loop.
  /// @param L Loop to rotate.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);

  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  const bool EnableHeaderDuplication;
  const bool PrepareForLTO;
  const bool CheckExitCount;
};
}

#endif // LLVM_TRANSFORMS_SCALAR_LOOPROTATION_H
