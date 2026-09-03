//===- llvm/Transforms/Utils/LoopPeel.h ----- Peeling utilities -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some loop peeling utilities. It does not define any
// actual pass or policy.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOOPPEEL_H
#define LLVM_TRANSFORMS_UTILS_LOOPPEEL_H

#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

namespace llvm {

/// Return true if loop \p L is suitable for peeling.
///
/// \param L Loop to test for peelability.
/// \return True if \p L can be peeled.
LLVM_ABI bool canPeel(const Loop *L);

/// Return true if the last iteration of \p L can be peeled off.
///
/// It makes sure the loop exit condition can be adjusted when peeling and that
/// the loop executes at least 2 iterations.
///
/// \param L Loop whose last iteration may be peeled.
/// \param SE ScalarEvolution used to analyze the exit condition.
/// \return True if the last iteration of \p L can be peeled.
LLVM_ABI bool canPeelLastIteration(const Loop &L, ScalarEvolution &SE);

/// Peel off \p PeelCount iterations from loop \p L.
///
/// \p VMap is the value-map that maps instructions from the original loop to
/// instructions in the last peeled-off iteration. If \p PeelLast is true, peel
/// off the last \p PeelCount iterations from \p L (canPeelLastIteration must be
/// true for \p L), otherwise peel off the first \p PeelCount iterations.
///
/// \param L Loop to peel.
/// \param PeelCount Number of iterations to peel off.
/// \param PeelLast If true, peel the last iterations; otherwise peel the first.
/// \param LI LoopInfo to keep consistent with the transformed CFG.
/// \param SE ScalarEvolution used when peeling the last iteration.
/// \param DT Dominator tree to update.
/// \param AC Assumption cache used by simplifications.
/// \param PreserveLCSSA Whether to preserve LCSSA form after peeling.
/// \param VMap Receives a mapping from original instructions to those in the
///        last peeled-off iteration.
LLVM_ABI void peelLoop(Loop *L, unsigned PeelCount, bool PeelLast, LoopInfo *LI,
                       ScalarEvolution *SE, DominatorTree &DT,
                       AssumptionCache *AC, bool PreserveLCSSA,
                       ValueToValueMapTy &VMap);

/// Gather peeling preferences for loop \p L from the target and user overrides.
///
/// \param L Loop for which peeling preferences are gathered.
/// \param SE ScalarEvolution analysis for \p L.
/// \param TTI Target transform info providing target-specific defaults.
/// \param UserAllowPeeling Optional override for whether peeling is allowed.
/// \param UserAllowProfileBasedPeeling Optional override for profile-based
///        peeling.
/// \param UnrollingSpecficValues If true, apply unroll-related command-line
///        overrides to the preferences.
/// \return Combined peeling preferences for \p L.
LLVM_ABI TargetTransformInfo::PeelingPreferences
gatherPeelingPreferences(Loop *L, ScalarEvolution &SE,
                         const TargetTransformInfo &TTI,
                         std::optional<bool> UserAllowPeeling,
                         std::optional<bool> UserAllowProfileBasedPeeling,
                         bool UnrollingSpecficValues = false);

/// Compute a profitable peel count for loop \p L and store it in \p PP.
///
/// \param L Loop for which to compute a peel count.
/// \param LoopSize Estimated size of the loop body.
/// \param PP Peeling preferences updated with the chosen peel count and flags.
/// \param TripCount Known trip count of \p L, or 0 if unknown.
/// \param DT Dominator tree used by profitability analysis.
/// \param SE ScalarEvolution used by profitability analysis.
/// \param TTI Target transform info used for cost checks.
/// \param AC Optional assumption cache used by simplifications.
/// \param Threshold Maximum allowed size for the peeled loop.
LLVM_ABI void computePeelCount(Loop *L, unsigned LoopSize,
                               TargetTransformInfo::PeelingPreferences &PP,
                               unsigned TripCount, DominatorTree &DT,
                               ScalarEvolution &SE,
                               const TargetTransformInfo &TTI,
                               AssumptionCache *AC = nullptr,
                               unsigned Threshold = UINT_MAX);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOOPPEEL_H
