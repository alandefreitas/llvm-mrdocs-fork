//===- llvm/Analysis/ProfileSummaryInfo.h - profile summary ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that provides access to profile summary
// information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_PROFILESUMMARYINFO_H
#define LLVM_ANALYSIS_PROFILESUMMARYINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/Pass.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/Compiler.h"
#include <memory>
#include <optional>

namespace llvm {
class BlockFrequencyInfo;
class MachineFunction;

/// Analysis providing profile information.
///
/// This is an immutable analysis pass that provides ability to query global
/// (program-level) profile information. The main APIs are isHotCount and
/// isColdCount that tells whether a given profile count is considered hot/cold
/// based on the profile summary. This also provides convenience methods to
/// check whether a function is hot or cold.

// FIXME: Provide convenience methods to determine hotness/coldness of other IR
// units. This would require making this depend on BFI.
class ProfileSummaryInfo {
private:
  const Module *M;
  std::unique_ptr<ProfileSummary> Summary;
  void computeThresholds();
  // Count thresholds to answer isHotCount and isColdCount queries.
  std::optional<uint64_t> HotCountThreshold, ColdCountThreshold;
  // True if the working set size of the code is considered huge,
  // because the number of profile counts required to reach the hot
  // percentile is above a huge threshold.
  std::optional<bool> HasHugeWorkingSetSize;
  // True if the working set size of the code is considered large,
  // because the number of profile counts required to reach the hot
  // percentile is above a large threshold.
  std::optional<bool> HasLargeWorkingSetSize;
  // Compute the threshold for a given cutoff.
  std::optional<uint64_t> computeThreshold(int PercentileCutoff) const;
  // The map that caches the threshold values. The keys are the percentile
  // cutoff values and the values are the corresponding threshold values.
  mutable DenseMap<int, uint64_t> ThresholdCache;

public:
  /// Construct profile summary info for module \p M.
  /// @param M Module whose profile summary is queried.
  ProfileSummaryInfo(const Module &M) : M(&M) { refresh(); }
  /// Move-construct from \p Arg.
  /// @param Arg ProfileSummaryInfo to move from.
  ProfileSummaryInfo(ProfileSummaryInfo &&Arg) = default;

  /// If a summary is provided as argument, use that. Otherwise,
  /// if the `Summary` member is null, attempt to refresh.
  /// @param Other Optional profile summary to adopt; null refreshes from the
  /// module metadata when needed.
  LLVM_ABI void refresh(std::unique_ptr<ProfileSummary> &&Other = nullptr);

  /// Returns true if profile summary is available.
  /// @return True if a profile summary is available.
  bool hasProfileSummary() const { return Summary != nullptr; }

  /// Returns true if module \c M has sample profile.
  /// @return True if the module has sample profile.
  bool hasSampleProfile() const {
    return hasProfileSummary() &&
           Summary->getKind() == ProfileSummary::PSK_Sample;
  }

  /// Returns true if module \c M has instrumentation profile.
  /// @return True if the module has instrumentation profile.
  bool hasInstrumentationProfile() const {
    return hasProfileSummary() &&
           Summary->getKind() == ProfileSummary::PSK_Instr;
  }

  /// Returns true if module \c M has context sensitive instrumentation profile.
  /// @return True if the module has context-sensitive instrumentation profile.
  bool hasCSInstrumentationProfile() const {
    return hasProfileSummary() &&
           Summary->getKind() == ProfileSummary::PSK_CSInstr;
  }

  /// Handle the invalidation of this information.
  ///
  /// When used as a result of \c ProfileSummaryAnalysis this method will be
  /// called when the module this was computed for changes. Since profile
  /// summary is immutable after it is annotated on the module, we return false
  /// here.
  /// @param M Module being invalidated (unused).
  /// @param PA Set of preserved analyses (unused).
  /// @param Inv Invalidator for dependent analyses (unused).
  /// @return False; profile summary is immutable after annotation.
  bool invalidate(Module &M, const PreservedAnalyses &PA,
                  ModuleAnalysisManager::Invalidator &Inv) {
    return false;
  }

