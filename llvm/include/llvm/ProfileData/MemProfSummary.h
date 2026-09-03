//===- MemProfSummary.h - MemProf summary support ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains MemProf summary support.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_MEMPROFSUMMARY_H
#define LLVM_PROFILEDATA_MEMPROFSUMMARY_H

#include "llvm/ProfileData/DataAccessProf.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace memprof {

/// Summary statistics for a MemProf profile.
class MemProfSummary {
private:
  /// The number of summary fields below, which is used to enable some forwards
  /// and backwards compatibility for the summary when serialized in the indexed
  /// MemProf format. As long as no existing summary fields are removed or
  /// reordered, and new summary fields are added after existing summary fields,
  /// the MemProf indexed profile version does not need to be bumped to
  /// accommodate new summary fields.
  static constexpr unsigned NumSummaryFields = 6;

  const uint64_t NumContexts, NumColdContexts, NumHotContexts;
  const uint64_t MaxColdTotalSize, MaxWarmTotalSize, MaxHotTotalSize;

  // MemProf v3 and prior versions don't have data access profile,
  // so record the data access profile state.
  bool HasDataAccessProfile = false;
  size_t NumHotSymbolsAndStringLiterals = 0;
  size_t NumKnownColdSymbols = 0;
  size_t NumKnownColdStringLiterals = 0;

public:
  /// Construct a summary from the given context and size totals.
  /// @param NumContexts Total number of allocation contexts.
  /// @param NumColdContexts Number of cold allocation contexts.
  /// @param NumHotContexts Number of hot allocation contexts.
  /// @param MaxColdTotalSize Maximum total size among cold contexts.
  /// @param MaxWarmTotalSize Maximum total size among warm contexts.
  /// @param MaxHotTotalSize Maximum total size among hot contexts.
  MemProfSummary(uint64_t NumContexts, uint64_t NumColdContexts,
                 uint64_t NumHotContexts, uint64_t MaxColdTotalSize,
                 uint64_t MaxWarmTotalSize, uint64_t MaxHotTotalSize)
      : NumContexts(NumContexts), NumColdContexts(NumColdContexts),
        NumHotContexts(NumHotContexts), MaxColdTotalSize(MaxColdTotalSize),
        MaxWarmTotalSize(MaxWarmTotalSize), MaxHotTotalSize(MaxHotTotalSize),
        HasDataAccessProfile(false) {}

  /// Return the number of serialized summary fields.
  /// @return The number of serialized summary fields.
  static constexpr unsigned getNumSummaryFields() { return NumSummaryFields; }
  /// Return the total number of allocation contexts.
  /// @return The total number of allocation contexts.
  uint64_t getNumContexts() const { return NumContexts; }
  /// Return the number of cold allocation contexts.
  /// @return The number of cold allocation contexts.
  uint64_t getNumColdContexts() const { return NumColdContexts; }
  /// Return the number of hot allocation contexts.
  /// @return The number of hot allocation contexts.
  uint64_t getNumHotContexts() const { return NumHotContexts; }
  /// Return the maximum total size among cold contexts.
  /// @return The maximum total size among cold contexts.
  uint64_t getMaxColdTotalSize() const { return MaxColdTotalSize; }
  /// Return the maximum total size among warm contexts.
  /// @return The maximum total size among warm contexts.
  uint64_t getMaxWarmTotalSize() const { return MaxWarmTotalSize; }
  /// Return the maximum total size among hot contexts.
  /// @return The maximum total size among hot contexts.
  uint64_t getMaxHotTotalSize() const { return MaxHotTotalSize; }
  /// Print this summary as YAML comments to \p OS.
  /// @param OS Destination stream.
  LLVM_ABI void printSummaryYaml(raw_ostream &OS) const;
  /// Write to indexed MemProf profile.
  /// @param OS Destination indexed profile stream.
  LLVM_ABI void write(ProfOStream &OS) const;
  /// Read from indexed MemProf profile.
  /// @param Ptr Cursor into little-endian summary bytes; advanced past the
  /// fields read.
  /// @return Newly constructed MemProf summary from the serialized fields.
  LLVM_ABI static std::unique_ptr<MemProfSummary>
  deserialize(const unsigned char *&Ptr);
  /// Build data access profile summary from \p DataAccessProfile.
  ///
  /// TODO: Remove this function after the data access profile summary is
  /// serialized.
  /// @param DataAccessProfile Source data-access profile; not owned.
  LLVM_ABI void
  buildDataAccessSummary(const DataAccessProfData &DataAccessProfile);
};

} // namespace memprof
} // namespace llvm

#endif // LLVM_PROFILEDATA_MEMPROFSUMMARY_H
