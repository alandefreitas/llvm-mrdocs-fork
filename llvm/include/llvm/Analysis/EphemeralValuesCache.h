//===- llvm/Analysis/EphemeralValuesCache.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass caches ephemeral values, i.e., values that are only used by
// @llvm.assume intrinsics, for cheap access after the initial collection.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_EPHEMERALVALUESCACHE_H
#define LLVM_ANALYSIS_EPHEMERALVALUESCACHE_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Function;
class AssumptionCache;
class Value;

/// A cache of ephemeral values within a function.
class EphemeralValuesCache {
  SmallPtrSet<const Value *, 32> EphValues;
  Function &F;
  AssumptionCache &AC;
  bool Collected = false;

  LLVM_ABI void collectEphemeralValues();

public:
  /// Construct an ephemeral-values cache for \p F using \p AC.
  /// @param F Function whose ephemeral values are cached.
  /// @param AC Assumption cache used to identify ephemeral values.
  EphemeralValuesCache(Function &F, AssumptionCache &AC) : F(F), AC(AC) {}

  /// Clear the cached ephemeral values for the function.
  ///
  /// They will be re-collected the next time they are requested.
  void clear() {
    EphValues.clear();
    Collected = false;
  }

  /// Access the set of ephemeral values currently tracked for this function.
  ///
  /// Collects them lazily on the first call after construction or \c clear().
  /// @return Set of ephemeral values for this function.
  const SmallPtrSetImpl<const Value *> &ephValues() {
    if (!Collected)
      collectEphemeralValues();
    return EphValues;
  }
};

/// A function analysis which provides an \c EphemeralValuesCache.
///
/// This analysis is intended for use with the new pass manager and will vend
/// ephemeral-values caches for a given function.
class EphemeralValuesAnalysis
    : public AnalysisInfoMixin<EphemeralValuesAnalysis> {
  friend AnalysisInfoMixin<EphemeralValuesAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// The analysis result type; an EphemeralValuesCache for a function.
  using Result = EphemeralValuesCache;

  /// Run the ephemeral-values analysis on function \p F.
  /// @param F Function to analyze.
  /// @param FAM Function analysis manager providing dependencies.
  /// @return EphemeralValuesCache for \p F.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_ANALYSIS_EPHEMERALVALUESCACHE_H