  /// Returns the profile count for \p CallInst.
  /// @param CallInst Call site whose profile count is requested.
  /// @param BFI Optional block frequency info used when counts are scaled.
  /// @return Profile count for \p CallInst, or std::nullopt if unavailable.
  LLVM_ABI std::optional<uint64_t>
  getProfileCount(const CallBase &CallInst, BlockFrequencyInfo *BFI) const;
  /// Returns true if module \c M has partial-profile sample profile.
  /// @return True if the module has partial-profile sample profile.
  LLVM_ABI bool hasPartialSampleProfile() const;
  /// Returns true if the working set size of the code is considered huge.
  /// @return True if the working set size is considered huge.
  LLVM_ABI bool hasHugeWorkingSetSize() const;
  /// Returns true if the working set size of the code is considered large.
  /// @return True if the working set size is considered large.
  LLVM_ABI bool hasLargeWorkingSetSize() const;
  /// Returns true if \p F has a hot function entry.
  ///
  /// If it returns false, it either means it is not hot or it is unknown
  /// whether it is hot or not (for example, no profile data is available).
  /// @param F Function to query for entry hotness.
  /// @return True if \p F has a hot function entry.
  template <typename FuncT> bool isFunctionEntryHot(const FuncT *F) const {
    if (!F || !hasProfileSummary())
      return false;
    std::optional<uint64_t> FunctionCount = getEntryCount(F);
    // FIXME: The heuristic used below for determining hotness is based on
    // preliminary SPEC tuning for inliner. This will eventually be a
    // convenience method that calls isHotCount.
    return FunctionCount && isHotCount(*FunctionCount);
  }

  /// Returns true if \p F contains hot code.
  /// @param F Function whose call-graph hotness is queried.
  /// @param BFI Block frequency info for blocks in \p F.
  /// @return True if \p F contains hot code.
  template <typename FuncT, typename BFIT>
  bool isFunctionHotInCallGraph(const FuncT *F, BFIT &BFI) const {
    if (!F || !hasProfileSummary())
      return false;
    if (auto FunctionCount = getEntryCount(F))
      if (isHotCount(*FunctionCount))
        return true;

    if (auto TotalCallCount = getTotalCallCount(F))
      if (isHotCount(*TotalCallCount))
        return true;

    for (const auto &BB : *F)
      if (isHotBlock(&BB, &BFI))
        return true;
    return false;
  }
  /// Returns true if \p F has cold function entry.
  /// @param F Function to query for entry coldness.
  /// @return True if \p F has a cold function entry.
  LLVM_ABI bool isFunctionEntryCold(const Function *F) const;
  /// Returns true if \p F contains only cold code.
  /// @param F Function whose call-graph coldness is queried.
  /// @param BFI Block frequency info for blocks in \p F.
  /// @return True if \p F contains only cold code.
  template <typename FuncT, typename BFIT>
  bool isFunctionColdInCallGraph(const FuncT *F, BFIT &BFI) const {
    if (!F || !hasProfileSummary())
      return false;
    if (auto FunctionCount = getEntryCount(F))
      if (!isColdCount(*FunctionCount))
        return false;

    if (auto TotalCallCount = getTotalCallCount(F))
      if (!isColdCount(*TotalCallCount))
        return false;

    for (const auto &BB : *F)
      if (!isColdBlock(&BB, &BFI))
        return false;
    return true;
  }
  /// Returns true if the hotness of \p F is unknown.
  /// @param F Function whose hotness is queried.
  /// @return True if the hotness of \p F is unknown.
  LLVM_ABI bool isFunctionHotnessUnknown(const Function &F) const;
  /// Returns true if \p F contains hot code with regard to a given hot
  /// percentile cutoff value.
  /// @param PercentileCutoff Hot percentile cutoff as a 6-digit fixed point
  /// value.
  /// @param F Function whose call-graph hotness is queried.
  /// @param BFI Block frequency info for blocks in \p F.
  /// @return True if \p F contains hot code for \p PercentileCutoff.
  template <typename FuncT, typename BFIT>
  bool isFunctionHotInCallGraphNthPercentile(int PercentileCutoff,
                                             const FuncT *F, BFIT &BFI) const {
    return isFunctionHotOrColdInCallGraphNthPercentile<true, FuncT, BFIT>(
        PercentileCutoff, F, BFI);
  }
  /// Returns true if \p F contains cold code with regard to a given cold
  /// percentile cutoff value.
  /// @param PercentileCutoff Cold percentile cutoff as a 6-digit fixed point
  /// value.
  /// @param F Function whose call-graph coldness is queried.
  /// @param BFI Block frequency info for blocks in \p F.
  /// @return True if \p F contains cold code for \p PercentileCutoff.
  template <typename FuncT, typename BFIT>
  bool isFunctionColdInCallGraphNthPercentile(int PercentileCutoff,
                                              const FuncT *F, BFIT &BFI) const {
    return isFunctionHotOrColdInCallGraphNthPercentile<false, FuncT, BFIT>(
        PercentileCutoff, F, BFI);
  }
  /// Returns true if count \p C is considered hot.
  /// @param C Profile count to classify.
  /// @return True if \p C is considered hot.
  LLVM_ABI bool isHotCount(uint64_t C) const;
  /// Returns true if count \p C is considered cold.
  /// @param C Profile count to classify.
  /// @return True if \p C is considered cold.
  LLVM_ABI bool isColdCount(uint64_t C) const;
  /// Returns true if count \p C is considered hot for a percentile cutoff.
  ///
  /// PercentileCutoff is encoded as a 6 digit decimal fixed point number, where
  /// the first two digits are the whole part. E.g. 995000 for 99.5 percentile.
  /// @param PercentileCutoff Hot percentile cutoff as a 6-digit fixed point
  /// value.
  /// @param C Profile count to classify.
  /// @return True if \p C is hot for \p PercentileCutoff.
  LLVM_ABI bool isHotCountNthPercentile(int PercentileCutoff, uint64_t C) const;
  /// Returns true if count \p C is considered cold for a percentile cutoff.
  ///
  /// PercentileCutoff is encoded as a 6 digit decimal fixed point number, where
  /// the first two digits are the whole part. E.g. 995000 for 99.5 percentile.
  /// @param PercentileCutoff Cold percentile cutoff as a 6-digit fixed point
  /// value.
  /// @param C Profile count to classify.
  /// @return True if \p C is cold for \p PercentileCutoff.
  LLVM_ABI bool isColdCountNthPercentile(int PercentileCutoff,
                                         uint64_t C) const;

