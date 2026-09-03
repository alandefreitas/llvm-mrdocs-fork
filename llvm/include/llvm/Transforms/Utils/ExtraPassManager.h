//===- ExtraFunctionPassManager.h - Run Optimizations on Demand -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides a pass manager that only runs its passes if the
/// provided marker analysis has been preserved, together with a class to
/// define such a marker analysis.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_EXTRAPASSMANAGER_H
#define LLVM_TRANSFORMS_UTILS_EXTRAPASSMANAGER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {

/// A marker analysis to determine if extra passes should be run on demand.
///
/// Passes requesting extra transformations to run need to request and preserve this analysis.
template <typename MarkerTy> struct ShouldRunExtraPasses {
  /// Result of the ShouldRunExtraPasses marker analysis.
  struct Result {
    /// Invalidate this result unless the marker analysis is preserved.
    /// @param F Function associated with this analysis result.
    /// @param PA Set of analyses that have been preserved.
    /// @param Inv Invalidator for dependent function analyses.
    /// @return True if this result should be invalidated.
    bool invalidate(Function &F, const PreservedAnalyses &PA,
                    FunctionAnalysisManager::Invalidator &Inv) {
      // Check whether the analysis has been explicitly invalidated. Otherwise,
      // it remains preserved.
      auto PAC = PA.getChecker<MarkerTy>();
      return !PAC.preservedWhenStateless();
    }

    /// Invalidate this result unless the marker analysis is preserved.
    /// @param L Loop associated with this analysis result.
    /// @param PA Set of analyses that have been preserved.
    /// @param Inv Invalidator for dependent loop analyses.
    /// @return True if this result should be invalidated.
    bool invalidate(Loop &L, const PreservedAnalyses &PA,
                    LoopAnalysisManager::Invalidator &Inv) {
      // Check whether the analysis has been explicitly invalidated. Otherwise,
      // it remains preserved.
      auto PAC = PA.getChecker<MarkerTy>();
      return !PAC.preservedWhenStateless();
    }
  };

  /// Compute the ShouldRunExtraPasses marker result for a function.
  /// @param F Function for which the marker analysis is requested.
  /// @param FAM Function analysis manager providing analyses for the pass.
  /// @return A fresh marker analysis result.
  Result run(Function &F, FunctionAnalysisManager &FAM) { return Result(); }

  /// Compute the ShouldRunExtraPasses marker result for a loop.
  /// @param L Loop for which the marker analysis is requested.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @return A fresh marker analysis result.
  Result run(Loop &L, LoopAnalysisManager &AM,
             LoopStandardAnalysisResults &AR) {
    return Result();
  }
};

/// A pass manager that runs extra function passes when a marker analysis is present.
///
/// A pass manager to run a set of extra function passes if the
/// ShouldRunExtraPasses marker analysis is present. This allows passes to
/// request additional transformations on demand. An example is extra
/// simplifications after loop-vectorization, if runtime checks have been added.
template <typename MarkerTy>
class ExtraFunctionPassManager
    : public RequiredPassInfoMixin<ExtraFunctionPassManager<MarkerTy>> {
  FunctionPassManager InnerFPM;

public:
  /// Add a pass to the set of extra function passes.
  /// @param Pass Pass to add to the inner function pass manager.
  template <typename PassT> void addPass(PassT &&Pass) {
    InnerFPM.addPass(std::move(Pass));
  }

  /// Run the extra function passes if the marker analysis is cached for F.
  /// @param F Function on which the extra passes may run.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    auto PA = PreservedAnalyses::all();
    if (AM.getCachedResult<MarkerTy>(F))
      PA.intersect(InnerFPM.run(F, AM));
    PA.abandon<MarkerTy>();
    return PA;
  }
};

/// A pass manager that runs extra loop passes when a marker analysis is present.
///
/// A pass manager to run a set of extra loop passes if the MarkerTy analysis is
/// present. This allows passes to request additional transformations on demand.
/// An example is doing additional runs of SimpleLoopUnswitch.
template <typename MarkerTy>
class ExtraLoopPassManager
    : public RequiredPassInfoMixin<ExtraLoopPassManager<MarkerTy>> {
  LoopPassManager InnerLPM;

public:
  /// Add a pass to the set of extra loop passes.
  /// @param Pass Pass to add to the inner loop pass manager.
  template <typename PassT> void addPass(PassT &&Pass) {
    InnerLPM.addPass(std::move(Pass));
  }

  /// Run the extra loop passes if the marker analysis is cached for L.
  /// @param L Loop on which the extra passes may run.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U) {
    auto PA = PreservedAnalyses::all();
    if (AM.getCachedResult<MarkerTy>(L))
      PA.intersect(InnerLPM.run(L, AM, AR, U));
    PA.abandon<MarkerTy>();
    return PA;
  }
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_EXTRAPASSMANAGER_H
