//===- InstrProfWriter.h - Instrumented profiling writer --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing profiling data for instrumentation
// based PGO and coverage.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_INSTRPROFWRITER_H
#define LLVM_PROFILEDATA_INSTRPROFWRITER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Object/BuildID.h"
#include "llvm/ProfileData/DataAccessProf.h"
#include "llvm/ProfileData/IndexedMemProfData.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/MemProfSummaryBuilder.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <random>

namespace llvm {

/// Trait for serializing instrumentation profile records into an on-disk hash
/// table.
class InstrProfRecordWriterTrait;
class ProfOStream;
class MemoryBuffer;
class raw_fd_ostream;

/// Writer for instrumentation based profile data.
class InstrProfWriter {
public:
  /// Map from function hash to instrumentation profile record.
  using ProfilingData = SmallDenseMap<uint64_t, InstrProfRecord>;

private:
  bool Sparse;
  StringMap<ProfilingData> FunctionData;
  /// The maximum length of a single temporal profile trace.
  uint64_t MaxTemporalProfTraceLength;
  /// The maximum number of stored temporal profile traces.
  uint64_t TemporalProfTraceReservoirSize;
  /// The total number of temporal profile traces seen.
  uint64_t TemporalProfTraceStreamSize = 0;
  /// The list of temporal profile traces.
  SmallVector<TemporalProfTraceTy> TemporalProfTraces;
  std::mt19937 RNG;

  // The MemProf data.
  memprof::IndexedMemProfData MemProfData;

  // List of binary ids.
  std::vector<llvm::object::BuildID> BinaryIds;

  // Read the vtable names from raw instr profile reader.
  StringSet<> VTableNames;

  // An enum describing the attributes of the profile.
  InstrProfKind ProfileKind = InstrProfKind::Unknown;
  // Use raw pointer here for the incomplete type object.
  InstrProfRecordWriterTrait *InfoObj;

  // Temporary support for writing the previous version of the format, to enable
  // some forward compatibility. Currently this suppresses the writing of the
  // new vtable names section and header fields.
  // TODO: Consider enabling this with future version changes as well, to ease
  // deployment of newer versions of llvm-profdata.
  bool WritePrevVersion = false;

  // The MemProf version we should write.
  memprof::IndexedVersion MemProfVersionRequested;

  // Whether to serialize the full schema.
  bool MemProfFullSchema;

  // Whether to generated random memprof hotness for testing.
  bool MemprofGenerateRandomHotness;

  std::unique_ptr<memprof::DataAccessProfData> DataAccessProfileData;

  // MemProf summary builder to which records are added as MemProf data is added
  // to the writer.
  memprof::MemProfSummaryBuilder MemProfSumBuilder;

public:
  /// Construct an instrumentation profile writer.
  ///
  /// For memprof testing, random hotness can be assigned to the contexts if
  /// \p MemprofGenerateRandomHotness is enabled.
  /// \param Sparse If true, omit zero-count records from the output.
  /// \param TemporalProfTraceReservoirSize Maximum number of temporal traces
  ///        retained via reservoir sampling.
  /// \param MaxTemporalProfTraceLength Maximum length of a single temporal
  ///        profile trace.
  /// \param WritePrevVersion If true, write the previous indexed format for
  ///        forward compatibility.
  /// \param MemProfVersionRequested MemProf indexed format version to write.
  /// \param MemProfFullSchema If true, serialize the full MemProf schema.
  /// \param MemprofGenerateRandomHotness If true, assign random MemProf
  ///        hotness for testing.
  /// \param RandomSeed Seed used when generating random MemProf hotness.
  LLVM_ABI InstrProfWriter(bool Sparse = false,
                           uint64_t TemporalProfTraceReservoirSize = 0,
                           uint64_t MaxTemporalProfTraceLength = 0,
                           bool WritePrevVersion = false,
                           memprof::IndexedVersion MemProfVersionRequested =
                               static_cast<memprof::IndexedVersion>(
                                   memprof::MinimumSupportedVersion),
                           bool MemProfFullSchema = false,
                           bool MemprofGenerateRandomHotness = false,
                           unsigned RandomSeed = 0);
  /// Destroy the writer and release owned resources.
  LLVM_ABI ~InstrProfWriter();

  /// Return the map of function names to profile data.
  /// @return Mutable map from function name to profiling data.
  StringMap<ProfilingData> &getProfileData() { return FunctionData; }