  /// Returns true if BasicBlock \p BB is considered hot.
  /// @param BB Basic block whose hotness is queried.
  /// @param BFI Block frequency info used to obtain the block profile count.
  /// @return True if \p BB is considered hot.
  template <typename BBType, typename BFIT>
  bool isHotBlock(const BBType *BB, BFIT *BFI) const {
    auto Count = BFI->getBlockProfileCount(BB);
    return Count && isHotCount(*Count);
  }

  /// Returns true if BasicBlock \p BB is considered cold.
  /// @param BB Basic block whose coldness is queried.
  /// @param BFI Block frequency info used to obtain the block profile count.
  /// @return True if \p BB is considered cold.
  template <typename BBType, typename BFIT>
  bool isColdBlock(const BBType *BB, BFIT *BFI) const {
    auto Count = BFI->getBlockProfileCount(BB);
    return Count && isColdCount(*Count);
  }

  /// Returns true if block frequency \p BlockFreq is considered cold.
  /// @param BlockFreq Relative block frequency to classify.
  /// @param BFI Block frequency info used to convert \p BlockFreq to a count.
  /// @return True if \p BlockFreq is considered cold.
  template <typename BFIT>
  bool isColdBlock(BlockFrequency BlockFreq, const BFIT *BFI) const {
    auto Count = BFI->getProfileCountFromFreq(BlockFreq);
    return Count && isColdCount(*Count);
  }

  /// Returns true if BasicBlock \p BB is considered hot for a percentile cutoff.
  /// @param PercentileCutoff Hot percentile cutoff as a 6-digit fixed point
  /// value.
  /// @param BB Basic block whose hotness is queried.
  /// @param BFI Block frequency info used to obtain the block profile count.
  /// @return True if \p BB is hot for \p PercentileCutoff.
  template <typename BBType, typename BFIT>
  bool isHotBlockNthPercentile(int PercentileCutoff, const BBType *BB,
                               BFIT *BFI) const {
    return isHotOrColdBlockNthPercentile<true, BBType, BFIT>(PercentileCutoff,
                                                             BB, BFI);
  }

  /// Returns true if block frequency \p BlockFreq is hot for a percentile
  /// cutoff.
  /// @param PercentileCutoff Hot percentile cutoff as a 6-digit fixed point
  /// value.
  /// @param BlockFreq Relative block frequency to classify.
  /// @param BFI Block frequency info used to convert \p BlockFreq to a count.
  /// @return True if \p BlockFreq is hot for \p PercentileCutoff.
  template <typename BFIT>
  bool isHotBlockNthPercentile(int PercentileCutoff, BlockFrequency BlockFreq,
                               BFIT *BFI) const {
    return isHotOrColdBlockNthPercentile<true, BFIT>(PercentileCutoff,
                                                     BlockFreq, BFI);
  }

