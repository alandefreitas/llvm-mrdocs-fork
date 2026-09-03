//===- LoopRotationUtils.h - Utilities to perform loop rotation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides utilities to convert a loop into a loop with bottom test.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOOPROTATIONUTILS_H
#define LLVM_TRANSFORMS_UTILS_LOOPROTATIONUTILS_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class AssumptionCache;
class DominatorTree;
class Loop;
class LoopInfo;
class MemorySSAUpdater;
class ScalarEvolution;
struct SimplifyQuery;
class TargetTransformInfo;

/// Convert a loop into a loop with bottom test.
///
/// It may perform loop latch simplification as well if the flag RotationOnly
/// is false. The flag Threshold represents the size threshold of the loop
/// header. If the loop header's size exceeds the threshold, the loop rotation
/// will give up. The flag IsUtilMode controls the heuristic used in the
/// LoopRotation. If it is true, the profitability heuristic will be ignored.
///
/// \param L Loop to rotate.
/// \param LI LoopInfo to keep consistent with the transformed CFG.
/// \param TTI Target transform info used for header size metrics.
/// \param AC Assumption cache used when collecting ephemeral values.
/// \param DT Dominator tree to update.
/// \param SE ScalarEvolution analysis to invalidate and optionally consult.
/// \param MSSAU MemorySSA updater, or nullptr.
/// \param SQ Simplify query used during latch simplification.
/// \param RotationOnly If true, only rotate; if false, also simplify the latch.
/// \param Threshold Maximum allowed size of the loop header; rotation is
///        skipped when the header exceeds this threshold.
/// \param IsUtilMode If true, ignore the profitability heuristic.
/// \param PrepareForLTO If true, avoid rotating loops with inlining candidates
///        that may be handled during LTO.
/// \param CheckExitCount If true, consider ScalarEvolution exit-count info when
///        deciding whether rotating an already-exiting latch is profitable.
/// \return True if the loop was modified.
LLVM_ABI bool LoopRotation(Loop *L, LoopInfo *LI,
                           const TargetTransformInfo *TTI, AssumptionCache *AC,
                           DominatorTree *DT, ScalarEvolution *SE,
                           MemorySSAUpdater *MSSAU, const SimplifyQuery &SQ,
                           bool RotationOnly, unsigned Threshold,
                           bool IsUtilMode, bool PrepareForLTO = false,
                           bool CheckExitCount = false);

} // namespace llvm

#endif
