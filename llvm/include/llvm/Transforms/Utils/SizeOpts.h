//===- llvm/Transforms/Utils/SizeOpts.h - size optimization -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains some shared code size optimization related code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SIZEOPTS_H
#define LLVM_TRANSFORMS_UTILS_SIZEOPTS_H

#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
/// Enable the profile-guided size optimizations.
LLVM_ABI extern cl::opt<bool> EnablePGSO;
/// Apply PGSO only when the working set size is large (except for cold code).
LLVM_ABI extern cl::opt<bool> PGSOLargeWorkingSetSizeOnly;
/// Apply the profile-guided size optimizations only to cold code.
LLVM_ABI extern cl::opt<bool> PGSOColdCodeOnly;
/// Apply PGSO only to cold code under instrumentation PGO.
LLVM_ABI extern cl::opt<bool> PGSOColdCodeOnlyForInstrPGO;
/// Apply PGSO only to cold code under sample PGO.
LLVM_ABI extern cl::opt<bool> PGSOColdCodeOnlyForSamplePGO;
/// Apply PGSO only to cold code under partial-profile sample PGO.
LLVM_ABI extern cl::opt<bool> PGSOColdCodeOnlyForPartialSamplePGO;
/// Force the profile-guided size optimizations regardless of profile data.
LLVM_ABI extern cl::opt<bool> ForcePGSO;
/// Profile summary cutoff for PGSO with instrumentation profiles.
LLVM_ABI extern cl::opt<int> PgsoCutoffInstrProf;
/// Profile summary cutoff for PGSO with sample profiles.
LLVM_ABI extern cl::opt<int> PgsoCutoffSampleProf;

class BasicBlock;
class BlockFrequencyInfo;
class Function;

/// Identifies the caller context of a profile-guided size optimization query.
enum class PGSOQueryType {
  /// A query call from an IR-level transform pass.
  IRPass,
  /// A query call from a unit test.
  Test,
  /// A query call from any other caller.
  Other,
};

static inline bool isPGSOColdCodeOnly(ProfileSummaryInfo *PSI) {
  return PGSOColdCodeOnly ||
         (PSI->hasInstrumentationProfile() && PGSOColdCodeOnlyForInstrPGO) ||
         (PSI->hasSampleProfile() &&
          ((!PSI->hasPartialSampleProfile() && PGSOColdCodeOnlyForSamplePGO) ||
           (PSI->hasPartialSampleProfile() &&
            PGSOColdCodeOnlyForPartialSamplePGO))) ||
         (PGSOLargeWorkingSetSizeOnly && !PSI->hasLargeWorkingSetSize());
}

/// Returns true if function \p F is suggested to be size-optimized based on the
/// profile.
///
/// \param F The function to consider for size optimization.
/// \param PSI Profile summary information used to classify hot/cold code.
/// \param BFI Block frequency information for \p F.
/// \param QueryType The caller context of this size-optimization query.
/// \return True if \p F should be optimized for size.
template <typename FuncT, typename BFIT>
bool shouldFuncOptimizeForSizeImpl(const FuncT *F, ProfileSummaryInfo *PSI,
                                   BFIT *BFI, PGSOQueryType QueryType) {
  assert(F);
  if (!PSI || !BFI || !PSI->hasProfileSummary())
    return false;
  if (ForcePGSO)
    return true;
  if (!EnablePGSO)
    return false;
  if (isPGSOColdCodeOnly(PSI))
    return PSI->isFunctionColdInCallGraph(F, *BFI);
  if (PSI->hasSampleProfile())
    // The "isCold" check seems to work better for Sample PGO as it could have
    // many profile-unannotated functions.
    return PSI->isFunctionColdInCallGraphNthPercentile(PgsoCutoffSampleProf, F,
                                                       *BFI);
  return !PSI->isFunctionHotInCallGraphNthPercentile(PgsoCutoffInstrProf, F,
                                                     *BFI);
}

/// Returns true if a basic block (or its frequency) is suggested to be
/// size-optimized based on the profile.
///
/// \param BBOrBlockFreq The basic block or block frequency to consider.
/// \param PSI Profile summary information used to classify hot/cold code.
/// \param BFI Block frequency information for the enclosing function.
/// \param QueryType The caller context of this size-optimization query.
/// \return True if the block should be optimized for size.
template <typename BlockTOrBlockFreq, typename BFIT>
bool shouldOptimizeForSizeImpl(BlockTOrBlockFreq BBOrBlockFreq,
                               ProfileSummaryInfo *PSI, BFIT *BFI,
                               PGSOQueryType QueryType) {
  if (!PSI || !BFI || !PSI->hasProfileSummary())
    return false;
  if (ForcePGSO)
    return true;
  if (!EnablePGSO)
    return false;
  if (isPGSOColdCodeOnly(PSI))
    return PSI->isColdBlock(BBOrBlockFreq, BFI);
  if (PSI->hasSampleProfile())
    // The "isCold" check seems to work better for Sample PGO as it could have
    // many profile-unannotated functions.
    return PSI->isColdBlockNthPercentile(PgsoCutoffSampleProf, BBOrBlockFreq,
                                         BFI);
  return !PSI->isHotBlockNthPercentile(PgsoCutoffInstrProf, BBOrBlockFreq, BFI);
}

/// Returns true if function \p F is suggested to be size-optimized based on the
/// profile.
///
/// \param F The function to consider for size optimization.
/// \param PSI Profile summary information used to classify hot/cold code.
/// \param BFI Block frequency information for \p F.
/// \param QueryType The caller context of this size-optimization query.
/// \return True if \p F should be optimized for size.
LLVM_ABI bool
shouldOptimizeForSize(const Function *F, ProfileSummaryInfo *PSI,
                      BlockFrequencyInfo *BFI,
                      PGSOQueryType QueryType = PGSOQueryType::Other);

/// Returns true if basic block \p BB is suggested to be size-optimized based on
/// the profile.
///
/// \param BB The basic block to consider for size optimization.
/// \param PSI Profile summary information used to classify hot/cold code.
/// \param BFI Block frequency information for the enclosing function.
/// \param QueryType The caller context of this size-optimization query.
/// \return True if \p BB should be optimized for size.
LLVM_ABI bool
shouldOptimizeForSize(const BasicBlock *BB, ProfileSummaryInfo *PSI,
                      BlockFrequencyInfo *BFI,
                      PGSOQueryType QueryType = PGSOQueryType::Other);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SIZEOPTS_H