  /// Returns true if BasicBlock \p BB is considered cold for a percentile
  /// cutoff.
  ///
  /// PercentileCutoff is encoded as a 6 digit decimal fixed point number, where
  /// the first two digits are the whole part. E.g. 995000 for 99.5 percentile.
  /// @param PercentileCutoff Cold percentile cutoff as a 6-digit fixed point
  /// value.
  /// @param BB Basic block whose coldness is queried.
  /// @param BFI Block frequency info used to obtain the block profile count.
  /// @return True if \p BB is cold for \p PercentileCutoff.
  template <typename BBType, typename BFIT>
  bool isColdBlockNthPercentile(int PercentileCutoff, const BBType *BB,
                                BFIT *BFI) const {
    return isHotOrColdBlockNthPercentile<false, BBType, BFIT>(PercentileCutoff,
                                                              BB, BFI);
  }
  /// Returns true if block frequency \p BlockFreq is cold for a percentile
  /// cutoff.
  /// @param PercentileCutoff Cold percentile cutoff as a 6-digit fixed point
  /// value.
  /// @param BlockFreq Relative block frequency to classify.
  /// @param BFI Block frequency info used to convert \p BlockFreq to a count.
  /// @return True if \p BlockFreq is cold for \p PercentileCutoff.
  template <typename BFIT>
  bool isColdBlockNthPercentile(int PercentileCutoff, BlockFrequency BlockFreq,
                                BFIT *BFI) const {
    return isHotOrColdBlockNthPercentile<false, BFIT>(PercentileCutoff,
                                                      BlockFreq, BFI);
  }
  /// Returns true if the call site \p CB is considered hot.
  /// @param CB Call site whose hotness is queried.
  /// @param BFI Optional block frequency info used when counts are scaled.
  /// @return True if \p CB is considered hot.
  LLVM_ABI bool isHotCallSite(const CallBase &CB,
                              BlockFrequencyInfo *BFI) const;
  /// Returns true if call site \p CB is considered cold.
  /// @param CB Call site whose coldness is queried.
  /// @param BFI Optional block frequency info used when counts are scaled.
  /// @return True if \p CB is considered cold.
  LLVM_ABI bool isColdCallSite(const CallBase &CB,
                               BlockFrequencyInfo *BFI) const;
  /// Returns HotCountThreshold if set. Recompute HotCountThreshold
  /// if not set.
  /// @return Hot count threshold, computing it if needed.
  LLVM_ABI uint64_t getOrCompHotCountThreshold() const;
  /// Returns ColdCountThreshold if set. Recompute HotCountThreshold
  /// if not set.
  /// @return Cold count threshold, computing it if needed.
  LLVM_ABI uint64_t getOrCompColdCountThreshold() const;
  /// Returns HotCountThreshold if set.
  /// @return Hot count threshold, or 0 if unset.
  uint64_t getHotCountThreshold() const {
    return HotCountThreshold.value_or(0);
  }
  /// Returns ColdCountThreshold if set.
  /// @return Cold count threshold, or 0 if unset.
  uint64_t getColdCountThreshold() const {
    return ColdCountThreshold.value_or(0);
  }

private:
  template <typename FuncT>
  std::optional<uint64_t> getTotalCallCount(const FuncT *F) const {
    return std::nullopt;
  }

  template <bool isHot, typename FuncT, typename BFIT>
  bool isFunctionHotOrColdInCallGraphNthPercentile(int PercentileCutoff,
                                                   const FuncT *F,
                                                   BFIT &FI) const {
    if (!F || !hasProfileSummary())
      return false;
    if (auto FunctionCount = getEntryCount(F)) {
      if (isHot && isHotCountNthPercentile(PercentileCutoff, *FunctionCount))
        return true;
      if (!isHot && !isColdCountNthPercentile(PercentileCutoff, *FunctionCount))
        return false;
    }
    if (auto TotalCallCount = getTotalCallCount(F)) {
      if (isHot && isHotCountNthPercentile(PercentileCutoff, *TotalCallCount))
        return true;
      if (!isHot &&
          !isColdCountNthPercentile(PercentileCutoff, *TotalCallCount))
        return false;
    }
    for (const auto &BB : *F) {
      if (isHot && isHotBlockNthPercentile(PercentileCutoff, &BB, &FI))
        return true;
      if (!isHot && !isColdBlockNthPercentile(PercentileCutoff, &BB, &FI))
        return false;
    }
    return !isHot;
  }

