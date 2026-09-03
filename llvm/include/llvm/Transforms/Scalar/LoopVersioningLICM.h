//===- LoopVersioningLICM.h - LICM Loop Versioning ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPVERSIONINGLICM_H
#define LLVM_TRANSFORMS_SCALAR_LOOPVERSIONINGLICM_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class LPMUpdater;
class Loop;

/// Pass that versions loops to enable LICM when alias analysis is uncertain.
///
/// Creates a runtime-checked clone with aggressive no-alias assumptions so
/// LICM can hoist more when the check proves memory accesses do not alias.
class LoopVersioningLICMPass
    : public OptionalPassInfoMixin<LoopVersioningLICMPass> {
public:
  /// Run loop versioning for LICM over the loop.
  /// @param L Loop that may be versioned to enable more aggressive LICM.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param LAR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &LAR,
                                 LPMUpdater &U);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPVERSIONINGLICM_H
