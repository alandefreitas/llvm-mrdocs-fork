//===- ProfileSummary.h - Profile summary data structure. -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the profile summary data structure.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PROFILESUMMARY_H
#define LLVM_IR_PROFILESUMMARY_H

#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <vector>

namespace llvm {

class LLVMContext;
class Metadata;
class raw_ostream;

/// One percentile cut-point entry in a profile summary.
///
/// The profile summary is one or more (Cutoff, MinCount, NumCounts) triplets.
/// The semantics of counts depend on the type of profile. For instrumentation
/// profile, counts are block counts and for sample profile, counts are
/// per-line samples. Given a target counts percentile, we compute the minimum
/// number of counts needed to reach this target and the minimum among these
/// counts.
struct ProfileSummaryEntry {
  const uint32_t Cutoff;    ///< The required percentile of counts.
  const uint64_t MinCount;  ///< The minimum count for this percentile.
  const uint64_t NumCounts; ///< Number of counts >= the minimum count.

  /// Construct a summary entry for the given cut-point.
  /// \param TheCutoff Required percentile of counts (scaled).
  /// \param TheMinCount Minimum count at this percentile.
  /// \param TheNumCounts Number of counts greater than or equal to the minimum.
  ProfileSummaryEntry(uint32_t TheCutoff, uint64_t TheMinCount,
                      uint64_t TheNumCounts)
      : Cutoff(TheCutoff), MinCount(TheMinCount), NumCounts(TheNumCounts) {}
};

/// Vector of profile summary percentile entries.
using SummaryEntryVector = std::vector<ProfileSummaryEntry>;

/// Aggregated profile summary statistics for a module or compilation unit.
class ProfileSummary {
public:
  /// Kind of profile data summarized by this object.
  enum Kind {
    /// Instrumentation (PGO) profile summary.
    PSK_Instr,
    /// Context-sensitive instrumentation profile summary.
    PSK_CSInstr,
    /// Sample-based profile summary.
    PSK_Sample
  };

private:
  const Kind PSK;
  const SummaryEntryVector DetailedSummary;
  const uint64_t TotalCount, MaxCount, MaxInternalCount, MaxFunctionCount;
  const uint32_t NumCounts, NumFunctions;
  /// If 'Partial' is false, it means the profile being used to optimize
  /// a target is collected from the same target.
  /// If 'Partial' is true, it means the profile is for common/shared
  /// code. The common profile is usually merged from profiles collected
  /// from running other targets.
  bool Partial = false;
  /// This approximately represents the ratio of the number of profile counters
  /// of the program being built to the number of profile counters in the
  /// partial sample profile. When 'Partial' is false, it is undefined. This is
  /// currently only available under thin LTO mode.
  double PartialProfileRatio = 0.0;
  /// Return detailed summary as metadata.
  Metadata *getDetailedSummaryMD(LLVMContext &Context);

public:
  /// Scale factor used when encoding percentile cutoffs as integers.
  static const int Scale = 1000000;

  /// Construct a profile summary from the given statistics and cut-points.
  /// \param K Kind of profile summarized.
  /// \param DetailedSummary Percentile cut-point entries.
  /// \param TotalCount Sum of all recorded counts.
  /// \param MaxCount Maximum count across the profile.
  /// \param MaxInternalCount Maximum non-entry count.
  /// \param MaxFunctionCount Maximum function entry count.
  /// \param NumCounts Number of distinct count sites.
  /// \param NumFunctions Number of functions with counts.
  /// \param Partial Whether the profile covers only part of the program.
  /// \param PartialProfileRatio Ratio of program counters to partial-profile
  /// counters when \p Partial is true.
  ProfileSummary(Kind K, const SummaryEntryVector &DetailedSummary,
                 uint64_t TotalCount, uint64_t MaxCount,
                 uint64_t MaxInternalCount, uint64_t MaxFunctionCount,
                 uint32_t NumCounts, uint32_t NumFunctions,
                 bool Partial = false, double PartialProfileRatio = 0)
      : PSK(K), DetailedSummary(DetailedSummary), TotalCount(TotalCount),
        MaxCount(MaxCount), MaxInternalCount(MaxInternalCount),
        MaxFunctionCount(MaxFunctionCount), NumCounts(NumCounts),
        NumFunctions(NumFunctions), Partial(Partial),
        PartialProfileRatio(PartialProfileRatio) {}

  /// Return the kind of profile summarized by this object.
  /// \return The kind of profile summarized by this object.
  Kind getKind() const { return PSK; }
  /// Return summary information as metadata.
  /// \param Context LLVM context used to create metadata nodes.
  /// \param AddPartialField Whether to include the partial-profile flag.
  /// \param AddPartialProfileRatioField Whether to include the partial profile
  /// ratio.
  /// \return Metadata encoding this profile summary.
  LLVM_ABI Metadata *getMD(LLVMContext &Context, bool AddPartialField = true,
                           bool AddPartialProfileRatioField = true);
  /// Construct profile summary from metadata.
  /// \param MD Metadata node encoding a profile summary.
  /// \return Newly constructed profile summary, or null if \p MD is invalid.
  LLVM_ABI static ProfileSummary *getFromMD(Metadata *MD);
  /// Return the detailed cut-point summary entries.
  /// \return The detailed cut-point summary entries.
  const SummaryEntryVector &getDetailedSummary() { return DetailedSummary; }
  /// Return the number of functions that have profile counts.
  /// \return The number of functions that have profile counts.
  uint32_t getNumFunctions() const { return NumFunctions; }
  /// Return the maximum function entry count in the profile.
  /// \return The maximum function entry count in the profile.
  uint64_t getMaxFunctionCount() const { return MaxFunctionCount; }
  /// Return the number of distinct count sites in the profile.
  /// \return The number of distinct count sites in the profile.
  uint32_t getNumCounts() const { return NumCounts; }
  /// Return the total profile count across all recorded functions.
  /// \return The total profile count across all recorded functions.
  uint64_t getTotalCount() const { return TotalCount; }
  /// Return the maximum count across all sites in the profile.
  /// \return The maximum count across all sites in the profile.
  uint64_t getMaxCount() const { return MaxCount; }
  /// Return the maximum non-entry (internal) count in the profile.
  /// \return The maximum non-entry (internal) count in the profile.
  uint64_t getMaxInternalCount() const { return MaxInternalCount; }
  /// Mark whether this summary describes a partial profile.
  /// \param PP True if the profile covers only part of the program.
  void setPartialProfile(bool PP) { Partial = PP; }
  /// Return true if this summary describes a partial profile.
  /// \return True if this summary describes a partial profile.
  bool isPartialProfile() const { return Partial; }
  /// Return the ratio of program counters to counters in the partial profile.
  /// \return The ratio of program counters to counters in the partial profile.
  double getPartialProfileRatio() const { return PartialProfileRatio; }
  /// Set the ratio of profile counters in the built program to those in the
  /// partial profile. Requires \c isPartialProfile().
  /// \param R Ratio of program counters to partial-profile counters.
  void setPartialProfileRatio(double R) {
    assert(isPartialProfile() && "Unexpected when not partial profile");
    PartialProfileRatio = R;
  }
  /// Print a high-level summary of the profile to \p OS.
  /// \param OS Stream to write the summary to.
  LLVM_ABI void printSummary(raw_ostream &OS) const;
  /// Print the detailed percentile cut-point summary to \p OS.
  /// \param OS Stream to write the detailed summary to.
  LLVM_ABI void printDetailedSummary(raw_ostream &OS) const;
};

} // end namespace llvm

#endif // LLVM_IR_PROFILESUMMARY_H
