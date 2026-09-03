////===- SampleProfileLoadBaseUtil.h - Profile loader util func --*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the utility functions for the sampled PGO loader base
/// implementation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SAMPLEPROFILELOADERBASEUTIL_H
#define LLVM_TRANSFORMS_UTILS_SAMPLEPROFILELOADERBASEUTIL_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {
using namespace sampleprof;

class ProfileSummaryInfo;
class Module;

/// Command-line option for the maximum number of weight-propagation iterations.
extern LLVM_ABI cl::opt<unsigned> SampleProfileMaxPropagateIterations;
/// Command-line option for the minimum matched-record coverage percentage.
extern LLVM_ABI cl::opt<unsigned> SampleProfileRecordCoverage;
/// Command-line option for the minimum matched-sample coverage percentage.
extern LLVM_ABI cl::opt<unsigned> SampleProfileSampleCoverage;
/// Command-line option that suppresses warnings about unused sample profiles.
extern LLVM_ABI cl::opt<bool> NoWarnSampleUnused;

/// Utilities shared by sample profile loader implementations.
namespace sampleprofutil {

/// Tracks which sample-profile records have been matched to IR instructions.
class SampleCoverageTracker {
public:
  /// Mark the sample record at \p LineOffset / \p Discriminator as used.
  ///
  /// \param FS Function samples containing the record.
  /// \param LineOffset Line offset of the sample record within \p FS.
  /// \param Discriminator Discriminator of the sample record.
  /// \param Samples Sample count to accumulate on first use of the record.
  /// \returns true if this is the first time the given record is marked used.
  LLVM_ABI bool markSamplesUsed(const FunctionSamples *FS, uint32_t LineOffset,
                                uint32_t Discriminator, uint64_t Samples);
  /// Return the percentage of sample records used relative to \p Total.
  ///
  /// \param Used Number of sample records that were applied.
  /// \param Total Total number of sample records available.
  /// \returns An integer percentage in the range 0-100.
  LLVM_ABI unsigned computeCoverage(unsigned Used, unsigned Total) const;
  /// Return the number of sample records applied from \p FS.
  ///
  /// This count does not include records from cold inlined callsites.
  ///
  /// \param FS Function samples whose used records are counted.
  /// \param PSI Profile summary used to decide which callsites are hot.
  /// \returns The number of used sample records from \p FS.
  LLVM_ABI unsigned countUsedRecords(const FunctionSamples *FS,
                                     ProfileSummaryInfo *PSI) const;
  /// Return the number of sample records in the body of \p FS.
  ///
  /// This count does not include records from cold inlined callsites.
  ///
  /// \param FS Function samples whose body records are counted.
  /// \param PSI Profile summary used to decide which callsites are hot.
  /// \returns The number of body sample records in \p FS.
  LLVM_ABI unsigned countBodyRecords(const FunctionSamples *FS,
                                     ProfileSummaryInfo *PSI) const;
  /// Return the total number of samples marked used so far.
  ///
  /// \returns The accumulated sample count from records marked used.
  uint64_t getTotalUsedSamples() const { return TotalUsedSamples; }
  /// Return the number of samples collected in the body of \p FS.
  ///
  /// This count does not include samples from cold inlined callsites.
  ///
  /// \param FS Function samples whose body samples are counted.
  /// \param PSI Profile summary used to decide which callsites are hot.
  /// \returns The total sample count in the body of \p FS.
  LLVM_ABI uint64_t countBodySamples(const FunctionSamples *FS,
                                     ProfileSummaryInfo *PSI) const;

  /// Clear coverage maps and the used-sample accumulator.
  void clear() {
    SampleCoverage.clear();
    TotalUsedSamples = 0;
  }
  /// Set whether profiles for symbols in the profile symbol list are accurate.
  ///
  /// \param V True if symbols in the profile symbol list should be treated as
  /// having accurate profiles.
  void setProfAccForSymsInList(bool V) { ProfAccForSymsInList = V; }

private:
  using BodySampleCoverageMap = std::map<LineLocation, unsigned>;
  using FunctionSamplesCoverageMap =
      DenseMap<const FunctionSamples *, BodySampleCoverageMap>;

  /// Coverage map for sampling records.
  ///
  /// This map keeps a record of sampling records that have been matched to
  /// an IR instruction. This is used to detect some form of staleness in
  /// profiles (see flag -sample-profile-check-coverage).
  ///
  /// Each entry in the map corresponds to a FunctionSamples instance.  This is
  /// another map that counts how many times the sample record at the
  /// given location has been used.
  FunctionSamplesCoverageMap SampleCoverage;

  /// Number of samples used from the profile.
  ///
  /// When a sampling record is used for the first time, the samples from
  /// that record are added to this accumulator.  Coverage is later computed
  /// based on the total number of samples available in this function and
  /// its callsites.
  ///
  /// Note that this accumulator tracks samples used from a single function
  /// and all the inlined callsites. Strictly, we should have a map of counters
  /// keyed by FunctionSamples pointers, but these stats are cleared after
  /// every function, so we just need to keep a single counter.
  uint64_t TotalUsedSamples = 0;

  // For symbol in profile symbol list, whether to regard their profiles
  // to be accurate. This is passed from the SampleLoader instance.
  bool ProfAccForSymsInList = false;
};

/// Return true if the given callsite is hot wrt to hot cutoff threshold.
///
/// \param CallsiteFS Function samples for the callsite, or nullptr if the
/// callsite was not inlined in the original binary.
/// \param PSI Profile summary used to compute hot/cold cutoffs.
/// \param ProfAccForSymsInList When true, treat non-cold counts as hot so warm
/// callsites can be early-inlined under profile-symbol-list accuracy mode.
/// \returns true if \p CallsiteFS is non-null and considered hot.
LLVM_ABI bool callsiteIsHot(const FunctionSamples *CallsiteFS,
                            ProfileSummaryInfo *PSI, bool ProfAccForSymsInList);

/// Create a global variable to flag FSDiscriminators are used.
///
/// \param M Module in which to create the FS-discriminator marker variable.
LLVM_ABI void createFSDiscriminatorVariable(Module *M);

} // end of namespace sampleprofutil
} // end of namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SAMPLEPROFILELOADERBASEUTIL_H