  /// Add function counts for the given function.
  ///
  /// If there are already counts for this function and the hash and number of
  /// counts match, each counter is summed. Optionally scale counts by
  /// \p Weight.
  /// \param I Named profile record to add or merge.
  /// \param Weight Scale factor applied to the record's counters.
  /// \param Warn Callback invoked for non-fatal merge warnings.
  LLVM_ABI void addRecord(NamedInstrProfRecord &&I, uint64_t Weight,
                          function_ref<void(Error)> Warn);
  /// Add function counts for the given function with weight 1.
  ///
  /// If there are already counts for this function and the hash and number of
  /// counts match, each counter is summed.
  /// \param I Named profile record to add or merge.
  /// \param Warn Callback invoked for non-fatal merge warnings.
  void addRecord(NamedInstrProfRecord &&I, function_ref<void(Error)> Warn) {
    addRecord(std::move(I), 1, Warn);
  }
  /// Record a vtable name for inclusion in the profile.
  /// \param VTableName Vtable name string to store.
  void addVTableName(StringRef VTableName) { VTableNames.insert(VTableName); }

  /// Add temporal profile traces using reservoir sampling.
  ///
  /// \p SrcStreamSize is the total number of temporal profiling traces the
  /// source has seen.
  /// \param SrcTraces Temporal traces to merge into this writer.
  /// \param SrcStreamSize Total number of traces observed by the source.
  LLVM_ABI void
  addTemporalProfileTraces(SmallVectorImpl<TemporalProfTraceTy> &SrcTraces,
                           uint64_t SrcStreamSize);

  /// Add the entire MemProf data set to the writer context.
  /// \param Incoming MemProf data to merge into this writer.
  /// \param Warn Callback invoked for non-fatal merge warnings.
  /// @return True on success, or false if a frame or call stack could not be
  ///         merged.
  LLVM_ABI bool addMemProfData(memprof::IndexedMemProfData Incoming,
                               function_ref<void(Error)> Warn);

  /// Append binary IDs to the writer's binary ID list.
  /// \param BIs Build IDs to add.
  LLVM_ABI void addBinaryIds(ArrayRef<llvm::object::BuildID> BIs);

  /// Set the data-access profile data owned by this writer.
  /// \param DataAccessProfile Data-access profile to take ownership of.
  LLVM_ABI void addDataAccessProfData(
      std::unique_ptr<memprof::DataAccessProfData> DataAccessProfile);

  /// Merge existing function counts from the given writer.
  /// \param IPW Writer whose records are merged into this one.
  /// \param Warn Callback invoked for non-fatal merge warnings.
  LLVM_ABI void mergeRecordsFromWriter(InstrProfWriter &&IPW,
                                       function_ref<void(Error)> Warn);

  /// Write the profile to \c OS.
  /// \param OS File stream that receives the binary profile.
  /// @return Success, or an error if the binary profile cannot be written.
  LLVM_ABI Error write(raw_fd_ostream &OS);

  /// Write the profile to a string output stream \c OS.
  /// \param OS String stream that receives the binary profile.
  /// @return Success, or an error if the binary profile cannot be written.
  LLVM_ABI Error write(raw_string_ostream &OS);

  /// Write the profile in text format to \c OS.
  /// \param OS File stream that receives the text profile.
  /// @return Success, or an error if the text profile cannot be written.
  LLVM_ABI Error writeText(raw_fd_ostream &OS);

  /// Write temporal profile trace data to the header in text format.
  /// \param OS File stream that receives the text header data.
  /// \param Symtab Symbol table used to resolve function names.
  LLVM_ABI void writeTextTemporalProfTraceData(raw_fd_ostream &OS,
                                               InstrProfSymtab &Symtab);

  /// Validate that a profile record is consistent before writing.
  /// \param Func Profile record to validate.
  /// @return Success, or an error if the record is inconsistent.
  LLVM_ABI Error validateRecord(const InstrProfRecord &Func);

  /// Write a single profile record in text format to \c OS.
  /// \param Name Function name for the record.
  /// \param Hash Function hash identifying the record variant.
  /// \param Counters Profile counters and value data to emit.
  /// \param Symtab Symbol table used to resolve names.
  /// \param OS File stream that receives the text record.
  LLVM_ABI static void writeRecordInText(StringRef Name, uint64_t Hash,
                                         const InstrProfRecord &Counters,
                                         InstrProfSymtab &Symtab,
                                         raw_fd_ostream &OS);

  /// Write the profile, returning the raw data. For testing.
  /// @return Memory buffer containing the serialized profile data.
  LLVM_ABI std::unique_ptr<MemoryBuffer> writeBuffer();