  template <bool isHot>
  bool isHotOrColdCountNthPercentile(int PercentileCutoff, uint64_t C) const;

  template <bool isHot, typename BBType, typename BFIT>
  bool isHotOrColdBlockNthPercentile(int PercentileCutoff, const BBType *BB,
                                     BFIT *BFI) const {
    auto Count = BFI->getBlockProfileCount(BB);
    if constexpr (isHot)
      return Count && isHotCountNthPercentile(PercentileCutoff, *Count);
    else
      return Count && isColdCountNthPercentile(PercentileCutoff, *Count);
  }

  template <bool isHot, typename BFIT>
  bool isHotOrColdBlockNthPercentile(int PercentileCutoff,
                                     BlockFrequency BlockFreq,
                                     BFIT *BFI) const {
    auto Count = BFI->getProfileCountFromFreq(BlockFreq);
    if constexpr (isHot)
      return Count && isHotCountNthPercentile(PercentileCutoff, *Count);
    else
      return Count && isColdCountNthPercentile(PercentileCutoff, *Count);
  }

  template <typename FuncT>
  std::optional<uint64_t> getEntryCount(const FuncT *F) const {
    return F->getEntryCount();
  }
};

template <>
inline std::optional<uint64_t>
ProfileSummaryInfo::getTotalCallCount<Function>(const Function *F) const {
  if (!hasSampleProfile())
    return std::nullopt;
  uint64_t TotalCallCount = 0;
  for (const auto &BB : *F)
    for (const auto &I : BB)
      if (isa<CallInst>(I) || isa<InvokeInst>(I))
        if (auto CallCount = getProfileCount(cast<CallBase>(I), nullptr))
          TotalCallCount += *CallCount;
  return TotalCallCount;
}

// Declare template specialization for llvm::MachineFunction. Do not implement
// here, because we cannot include MachineFunction header here, that would break
// dependency rules.
template <>
std::optional<uint64_t> ProfileSummaryInfo::getEntryCount<MachineFunction>(
    const MachineFunction *F) const;

/// An analysis pass based on legacy pass manager to deliver ProfileSummaryInfo.
class LLVM_ABI ProfileSummaryInfoWrapperPass : public ImmutablePass {
  std::unique_ptr<ProfileSummaryInfo> PSI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the legacy profile summary info wrapper pass.
  ProfileSummaryInfoWrapperPass();

  /// Return the cached ProfileSummaryInfo.
  /// @return Cached ProfileSummaryInfo for the module.
  ProfileSummaryInfo &getPSI() { return *PSI; }
  /// Return the cached ProfileSummaryInfo.
  /// @return Cached ProfileSummaryInfo for the module.
  const ProfileSummaryInfo &getPSI() const { return *PSI; }

  /// Build ProfileSummaryInfo for module \p M.
  /// @param M Module to analyze.
  /// @return False; this analysis does not modify the module.
  bool doInitialization(Module &M) override;
  /// Release the cached ProfileSummaryInfo after the module is processed.
  /// @param M Module whose analysis state is being finalized.
  /// @return False; this pass does not modify the module.
  bool doFinalization(Module &M) override;
  /// Declare required and preserved analyses for this pass.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

/// An analysis pass based on the new PM to deliver ProfileSummaryInfo.
class ProfileSummaryAnalysis
    : public AnalysisInfoMixin<ProfileSummaryAnalysis> {
public:
  /// Provide the result type for this analysis pass.
  typedef ProfileSummaryInfo Result;

  /// Compute ProfileSummaryInfo for module \p M.
  /// @param M Module to analyze.
  /// @param AM Module analysis manager providing dependencies.
  /// @return ProfileSummaryInfo for \p M.
  LLVM_ABI Result run(Module &M, ModuleAnalysisManager &AM);

private:
  friend AnalysisInfoMixin<ProfileSummaryAnalysis>;
  LLVM_ABI static AnalysisKey Key;
};

/// Printer pass that uses \c ProfileSummaryAnalysis.
class ProfileSummaryPrinterPass
    : public RequiredPassInfoMixin<ProfileSummaryPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes profile summary info to \p OS.
  /// @param OS Output stream that receives the printed summary.
  explicit ProfileSummaryPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print profile summary information for module \p M.
  /// @param M Module whose profile summary is printed.
  /// @param AM Module analysis manager providing ProfileSummaryAnalysis.
  /// @return All analyses are preserved; this pass is read-only.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif
