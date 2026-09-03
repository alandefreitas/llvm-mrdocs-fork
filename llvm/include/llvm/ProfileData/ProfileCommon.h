//===- ProfileCommon.h - Common profiling APIs. -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains data structures and functions common to both instrumented
// and sample profiling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_PROFILECOMMON_H
#define LLVM_PROFILEDATA_PROFILECOMMON_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace llvm {

/// Merge context-sensitive profiles before calculating summary thresholds.
LLVM_ABI extern cl::opt<bool> UseContextLessSummary;
/// Percentile cutoff (scaled by ProfileSummary::Scale) used to derive the hot
/// count threshold.
LLVM_ABI extern cl::opt<int> ProfileSummaryCutoffHot;
/// Percentile cutoff (scaled by ProfileSummary::Scale) used to derive the cold
/// count threshold.
LLVM_ABI extern cl::opt<int> ProfileSummaryCutoffCold;
/// Block-count threshold above which the hot working set is considered huge.
LLVM_ABI extern cl::opt<unsigned> ProfileSummaryHugeWorkingSetSizeThreshold;
/// Block-count threshold above which the hot working set is considered large.
LLVM_ABI extern cl::opt<unsigned> ProfileSummaryLargeWorkingSetSizeThreshold;
/// Fixed hot count that overrides the threshold derived from
/// ProfileSummaryCutoffHot.
LLVM_ABI extern cl::opt<uint64_t> ProfileSummaryHotCount;
/// Fixed cold count that overrides the threshold derived from
/// ProfileSummaryCutoffCold.
LLVM_ABI extern cl::opt<uint64_t> ProfileSummaryColdCount;

namespace sampleprof {

class FunctionSamples;

} // end namespace sampleprof

/// Base builder that accumulates profile counts and computes a ProfileSummary.
class ProfileSummaryBuilder {
private:
  /// We keep track of the number of times a count (block count or samples)
  /// appears in the profile. The map is kept sorted in the descending order of
  /// counts.
  std::map<uint64_t, uint32_t, std::greater<uint64_t>> CountFrequencies;
  std::vector<uint32_t> DetailedSummaryCutoffs;

protected:
  /// Detailed percentile summary entries computed from the accumulated counts.
  SummaryEntryVector DetailedSummary;
  /// Sum of all profile counts recorded so far.
  uint64_t TotalCount = 0;
  /// Maximum profile count seen so far.
  uint64_t MaxCount = 0;
  /// Maximum function entry count seen so far.
  uint64_t MaxFunctionCount = 0;
  /// Number of individual profile counts recorded so far.
  uint32_t NumCounts = 0;
  /// Number of functions recorded so far.
  uint32_t NumFunctions = 0;

  /// Construct a builder that will compute summary entries for \p Cutoffs.
  /// \param Cutoffs Percentile cutoffs (scaled by ProfileSummary::Scale) for
  /// the detailed summary.
  ProfileSummaryBuilder(std::vector<uint32_t> Cutoffs)
      : DetailedSummaryCutoffs(std::move(Cutoffs)) {}
  /// Destroy the profile summary builder.
  ~ProfileSummaryBuilder() = default;

  inline void addCount(uint64_t Count);
  /// Compute the detailed percentile summary from the accumulated counts.
  LLVM_ABI void computeDetailedSummary();

public:
  /// A vector of useful cutoff values for detailed summary.
  LLVM_ABI static const ArrayRef<uint32_t> DefaultCutoffs;

  /// Find the summary entry for a desired percentile of counts.
  /// \param DS Detailed summary entries to search.
  /// \param Percentile Desired percentile scaled by ProfileSummary::Scale.
  /// \return Reference to the ProfileSummaryEntry for \p Percentile.
  LLVM_ABI static const ProfileSummaryEntry &
  getEntryForPercentile(const SummaryEntryVector &DS, uint64_t Percentile);
  /// Return the minimum count treated as hot for the given detailed summary.
  /// \param DS Detailed summary entries used to derive the threshold.
  /// \return Minimum count at or above which a count is considered hot.
  LLVM_ABI static uint64_t getHotCountThreshold(const SummaryEntryVector &DS);
  /// Return the maximum count treated as cold for the given detailed summary.
  /// \param DS Detailed summary entries used to derive the threshold.
  /// \return Maximum count at or below which a count is considered cold.
  LLVM_ABI static uint64_t getColdCountThreshold(const SummaryEntryVector &DS);
};

/// Builder that constructs a ProfileSummary from instrumentation profile
/// records.
class InstrProfSummaryBuilder final : public ProfileSummaryBuilder {
  uint64_t MaxInternalBlockCount = 0;

public:
  /// Construct a builder that will compute summary entries for \p Cutoffs.
  /// \param Cutoffs Percentile cutoffs (scaled by ProfileSummary::Scale) for
  /// the detailed summary.
  InstrProfSummaryBuilder(std::vector<uint32_t> Cutoffs)
      : ProfileSummaryBuilder(std::move(Cutoffs)) {}

  /// Record a function entry count from an instrumentation profile.
  /// \param Count Function entry count to add.
  LLVM_ABI void addEntryCount(uint64_t Count);
  /// Record an internal (non-entry) block count from an instrumentation
  /// profile.
  /// \param Count Internal block count to add.
  LLVM_ABI void addInternalCount(uint64_t Count);

  /// Incorporate all counts from an instrumentation profile record.
  /// \param Record Instrumentation profile record whose counts are added.
  LLVM_ABI void addRecord(const InstrProfRecord &Record);
  /// Compute and return the instrumentation profile summary.
  /// \return Ownership of the computed ProfileSummary.
  LLVM_ABI std::unique_ptr<ProfileSummary> getSummary();
};

/// Builder that constructs a ProfileSummary from sample profile data.
class SampleProfileSummaryBuilder final : public ProfileSummaryBuilder {
public:
  /// Construct a builder that will compute summary entries for \p Cutoffs.
  /// \param Cutoffs Percentile cutoffs (scaled by ProfileSummary::Scale) for
  /// the detailed summary.
  SampleProfileSummaryBuilder(std::vector<uint32_t> Cutoffs)
      : ProfileSummaryBuilder(std::move(Cutoffs)) {}

  /// Incorporate sample counts from a function's sample profile.
  /// \param FS Function samples whose counts are added.
  /// \param isCallsiteSample Whether \p FS is a callsite (nested) sample rather
  /// than a top-level function.
  LLVM_ABI void addRecord(const sampleprof::FunctionSamples &FS,
                          bool isCallsiteSample = false);
  /// Compute a profile summary from a map of function sample profiles.
  /// \param Profiles Map of function samples to summarize.
  /// \return Ownership of the ProfileSummary computed from \p Profiles.
  LLVM_ABI std::unique_ptr<ProfileSummary>
  computeSummaryForProfiles(const sampleprof::SampleProfileMap &Profiles);
  /// Compute and return the sample profile summary from recorded samples.
  /// \return Ownership of the computed ProfileSummary.
  LLVM_ABI std::unique_ptr<ProfileSummary> getSummary();
};

/// This is called when a count is seen in the profile.
/// \param Count Profile count (block count or sample weight) to record.
void ProfileSummaryBuilder::addCount(uint64_t Count) {
  TotalCount += Count;
  if (Count > MaxCount)
    MaxCount = Count;
  NumCounts++;
  CountFrequencies[Count]++;
}

} // end namespace llvm

#endif // LLVM_PROFILEDATA_PROFILECOMMON_H