  /// Update the attributes of the current profile from the attributes
  /// specified. An error is returned if IR and FE profiles are mixed.
  /// \param Other Profile kind attributes to merge into this writer.
  /// @return Success, or an error if the profile kinds are incompatible.
  Error mergeProfileKind(const InstrProfKind Other) {
    // If the kind is unset, this is the first profile we are merging so just
    // set it to the given type.
    if (ProfileKind == InstrProfKind::Unknown) {
      ProfileKind = Other;
      return Error::success();
    }

    // Returns true if merging is should fail assuming A and B are incompatible.
    auto testIncompatible = [&](InstrProfKind A, InstrProfKind B) {
      return (static_cast<bool>(ProfileKind & A) &&
              static_cast<bool>(Other & B)) ||
             (static_cast<bool>(ProfileKind & B) &&
              static_cast<bool>(Other & A));
    };

    // Check if the profiles are in-compatible. Clang frontend profiles can't be
    // merged with other profile types.
    if (static_cast<bool>(
            (ProfileKind & InstrProfKind::FrontendInstrumentation) ^
            (Other & InstrProfKind::FrontendInstrumentation))) {
      return make_error<InstrProfError>(
          instrprof_error::unsupported_version,
          "cannot merge IR generated profile with Clang generated profile");
    }
    if (testIncompatible(InstrProfKind::FunctionEntryOnly,
                         InstrProfKind::FunctionEntryInstrumentation) ||
        testIncompatible(InstrProfKind::FunctionEntryOnly,
                         InstrProfKind::LoopEntriesInstrumentation)) {
      return make_error<InstrProfError>(
          instrprof_error::unsupported_version,
          "cannot merge FunctionEntryOnly profiles and BB profiles together");
    }
    if (static_cast<bool>((ProfileKind & InstrProfKind::SingleByteCoverage) ^
                          (Other & InstrProfKind::SingleByteCoverage))) {
      return make_error<InstrProfError>(
          instrprof_error::coverage_count_mismatch);
    }

    // Now we update the profile type with the bits that are set.
    ProfileKind |= Other;
    return Error::success();
  }

  /// Return the attributes of the profile held by this writer.
  /// @return Combined InstrProfKind flags for the profile held by this writer.
  InstrProfKind getProfileKind() const { return ProfileKind; }

  /// Return true if the profile uses single-byte coverage counters.
  /// @return True if the profile uses single-byte coverage counters.
  bool hasSingleByteCoverage() const {
    return static_cast<bool>(ProfileKind & InstrProfKind::SingleByteCoverage);
  }

  /// Set the endianness used when writing value-profiling data.
  ///
  /// Internal interface for testing purpose only.
  /// \param Endianness Endianness applied to value profile payloads.
  LLVM_ABI void setValueProfDataEndianness(llvm::endianness Endianness);
  /// Set whether the writer emits a sparse profile.
  ///
  /// Internal interface for testing purpose only.
  /// \param Sparse If true, omit zero-count records from the output.
  LLVM_ABI void setOutputSparse(bool Sparse);
  /// Set the MemProf indexed format version to write.
  /// \param Version MemProf version requested for output.
  void setMemProfVersionRequested(memprof::IndexedVersion Version) {
    MemProfVersionRequested = Version;
  }
  /// Set whether to serialize the full MemProf schema.
  /// \param Full If true, write the full schema instead of the partial one.
  void setMemProfFullSchema(bool Full) { MemProfFullSchema = Full; }

  /// Compute profile overlap between this writer and \p Other.
  ///
  /// Program-level results are stored in \p Overlap and function-level results
  /// in \p FuncLevelOverlap.
  /// \param Other Named profile record compared against this writer.
  /// \param Overlap Program-level overlap statistics to update.
  /// \param FuncLevelOverlap Function-level overlap statistics to update.
  /// \param FuncFilter Filters controlling which functions are compared.
  LLVM_ABI void overlapRecord(NamedInstrProfRecord &&Other,
                              OverlapStats &Overlap,
                              OverlapStats &FuncLevelOverlap,
                              const OverlapFuncFilters &FuncFilter);

private:
  void addRecord(StringRef Name, uint64_t Hash, InstrProfRecord &&I,
                 uint64_t Weight, function_ref<void(Error)> Warn);
  bool shouldEncodeData(const ProfilingData &PD);

  /// Add a memprof record for a function identified by its \p Id.
  void addMemProfRecord(const GlobalValue::GUID Id,
                        const memprof::IndexedMemProfRecord &Record);

  /// Add a memprof frame identified by the hash of the contents of the frame in
  /// \p FrameId.
  bool addMemProfFrame(const memprof::FrameId, const memprof::Frame &F,
                       function_ref<void(Error)> Warn);

  /// Add a call stack identified by the hash of the contents of the call stack
  /// in \p CallStack.
  bool addMemProfCallStack(const memprof::CallStackId CSId,
                           const llvm::SmallVector<memprof::FrameId> &CallStack,
                           function_ref<void(Error)> Warn);

  Error writeImpl(ProfOStream &OS);

  // Writes known header fields and reserves space for fields whose value are
  // known only after payloads are written. Returns the start byte offset for
  // back patching.
  uint64_t writeHeader(const IndexedInstrProf::Header &header,
                       const bool WritePrevVersion, ProfOStream &OS);

  // Writes binary IDs.
  Error writeBinaryIds(ProfOStream &OS);

  // Writes compressed vtable names to profiles.
  Error writeVTableNames(ProfOStream &OS);
};

} // end namespace llvm

#endif // LLVM_PROFILEDATA_INSTRPROFWRITER_H
