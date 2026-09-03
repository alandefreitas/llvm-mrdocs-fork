//===- InstrProfReader.h - Instrumented profiling readers -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for reading profiling data for instrumentation
// based PGO and coverage.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_INSTRPROFREADER_H
#define LLVM_PROFILEDATA_INSTRPROFREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/Object/BuildID.h"
#include "llvm/ProfileData/DataAccessProf.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/InstrProfCorrelator.h"
#include "llvm/ProfileData/MemProf.h"
#include "llvm/ProfileData/MemProfSummary.h"
#include "llvm/ProfileData/MemProfYAML.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OnDiskHashTable.h"
#include "llvm/Support/SwapByteOrder.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

class InstrProfReader;

namespace vfs {
class FileSystem;
} // namespace vfs

/// A file format agnostic iterator over profiling data.
template <class record_type = NamedInstrProfRecord,
          class reader_type = InstrProfReader>
class InstrProfIterator {
public:
  /// Input iterator category tag.
  using iterator_category = std::input_iterator_tag;
  /// Type of the profile record yielded by the iterator.
  using value_type = record_type;
  /// Distance type between iterators (unused for input iterators).
  using difference_type = std::ptrdiff_t;
  /// Pointer to a profile record.
  using pointer = value_type *;
  /// Reference to a profile record.
  using reference = value_type &;

private:
  reader_type *Reader = nullptr;
  value_type Record;

  void increment() {
    if (Error E = Reader->readNextRecord(Record)) {
      // Handle errors in the reader.
      InstrProfError::take(std::move(E));
      *this = InstrProfIterator();
    }
  }

public:
  /// Construct an empty end iterator.
  InstrProfIterator() = default;
  /// Construct an iterator that begins reading from \p Reader.
  /// \param Reader Profile reader that supplies successive records.
  InstrProfIterator(reader_type *Reader) : Reader(Reader) { increment(); }

  /// Advance to the next profile record.
  /// @return Reference to this iterator after advancing.
  InstrProfIterator &operator++() {
    increment();
    return *this;
  }
  /// Return true if this iterator and \p RHS refer to the same reader.
  /// \param RHS Other iterator to compare.
  /// @return True if both iterators refer to the same reader.
  bool operator==(const InstrProfIterator &RHS) const {
    return Reader == RHS.Reader;
  }
  /// Return true if this iterator and \p RHS refer to different readers.
  /// \param RHS Other iterator to compare.
  /// @return True if the iterators refer to different readers.
  bool operator!=(const InstrProfIterator &RHS) const {
    return Reader != RHS.Reader;
  }
  /// Return a reference to the current profile record.
  /// @return Reference to the current profile record.
  value_type &operator*() { return Record; }
  /// Return a pointer to the current profile record.
  /// @return Pointer to the current profile record.
  value_type *operator->() { return &Record; }
};

/// Base class and interface for reading profiling data of any known instrprof
/// format. Provides an iterator over NamedInstrProfRecords.
class InstrProfReader {
  instrprof_error LastError = instrprof_error::success;
  std::string LastErrorMsg;

public:
  /// Construct a default instrprof reader with no open profile.
  InstrProfReader() = default;
  /// Destroy the reader.
  virtual ~InstrProfReader() = default;

  /// Read the header.  Required before reading first record.
  /// @return Success, or an error if the header cannot be read.
  virtual Error readHeader() = 0;

  /// Read a single record.
  /// \param Record Output parameter set to the next named profile record.
  /// @return Success, or an error if the next record cannot be read.
  virtual Error readNextRecord(NamedInstrProfRecord &Record) = 0;

  /// Read a list of binary ids.
  /// \param BinaryIds Output vector filled with build IDs from the profile.
  /// @return Success, or an error if binary IDs cannot be read.
  virtual Error readBinaryIds(std::vector<llvm::object::BuildID> &BinaryIds) {
    return success();
  }

  /// Print binary ids.
  /// \param OS Output stream that receives the printed build IDs.
  /// @return Success, or an error if binary IDs cannot be printed.
  virtual Error printBinaryIds(raw_ostream &OS) { return success(); };

  /// Iterator over profile data.
  /// @return Iterator positioned at the first profile record.
  InstrProfIterator<> begin() { return InstrProfIterator<>(this); }
  /// Return an end iterator for the profile data.
  /// @return Past-the-end iterator for the profile data.
  InstrProfIterator<> end() { return InstrProfIterator<>(); }

  /// Return the profile version.
  /// @return The profile format version.
  virtual uint64_t getVersion() const = 0;

  /// Return true if this is an IR-level instrumentation profile.
  /// @return True if this is an IR-level instrumentation profile.
  virtual bool isIRLevelProfile() const = 0;

  /// Return true if this is a context-sensitive IR-level profile.
  /// @return True if this is a context-sensitive IR-level profile.
  virtual bool hasCSIRLevelProfile() const = 0;

  /// Return true if the profile instruments function entries.
  /// @return True if the profile instruments function entries.
  virtual bool instrEntryBBEnabled() const = 0;

  /// Return true if the profile instruments all loop entries.
  /// @return True if the profile instruments all loop entries.
  virtual bool instrLoopEntriesEnabled() const = 0;

  /// Return true if the profile has single byte counters representing coverage.
  /// @return True if the profile has single-byte coverage counters.
  virtual bool hasSingleByteCoverage() const = 0;

  /// Return true if the profile only instruments function entries.
  /// @return True if the profile only instruments function entries.
  virtual bool functionEntryOnly() const = 0;

  /// Return true if profile includes a memory profile.
  /// @return True if the profile includes a memory profile.
  virtual bool hasMemoryProfile() const = 0;

  /// Return true if this has a temporal profile.
  /// @return True if this profile includes temporal profiling data.
  virtual bool hasTemporalProfile() const = 0;

  /// Returns a BitsetEnum describing the attributes of the profile. To check
  /// individual attributes prefer using the helpers above.
  /// @return Bitset describing the attributes of the profile.
  virtual InstrProfKind getProfileKind() const = 0;

  /// Return the PGO symbol table.
  ///
  /// There are three different readers: Raw, Text, and Indexed profile
  /// readers. The first two types of readers are used only by llvm-profdata
  /// tool, while the indexed profile reader is also used by llvm-cov tool and
  /// the compiler (backend or frontend). Since creating PGO symtab can create
  /// significant runtime and memory overhead (as it touches data for the whole
  /// program), InstrProfSymtab for the indexed profile reader should be
  /// created on demand and it is recommended to be only used for dumping
  /// purpose with llvm-proftool, not with the compiler.
  /// @return The PGO symbol table for this profile.
  virtual InstrProfSymtab &getSymtab() = 0;

  /// Compute the sum of counts and return in Sum.
  /// \param Sum Output accumulator that receives the summed counts.
  /// \param IsCS Whether to accumulate context-sensitive profile counts.
  LLVM_ABI void accumulateCounts(CountSumOrPercent &Sum, bool IsCS);

protected:
  /// Optional PGO symbol table owned by the reader.
  std::unique_ptr<InstrProfSymtab> Symtab;
  /// A list of temporal profile traces.
  SmallVector<TemporalProfTraceTy> TemporalProfTraces;
  /// The total number of temporal profile traces seen.
  uint64_t TemporalProfTraceStreamSize = 0;

  /// Set the current error and return same.
  /// \param Err Error code to record as the reader's last error.
  /// \param ErrMsg Optional message associated with \p Err.
  /// @return Success when \p Err is success; otherwise an InstrProfError.
  Error error(instrprof_error Err, const std::string &ErrMsg = "") {
    LastError = Err;
    LastErrorMsg = ErrMsg;
    if (Err == instrprof_error::success)
      return Error::success();
    return make_error<InstrProfError>(Err, ErrMsg);
  }

  /// Set the current error from \p E and return a matching InstrProfError.
  /// \param E Error to unwrap into the reader's last error state.
  /// @return An InstrProfError matching the recorded last error state.
  Error error(Error &&E) {
    handleAllErrors(std::move(E), [&](const InstrProfError &IPE) {
      LastError = IPE.get();
      LastErrorMsg = IPE.getMessage();
    });
    return make_error<InstrProfError>(LastError, LastErrorMsg);
  }

  /// Clear the current error and return a successful one.
  /// @return A successful Error after clearing the last error state.
  Error success() { return error(instrprof_error::success); }

public:
  /// Return true if the reader has finished reading the profile data.
  /// @return True if the reader has reached end-of-file.
  bool isEOF() { return LastError == instrprof_error::eof; }

  /// Return true if the reader encountered an error reading profiling data.
  /// @return True if the reader has a non-EOF error.
  bool hasError() { return LastError != instrprof_error::success && !isEOF(); }

  /// Get the current error.
  /// @return The current error, or success if none is pending.
  Error getError() {
    if (hasError())
      return make_error<InstrProfError>(LastError, LastErrorMsg);
    return Error::success();
  }

  /// Factory method to create an appropriately typed reader for the given
  /// instrprof file.
  /// \param Path Path to the profile file to open.
  /// \param FS Virtual file system used to open \p Path.
  /// \param Correlator Optional correlator for raw profile data.
  /// \param BIDFetcher Optional build-ID fetcher for correlation.
  /// \param BIDFetcherCorrelatorKind Correlation kind used with \p BIDFetcher.
  /// \param Warn Optional callback invoked for non-fatal warnings.
  /// @return A typed reader for \p Path, or an error on failure.
  LLVM_ABI static Expected<std::unique_ptr<InstrProfReader>> create(
      const Twine &Path, vfs::FileSystem &FS,
      const InstrProfCorrelator *Correlator = nullptr,
      const object::BuildIDFetcher *BIDFetcher = nullptr,
      const InstrProfCorrelator::ProfCorrelatorKind BIDFetcherCorrelatorKind =
          InstrProfCorrelator::ProfCorrelatorKind::NONE,
      std::function<void(Error)> Warn = nullptr);

  /// Factory method to create an appropriately typed reader for the given
  /// instrprof buffer.
  /// \param Buffer Memory buffer holding the profile contents.
  /// \param Correlator Optional correlator for raw profile data.
  /// \param BIDFetcher Optional build-ID fetcher for correlation.
  /// \param BIDFetcherCorrelatorKind Correlation kind used with \p BIDFetcher.
  /// \param Warn Optional callback invoked for non-fatal warnings.
  /// @return A typed reader for \p Buffer, or an error on failure.
  LLVM_ABI static Expected<std::unique_ptr<InstrProfReader>> create(
      std::unique_ptr<MemoryBuffer> Buffer,
      const InstrProfCorrelator *Correlator = nullptr,
      const object::BuildIDFetcher *BIDFetcher = nullptr,
      const InstrProfCorrelator::ProfCorrelatorKind BIDFetcherCorrelatorKind =
          InstrProfCorrelator::ProfCorrelatorKind::NONE,
      std::function<void(Error)> Warn = nullptr);

  /// Return the temporal profile traces.
  /// \param Weight for raw profiles use this as the temporal profile trace
  ///               weight
  /// \returns a list of temporal profile traces.
  virtual SmallVector<TemporalProfTraceTy> &
  getTemporalProfTraces(std::optional<uint64_t> Weight = {}) {
    // For non-raw profiles we ignore the input weight and instead use the
    // weights already in the traces.
    return TemporalProfTraces;
  }
  /// Return the total number of temporal profile traces seen.
  /// @return The total number of temporal profile traces seen.
  uint64_t getTemporalProfTraceStreamSize() {
    return TemporalProfTraceStreamSize;
  }
};

/// Reader for the simple text based instrprof format.
///
/// This format is a simple text format that's suitable for test data. Records
/// are separated by one or more blank lines, and record fields are separated by
/// new lines.
///
/// Each record consists of a function name, a function hash, a number of
/// counters, and then each counter value, in that order.
class LLVM_ABI TextInstrProfReader : public InstrProfReader {
private:
  /// The profile data file contents.
  std::unique_ptr<MemoryBuffer> DataBuffer;
  /// Iterator over the profile data.
  line_iterator Line;
  /// The attributes of the current profile.
  InstrProfKind ProfileKind = InstrProfKind::Unknown;

  Error readValueProfileData(InstrProfRecord &Record);

  Error readTemporalProfTraceData();

public:
  /// Construct a text profile reader over \p DataBuffer_.
  /// \param DataBuffer_ Memory buffer holding the text profile contents.
  TextInstrProfReader(std::unique_ptr<MemoryBuffer> DataBuffer_)
      : DataBuffer(std::move(DataBuffer_)), Line(*DataBuffer, true, '#') {}
  /// Deleted copy constructor.
  /// @param Other Unused; copy construction is deleted.
  TextInstrProfReader(const TextInstrProfReader &Other) = delete;
  /// Deleted copy assignment.
  /// @param Other Unused; copy assignment is deleted.
  TextInstrProfReader &operator=(const TextInstrProfReader &Other) = delete;

  /// Return true if the given buffer is in text instrprof format.
  /// \param Buffer Memory buffer to test for the text profile format.
  /// @return True if \p Buffer is in text instrprof format.
  static bool hasFormat(const MemoryBuffer &Buffer);

  /// Return the profile version (always 0 for the text format).
  /// @return Always 0; the text format has no version field.
  // Text format does not have version, so return 0.
  uint64_t getVersion() const override { return 0; }

  /// Return true if this is an IR-level instrumentation profile.
  /// @return True if this is an IR-level instrumentation profile.
  bool isIRLevelProfile() const override {
    return static_cast<bool>(ProfileKind & InstrProfKind::IRInstrumentation);
  }

  /// Return true if this is a context-sensitive IR-level profile.
  /// @return True if this is a context-sensitive IR-level profile.
  bool hasCSIRLevelProfile() const override {
    return static_cast<bool>(ProfileKind & InstrProfKind::ContextSensitive);
  }

  /// Return true if the profile instruments function entries.
  /// @return True if the profile instruments function entries.
  bool instrEntryBBEnabled() const override {
    return static_cast<bool>(ProfileKind &
                             InstrProfKind::FunctionEntryInstrumentation);
  }

  /// Return true if the profile instruments all loop entries.
  /// @return True if the profile instruments all loop entries.
  bool instrLoopEntriesEnabled() const override {
    return static_cast<bool>(ProfileKind &
                             InstrProfKind::LoopEntriesInstrumentation);
  }

  /// Return true if the profile has single byte counters representing coverage.
  /// @return True if the profile has single-byte coverage counters.
  bool hasSingleByteCoverage() const override {
    return static_cast<bool>(ProfileKind & InstrProfKind::SingleByteCoverage);
  }

  /// Return true if the profile only instruments function entries.
  /// @return True if the profile only instruments function entries.
  bool functionEntryOnly() const override {
    return static_cast<bool>(ProfileKind & InstrProfKind::FunctionEntryOnly);
  }

  /// Return true if profile includes a memory profile.
  /// @return Always false; text format memory profiles are unsupported.
  bool hasMemoryProfile() const override {
    // TODO: Add support for text format memory profiles.
    return false;
  }

  /// Return true if this has a temporal profile.
  /// @return True if this profile includes temporal profiling data.
  bool hasTemporalProfile() const override {
    return static_cast<bool>(ProfileKind & InstrProfKind::TemporalProfile);
  }

  /// Returns a BitsetEnum describing the attributes of the profile.
  /// @return Bitset describing the attributes of the profile.
  InstrProfKind getProfileKind() const override { return ProfileKind; }

  /// Read the header.
  /// @return Success, or an error if the header cannot be read.
  Error readHeader() override;

  /// Read a single record.
  /// \param Record Output parameter set to the next named profile record.
  /// @return Success, or an error if the next record cannot be read.
  Error readNextRecord(NamedInstrProfRecord &Record) override;

  /// Return the PGO symbol table.
  /// @return The PGO symbol table for this profile.
  InstrProfSymtab &getSymtab() override {
    assert(Symtab);
    return *Symtab;
  }
};

/// Reader for the raw instrprof binary format from runtime.
///
/// This format is a raw memory dump of the instrumentation-based profiling data
/// from the runtime.  It has no index.
///
/// Templated on the unsigned type whose size matches pointers on the platform
/// that wrote the profile.
template <class IntPtrT>
class RawInstrProfReader : public InstrProfReader {
private:
  /// The profile data file contents.
  std::unique_ptr<MemoryBuffer> DataBuffer;
  /// If available, this hold the ProfileData array used to correlate raw
  /// instrumentation data to their functions.
  const InstrProfCorrelatorImpl<IntPtrT> *Correlator;
  /// Fetches debuginfo by build id to correlate profiles.
  const object::BuildIDFetcher *BIDFetcher;
  /// Correlates profiles with build id fetcher by fetching debuginfo with build
  /// ID.
  std::unique_ptr<InstrProfCorrelator> BIDFetcherCorrelator;
  /// Indicates if should use debuginfo or binary to correlate with build id
  /// fetcher.
  InstrProfCorrelator::ProfCorrelatorKind BIDFetcherCorrelatorKind;
  /// A list of timestamps paired with a function name reference.
  std::vector<std::pair<uint64_t, uint64_t>> TemporalProfTimestamps;
  bool ShouldSwapBytes;
  // The value of the version field of the raw profile data header. The lower 32
  // bits specifies the format version and the most significant 32 bits specify
  // the variant types of the profile.
  uint64_t Version;
  uint64_t CountersDelta;
  uint64_t BitmapDelta;
  uint64_t UniformCountersDelta;
  uint64_t NamesDelta;
  const RawInstrProf::ProfileData<IntPtrT> *Data;
  const RawInstrProf::ProfileData<IntPtrT> *DataEnd;
  const RawInstrProf::VTableProfileData<IntPtrT> *VTableBegin = nullptr;
  const RawInstrProf::VTableProfileData<IntPtrT> *VTableEnd = nullptr;
  const char *CountersStart;
  const char *CountersEnd;
  const char *BitmapStart;
  const char *BitmapEnd;
  const char *UniformCountersStart;
  const char *UniformCountersEnd;
  const char *NamesStart;
  const char *NamesEnd;
  const char *VNamesStart = nullptr;
  const char *VNamesEnd = nullptr;
  // After value profile is all read, this pointer points to
  // the header of next profile data (if exists)
  const uint8_t *ValueDataStart;
  uint32_t ValueKindLast;
  uint32_t CurValueDataSize;
  std::vector<llvm::object::BuildID> BinaryIds;

  std::function<void(Error)> Warn;

  /// Maxium counter value 2^56.
  static const uint64_t MaxCounterValue = (1ULL << 56);

public:
  /// Construct a raw profile reader over \p DataBuffer.
  /// \param DataBuffer Memory buffer holding the raw profile contents.
  /// \param Correlator Optional correlator for raw instrumentation data.
  /// \param BIDFetcher Optional build-ID fetcher for correlation.
  /// \param BIDFetcherCorrelatorKind Correlation kind used with \p BIDFetcher.
  /// \param Warn Optional callback invoked for non-fatal warnings.
  RawInstrProfReader(
      std::unique_ptr<MemoryBuffer> DataBuffer,
      const InstrProfCorrelator *Correlator,
      const object::BuildIDFetcher *BIDFetcher,
      const InstrProfCorrelator::ProfCorrelatorKind BIDFetcherCorrelatorKind,
      std::function<void(Error)> Warn)
      : DataBuffer(std::move(DataBuffer)),
        Correlator(dyn_cast_or_null<const InstrProfCorrelatorImpl<IntPtrT>>(
            Correlator)),
        BIDFetcher(BIDFetcher),
        BIDFetcherCorrelatorKind(BIDFetcherCorrelatorKind), Warn(Warn) {}

  /// Deleted copy constructor.
  /// @param Other Unused; copy construction is deleted.
  RawInstrProfReader(const RawInstrProfReader &Other) = delete;
  /// Deleted copy assignment.
  /// @param Other Unused; copy assignment is deleted.
  RawInstrProfReader &operator=(const RawInstrProfReader &Other) = delete;

  /// Return true if \p DataBuffer is in the raw instrprof format.
  /// \param DataBuffer Memory buffer to test for the raw profile format.
  /// @return True if \p DataBuffer is in the raw instrprof format.
  static bool hasFormat(const MemoryBuffer &DataBuffer);
  /// Read the header. Required before reading first record.
  /// @return Success, or an error if the header cannot be read.
  Error readHeader() override;
  /// Read a single record.
  /// \param Record Output parameter set to the next named profile record.
  /// @return Success, or an error if the next record cannot be read.
  Error readNextRecord(NamedInstrProfRecord &Record) override;
  /// Read a list of binary ids.
  /// \param BinaryIds Output vector filled with build IDs from the profile.
  /// @return Success, or an error if binary IDs cannot be read.
  Error readBinaryIds(std::vector<llvm::object::BuildID> &BinaryIds) override;
  /// Print binary ids.
  /// \param OS Output stream that receives the printed build IDs.
  /// @return Success, or an error if binary IDs cannot be printed.
  Error printBinaryIds(raw_ostream &OS) override;

  /// Return the profile version.
  /// @return The profile format version.
  uint64_t getVersion() const override { return Version; }

  /// Return true if this is an IR-level instrumentation profile.
  /// @return True if this is an IR-level instrumentation profile.
  bool isIRLevelProfile() const override {
    return (Version & VARIANT_MASK_IR_PROF) != 0;
  }

  /// Return true if this is a context-sensitive IR-level profile.
  /// @return True if this is a context-sensitive IR-level profile.
  bool hasCSIRLevelProfile() const override {
    return (Version & VARIANT_MASK_CSIR_PROF) != 0;
  }

  /// Return true if the profile instruments function entries.
  /// @return True if the profile instruments function entries.
  bool instrEntryBBEnabled() const override {
    return (Version & VARIANT_MASK_INSTR_ENTRY) != 0;
  }

  /// Return true if the profile instruments all loop entries.
  /// @return True if the profile instruments all loop entries.
  bool instrLoopEntriesEnabled() const override {
    return (Version & VARIANT_MASK_INSTR_LOOP_ENTRIES) != 0;
  }

  /// Return true if the profile has single byte counters representing coverage.
  /// @return True if the profile has single-byte coverage counters.
  bool hasSingleByteCoverage() const override {
    return (Version & VARIANT_MASK_BYTE_COVERAGE) != 0;
  }

  /// Return true if the profile only instruments function entries.
  /// @return True if the profile only instruments function entries.
  bool functionEntryOnly() const override {
    return (Version & VARIANT_MASK_FUNCTION_ENTRY_ONLY) != 0;
  }

  /// Return true if profile includes a memory profile.
  /// @return Always false; raw memory profiles use a separate format.
  bool hasMemoryProfile() const override {
    // Memory profiles have a separate raw format, so this should never be set.
    assert(!(Version & VARIANT_MASK_MEMPROF));
    return false;
  }

  /// Return true if this has a temporal profile.
  /// @return True if this profile includes temporal profiling data.
  bool hasTemporalProfile() const override {
    return (Version & VARIANT_MASK_TEMPORAL_PROF) != 0;
  }

  /// Returns a BitsetEnum describing the attributes of the raw instr profile.
  /// @return Bitset describing the attributes of the raw profile.
  InstrProfKind getProfileKind() const override;

  /// Return the PGO symbol table.
  /// @return The PGO symbol table for this profile.
  InstrProfSymtab &getSymtab() override {
    assert(Symtab.get());
    return *Symtab.get();
  }

  /// Return the temporal profile traces.
  /// \param Weight Temporal profile trace weight for raw profiles.
  /// @return A list of temporal profile traces.
  SmallVector<TemporalProfTraceTy> &
  getTemporalProfTraces(std::optional<uint64_t> Weight = {}) override;

private:
  Error createSymtab(InstrProfSymtab &Symtab);
  Error readNextHeader(const char *CurrentPos);
  Error readHeader(const RawInstrProf::Header &Header);

  template <class IntT> IntT swap(IntT Int) const {
    return ShouldSwapBytes ? llvm::byteswap(Int) : Int;
  }

  llvm::endianness getDataEndianness() const {
    if (!ShouldSwapBytes)
      return llvm::endianness::native;
    if (llvm::endianness::native == llvm::endianness::little)
      return llvm::endianness::big;
    else
      return llvm::endianness::little;
  }

  inline uint8_t getNumPaddingBytes(uint64_t SizeInBytes) {
    return 7 & (sizeof(uint64_t) - SizeInBytes % sizeof(uint64_t));
  }

  Error readName(NamedInstrProfRecord &Record);
  Error readFuncHash(NamedInstrProfRecord &Record);
  Error readRawCounts(InstrProfRecord &Record);
  Error readRawBitmapBytes(InstrProfRecord &Record);
  Error readRawUniformCounters(InstrProfRecord &Record);
  Error readValueProfilingData(InstrProfRecord &Record);
  bool atEnd() const { return Data == DataEnd; }

  void advanceData() {
    // `CountersDelta` and `BitmapDelta` are constant zero when using debug info
    // correlation.
    if (!Correlator && !BIDFetcherCorrelator) {
      // The initial CountersDelta is the in-memory address difference between
      // the data and counts sections:
      // start(__llvm_prf_cnts) - start(__llvm_prf_data)
      // As we advance to the next record, we maintain the correct CountersDelta
      // with respect to the next record.
      CountersDelta -= sizeof(*Data);
      BitmapDelta -= sizeof(*Data);
      UniformCountersDelta -= sizeof(*Data);
    }
    Data++;
    ValueDataStart += CurValueDataSize;
  }

  const char *getNextHeaderPos() const {
      assert(atEnd());
      return (const char *)ValueDataStart;
  }

  StringRef getName(uint64_t NameRef) const {
    return Symtab->getFuncOrVarName(swap(NameRef));
  }

  int getCounterTypeSize() const {
    return hasSingleByteCoverage() ? sizeof(uint8_t) : sizeof(uint64_t);
  }
};

/// Raw instrprof reader specialized for 32-bit profile pointer values.
using RawInstrProfReader32 = RawInstrProfReader<uint32_t>;
/// Raw instrprof reader specialized for 64-bit profile pointer values.
using RawInstrProfReader64 = RawInstrProfReader<uint64_t>;

namespace IndexedInstrProf {

enum class HashT : uint32_t;

} // end namespace IndexedInstrProf

/// Trait for lookups into the on-disk hash table for the binary instrprof
/// format.
class InstrProfLookupTrait {
  std::vector<NamedInstrProfRecord> DataBuffer;
  IndexedInstrProf::HashT HashType;
  unsigned FormatVersion;
  // Endianness of the input value profile data.
  // It should be LE by default, but can be changed
  // for testing purpose.
  llvm::endianness ValueProfDataEndianness = llvm::endianness::little;

public:
  /// Construct a lookup trait for \p HashType and \p FormatVersion.
  /// \param HashType Hash algorithm used by the indexed profile.
  /// \param FormatVersion Indexed profile format version.
  InstrProfLookupTrait(IndexedInstrProf::HashT HashType, unsigned FormatVersion)
      : HashType(HashType), FormatVersion(FormatVersion) {}

  /// Array of named profile records returned by ReadData.
  using data_type = ArrayRef<NamedInstrProfRecord>;

  /// Key type stored in the on-disk hash table.
  using internal_key_type = StringRef;
  /// External lookup key type (function name).
  using external_key_type = StringRef;
  /// Hash value type for table keys.
  using hash_value_type = uint64_t;
  /// Byte offset / length type used by the on-disk format.
  using offset_type = uint64_t;

  /// Return true if keys \p A and \p B are equal.
  /// \param A First key.
  /// \param B Second key.
  /// @return True if \p A and \p B are equal.
  static bool EqualKey(StringRef A, StringRef B) { return A == B; }
  /// Return the internal key corresponding to external key \p K.
  /// \param K External lookup key.
  /// @return The internal key corresponding to \p K.
  static StringRef GetInternalKey(StringRef K) { return K; }
  /// Return the external key corresponding to internal key \p K.
  /// \param K Internal table key.
  /// @return The external key corresponding to \p K.
  static StringRef GetExternalKey(StringRef K) { return K; }

  /// Return the hash of key \p K.
  /// \param K Key to hash.
  /// @return Hash value of \p K.
  LLVM_ABI hash_value_type ComputeHash(StringRef K);

  /// Read key and data byte lengths from little-endian bytes at \p D.
  /// \param D Pointer advanced past the length fields.
  /// @return Pair of key length and data length in bytes.
  static std::pair<offset_type, offset_type>
  ReadKeyDataLength(const unsigned char *&D) {
    using namespace support;

    offset_type KeyLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    offset_type DataLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    return std::make_pair(KeyLen, DataLen);
  }

  /// Read the lookup key of length \p N from bytes at \p D.
  /// \param D Key payload.
  /// \param N Key length in bytes.
  /// @return String view of the key bytes.
  StringRef ReadKey(const unsigned char *D, offset_type N) {
    return StringRef((const char *)D, N);
  }

  /// Deserialize value profiling data from bytes between \p D and \p End.
  /// \param D Pointer advanced as value data is consumed.
  /// \param End Exclusive end of the readable byte range.
  /// @return True if value profiling data was deserialized successfully.
  LLVM_ABI bool readValueProfilingData(const unsigned char *&D,
                                       const unsigned char *const End);
  /// Deserialize profile records for key \p K from \p N bytes at \p D.
  /// \param K Lookup key associated with the records.
  /// \param D Record payload.
  /// \param N Data length in bytes.
  /// @return Array of named profile records for \p K.
  LLVM_ABI data_type ReadData(StringRef K, const unsigned char *D,
                              offset_type N);

  /// Set the endianness used when reading value profile data (testing only).
  /// \param Endianness Endianness of the value profiling payload.
  // Used for testing purpose only.
  void setValueProfDataEndianness(llvm::endianness Endianness) {
    ValueProfDataEndianness = Endianness;
  }
};

/// Base interface for iterating and looking up records in an indexed profile.
struct InstrProfReaderIndexBase {
  /// Destroy the index.
  virtual ~InstrProfReaderIndexBase() = default;

  /// Read all profile records for the key at the current iterator position.
  /// \param Data Output array reference set to the matching records.
  /// @return Success, or an error if records cannot be read.
  virtual Error getRecords(ArrayRef<NamedInstrProfRecord> &Data) = 0;

  /// Read all profile records whose key equals \p FuncName.
  /// \param FuncName Function name used as the lookup key.
  /// \param Data Output array reference set to the matching records.
  /// @return Success, or an error if records cannot be read.
  virtual Error getRecords(StringRef FuncName,
                                     ArrayRef<NamedInstrProfRecord> &Data) = 0;
  /// Advance the iterator to the next key in the index.
  virtual void advanceToNextKey() = 0;
  /// Return true if the iterator is past the last key.
  /// @return True if the iterator is past the last key.
  virtual bool atEnd() const = 0;
  /// Set the endianness used when reading value profile data.
  /// \param Endianness Endianness of the value profiling payload.
  virtual void setValueProfDataEndianness(llvm::endianness Endianness) = 0;
  /// Return the profile format version.
  /// @return The profile format version.
  virtual uint64_t getVersion() const = 0;
  /// Return true if this is an IR-level instrumentation profile.
  /// @return True if this is an IR-level instrumentation profile.
  virtual bool isIRLevelProfile() const = 0;
  /// Return true if this is a context-sensitive IR-level profile.
  /// @return True if this is a context-sensitive IR-level profile.
  virtual bool hasCSIRLevelProfile() const = 0;
  /// Return true if the profile instruments function entries.
  /// @return True if the profile instruments function entries.
  virtual bool instrEntryBBEnabled() const = 0;
  /// Return true if the profile instruments all loop entries.
  /// @return True if the profile instruments all loop entries.
  virtual bool instrLoopEntriesEnabled() const = 0;
  /// Return true if the profile has single byte counters representing coverage.
  /// @return True if the profile has single-byte coverage counters.
  virtual bool hasSingleByteCoverage() const = 0;
  /// Return true if the profile only instruments function entries.
  /// @return True if the profile only instruments function entries.
  virtual bool functionEntryOnly() const = 0;
  /// Return true if profile includes a memory profile.
  /// @return True if the profile includes a memory profile.
  virtual bool hasMemoryProfile() const = 0;
  /// Return true if this has a temporal profile.
  /// @return True if this profile includes temporal profiling data.
  virtual bool hasTemporalProfile() const = 0;
  /// Returns a BitsetEnum describing the attributes of the profile.
  /// @return Bitset describing the attributes of the profile.
  virtual InstrProfKind getProfileKind() const = 0;
  /// Populate \p Symtab from the keys in this index.
  /// \param Symtab Symbol table to populate.
  /// @return Success, or an error if the symbol table cannot be populated.
  virtual Error populateSymtab(InstrProfSymtab &Symtab) = 0;
};

/// On-disk iterable hash table used by indexed instrprof format version 3.
using OnDiskHashTableImplV3 =
    OnDiskIterableChainedHashTable<InstrProfLookupTrait>;

/// On-disk hash table of MemProf records keyed by function name hash.
using MemProfRecordHashTable =
    OnDiskIterableChainedHashTable<memprof::RecordLookupTrait>;
/// On-disk hash table of MemProf frames keyed by frame id.
using MemProfFrameHashTable =
    OnDiskIterableChainedHashTable<memprof::FrameLookupTrait>;
/// On-disk hash table of MemProf call stacks keyed by call stack id.
using MemProfCallStackHashTable =
    OnDiskIterableChainedHashTable<memprof::CallStackLookupTrait>;

/// Remapper that matches Itanium-mangled names to indexed profile keys.
template <typename HashTableImpl>
class InstrProfReaderItaniumRemapper;

/// Index over an on-disk hash table of named instrumentation profile records.
template <typename HashTableImpl>
class InstrProfReaderIndex : public InstrProfReaderIndexBase {
private:
  std::unique_ptr<HashTableImpl> HashTable;
  typename HashTableImpl::data_iterator RecordIterator;
  uint64_t FormatVersion;

  friend class InstrProfReaderItaniumRemapper<HashTableImpl>;

public:
  /// Construct an index over the on-disk hash table spanning \p Buckets.
  /// \param Buckets Pointer to the hash table bucket array.
  /// \param Payload Pointer to the hash table payload region.
  /// \param Base Base address used to resolve on-disk offsets.
  /// \param HashType Hash algorithm used by the indexed profile.
  /// \param Version Indexed profile format version (including variant bits).
  InstrProfReaderIndex(const unsigned char *Buckets,
                       const unsigned char *const Payload,
                       const unsigned char *const Base,
                       IndexedInstrProf::HashT HashType, uint64_t Version);
  /// Destroy the index.
  ~InstrProfReaderIndex() override = default;

  /// Read all profile records for the key at the current iterator position.
  /// \param Data Output array reference set to the matching records.
  /// @return Success, or an error if records cannot be read.
  Error getRecords(ArrayRef<NamedInstrProfRecord> &Data) override;
  /// Read all profile records whose key equals \p FuncName.
  /// \param FuncName Function name used as the lookup key.
  /// \param Data Output array reference set to the matching records.
  /// @return Success, or an error if records cannot be read.
  Error getRecords(StringRef FuncName,
                   ArrayRef<NamedInstrProfRecord> &Data) override;
  /// Advance the iterator to the next key in the index.
  void advanceToNextKey() override { RecordIterator++; }

  /// Return true if the iterator is past the last key.
  /// @return True if the iterator is past the last key.
  bool atEnd() const override {
    return RecordIterator == HashTable->data_end();
  }

  /// Set the endianness used when reading value profile data.
  /// \param Endianness Endianness of the value profiling payload.
  void setValueProfDataEndianness(llvm::endianness Endianness) override {
    HashTable->getInfoObj().setValueProfDataEndianness(Endianness);
  }

  /// Return the profile format version.
  /// @return The profile format version.
  uint64_t getVersion() const override { return GET_VERSION(FormatVersion); }

  /// Return true if this is an IR-level instrumentation profile.
  /// @return True if this is an IR-level instrumentation profile.
  bool isIRLevelProfile() const override {
    return (FormatVersion & VARIANT_MASK_IR_PROF) != 0;
  }

  /// Return true if this is a context-sensitive IR-level profile.
  /// @return True if this is a context-sensitive IR-level profile.
  bool hasCSIRLevelProfile() const override {
    return (FormatVersion & VARIANT_MASK_CSIR_PROF) != 0;
  }

  /// Return true if the profile instruments function entries.
  /// @return True if the profile instruments function entries.
  bool instrEntryBBEnabled() const override {
    return (FormatVersion & VARIANT_MASK_INSTR_ENTRY) != 0;
  }

  /// Return true if the profile instruments all loop entries.
  /// @return True if the profile instruments all loop entries.
  bool instrLoopEntriesEnabled() const override {
    return (FormatVersion & VARIANT_MASK_INSTR_LOOP_ENTRIES) != 0;
  }

  /// Return true if the profile has single byte counters representing coverage.
  /// @return True if the profile has single-byte coverage counters.
  bool hasSingleByteCoverage() const override {
    return (FormatVersion & VARIANT_MASK_BYTE_COVERAGE) != 0;
  }

  /// Return true if the profile only instruments function entries.
  /// @return True if the profile only instruments function entries.
  bool functionEntryOnly() const override {
    return (FormatVersion & VARIANT_MASK_FUNCTION_ENTRY_ONLY) != 0;
  }

  /// Return true if profile includes a memory profile.
  /// @return True if the profile includes a memory profile.
  bool hasMemoryProfile() const override {
    return (FormatVersion & VARIANT_MASK_MEMPROF) != 0;
  }

  /// Return true if this has a temporal profile.
  /// @return True if this profile includes temporal profiling data.
  bool hasTemporalProfile() const override {
    return (FormatVersion & VARIANT_MASK_TEMPORAL_PROF) != 0;
  }

  /// Returns a BitsetEnum describing the attributes of the profile.
  /// @return Bitset describing the attributes of the profile.
  InstrProfKind getProfileKind() const override;

  /// Populate \p Symtab from the keys in this index.
  /// \param Symtab Symbol table to populate.
  /// @return Success, or an error if the symbol table cannot be populated.
  Error populateSymtab(InstrProfSymtab &Symtab) override {
    // FIXME: the create method calls 'finalizeSymtab' and sorts a bunch of
    // arrays/maps. Since there are other data sources other than 'HashTable' to
    // populate a symtab, it might make sense to have something like this
    // 1. Let each data source populate Symtab and init the arrays/maps without
    // calling 'finalizeSymtab'
    // 2. Call 'finalizeSymtab' once to get all arrays/maps sorted if needed.
    return Symtab.create(HashTable->keys());
  }
};

/// Name matcher supporting fuzzy matching of symbol names to names in profiles.
class InstrProfReaderRemapper {
public:
  /// Destroy the remapper.
  virtual ~InstrProfReaderRemapper() = default;
  /// Populate name remappings used for fuzzy profile lookups.
  /// @return Success, or an error if remappings cannot be populated.
  virtual Error populateRemappings() { return Error::success(); }
  /// Look up profile records for \p FuncName after applying remappings.
  /// \param FuncName Function name to look up (possibly remapped).
  /// \param Data Output array reference set to the matching records.
  /// @return Success, or an error if records cannot be looked up.
  virtual Error getRecords(StringRef FuncName,
                           ArrayRef<NamedInstrProfRecord> &Data) = 0;
};

/// Reader for the MemProf section of an indexed instrumentation profile.
class IndexedMemProfReader {
private:
  /// The MemProf version.
  memprof::IndexedVersion Version =
      static_cast<memprof::IndexedVersion>(memprof::MinimumSupportedVersion);
  /// MemProf summary (if available, version >= 4).
  std::unique_ptr<memprof::MemProfSummary> MemProfSum;
  /// MemProf profile schema (if available).
  memprof::MemProfSchema Schema;
  /// MemProf record profile data on-disk indexed via llvm::md5(FunctionName).
  std::unique_ptr<MemProfRecordHashTable> MemProfRecordTable;
  /// MemProf frame profile data on-disk indexed via frame id.
  std::unique_ptr<MemProfFrameHashTable> MemProfFrameTable;
  /// MemProf call stack data on-disk indexed via call stack id.
  std::unique_ptr<MemProfCallStackHashTable> MemProfCallStackTable;
  /// The starting address of the frame array.
  const unsigned char *FrameBase = nullptr;
  /// The starting address of the call stack array.
  const unsigned char *CallStackBase = nullptr;
  // The number of elements in the radix tree array.
  unsigned RadixTreeSize = 0;
  /// The data access profiles, deserialized from binary data.
  std::unique_ptr<memprof::DataAccessProfData> DataAccessProfileData;

  Error deserializeV2(const unsigned char *Start, const unsigned char *Ptr);
  Error deserializeRadixTreeBased(const unsigned char *Start,
                                  const unsigned char *Ptr,
                                  memprof::IndexedVersion Version);

public:
  /// Construct an empty MemProf reader with no deserialized data.
  IndexedMemProfReader() = default;

  /// Deserialize the MemProf section starting at \p MemProfOffset in \p Start.
  /// \param Start Base address of the indexed profile buffer.
  /// \param MemProfOffset Byte offset of the MemProf section from \p Start.
  /// @return Success, or an error if MemProf data cannot be deserialized.
  LLVM_ABI Error deserialize(const unsigned char *Start,
                             uint64_t MemProfOffset);

  /// Return the memprof record for the function identified by \p FuncNameHash.
  /// \param FuncNameHash Hash of the function name (llvm::md5).
  /// @return The MemProf record on success, or an error if not found.
  LLVM_ABI Expected<memprof::MemProfRecord>
  getMemProfRecord(const uint64_t FuncNameHash) const;

  /// Return caller-callee pairs derived from the MemProf call graph.
  /// @return Map from caller hash to callee call-edge list.
  LLVM_ABI DenseMap<uint64_t, SmallVector<memprof::CallEdgeTy, 0>>
  getMemProfCallerCalleePairs() const;

  /// Return a non-owned pointer to data access profile data.
  /// @return Non-owned pointer to data access profile data, or nullptr.
  memprof::DataAccessProfData *getDataAccessProfileData() const {
    return DataAccessProfileData.get();
  }

  /// Return the entire MemProf profile.
  /// @return All MemProf data from this indexed section.
  LLVM_ABI memprof::AllMemProfData getAllMemProfData() const;

  /// Return the MemProf summary, or nullptr if unavailable.
  /// @return The MemProf summary, or nullptr if unavailable.
  memprof::MemProfSummary *getSummary() const { return MemProfSum.get(); }
};

/// Reader for the indexed binary instrprof format.
class LLVM_ABI IndexedInstrProfReader : public InstrProfReader {
private:
  /// The profile data file contents.
  std::unique_ptr<MemoryBuffer> DataBuffer;
  /// The profile remapping file contents.
  std::unique_ptr<MemoryBuffer> RemappingBuffer;
  /// The index into the profile data.
  std::unique_ptr<InstrProfReaderIndexBase> Index;
  /// The profile remapping file contents.
  std::unique_ptr<InstrProfReaderRemapper> Remapper;
  /// Profile summary data.
  std::unique_ptr<ProfileSummary> Summary;
  /// Context sensitive profile summary data.
  std::unique_ptr<ProfileSummary> CS_Summary;
  IndexedMemProfReader MemProfReader;
  /// The compressed vtable names, to be used for symtab construction.
  /// A compiler that reads indexed profiles could construct symtab from module
  /// IR so it doesn't need the decompressed names.
  StringRef VTableName;
  /// A memory buffer holding binary ids.
  ArrayRef<uint8_t> BinaryIdsBuffer;

  // Index to the current record in the record array.
  unsigned RecordIndex = 0;

  // Read the profile summary. Return a pointer pointing to one byte past the
  // end of the summary data if it exists or the input \c Cur.
  // \c UseCS indicates whether to use the context-sensitive profile summary.
  const unsigned char *readSummary(IndexedInstrProf::ProfVersion Version,
                                   const unsigned char *Cur, bool UseCS);

public:
  /// Construct an indexed profile reader over \p DataBuffer.
  /// \param DataBuffer Memory buffer holding the indexed profile contents.
  /// \param RemappingBuffer Optional buffer holding profile remapping data.
  IndexedInstrProfReader(
      std::unique_ptr<MemoryBuffer> DataBuffer,
      std::unique_ptr<MemoryBuffer> RemappingBuffer = nullptr)
      : DataBuffer(std::move(DataBuffer)),
        RemappingBuffer(std::move(RemappingBuffer)) {}
  /// Deleted copy constructor.
  /// @param Other Unused; copy construction is deleted.
  IndexedInstrProfReader(const IndexedInstrProfReader &Other) = delete;
  /// Deleted copy assignment.
  /// @param Other Unused; copy assignment is deleted.
  IndexedInstrProfReader &operator=(const IndexedInstrProfReader &Other) = delete;

  /// Return the profile version.
  /// @return The profile format version.
  uint64_t getVersion() const override { return Index->getVersion(); }
  /// Return true if this is an IR-level instrumentation profile.
  /// @return True if this is an IR-level instrumentation profile.
  bool isIRLevelProfile() const override { return Index->isIRLevelProfile(); }
  /// Return true if this is a context-sensitive IR-level profile.
  /// @return True if this is a context-sensitive IR-level profile.
  bool hasCSIRLevelProfile() const override {
    return Index->hasCSIRLevelProfile();
  }

  /// Return true if the profile instruments function entries.
  /// @return True if the profile instruments function entries.
  bool instrEntryBBEnabled() const override {
    return Index->instrEntryBBEnabled();
  }

  /// Return true if the profile instruments all loop entries.
  /// @return True if the profile instruments all loop entries.
  bool instrLoopEntriesEnabled() const override {
    return Index->instrLoopEntriesEnabled();
  }

  /// Return true if the profile has single byte counters representing coverage.
  /// @return True if the profile has single-byte coverage counters.
  bool hasSingleByteCoverage() const override {
    return Index->hasSingleByteCoverage();
  }

  /// Return true if the profile only instruments function entries.
  /// @return True if the profile only instruments function entries.
  bool functionEntryOnly() const override { return Index->functionEntryOnly(); }

  /// Return true if profile includes a memory profile.
  /// @return True if the profile includes a memory profile.
  bool hasMemoryProfile() const override { return Index->hasMemoryProfile(); }

  /// Return true if this has a temporal profile.
  /// @return True if this profile includes temporal profiling data.
  bool hasTemporalProfile() const override {
    return Index->hasTemporalProfile();
  }

  /// Returns a BitsetEnum describing the attributes of the indexed instr
  /// profile.
  /// @return Bitset describing the attributes of the indexed profile.
  InstrProfKind getProfileKind() const override {
    return Index->getProfileKind();
  }

  /// Return true if the given buffer is in an indexed instrprof format.
  /// \param DataBuffer Memory buffer to test for the indexed profile format.
  /// @return True if \p DataBuffer is in indexed instrprof format.
  static bool hasFormat(const MemoryBuffer &DataBuffer);

  /// Read the file header.
  /// @return Success, or an error if the header cannot be read.
  Error readHeader() override;
  /// Read a single record.
  /// \param Record Output parameter set to the next named profile record.
  /// @return Success, or an error if the next record cannot be read.
  Error readNextRecord(NamedInstrProfRecord &Record) override;

  /// Return the NamedInstrProfRecord for \p FuncName and \p FuncHash.
  ///
  /// When returning a hash_mismatch error and MismatchedFuncSum is not
  /// nullptr, the sum of all counters in the mismatched function will be set
  /// to MismatchedFuncSum. If there are multiple instances of mismatched
  /// functions, MismatchedFuncSum returns the maximum. If \c FuncName is not
  /// found, try to lookup \c DeprecatedFuncName to handle profiles built by
  /// older compilers.
  /// \param FuncName Function name used for the primary lookup.
  /// \param FuncHash Function CFG hash that must match the record.
  /// \param DeprecatedFuncName Fallback name used for older profile formats.
  /// \param MismatchedFuncSum Optional out-parameter set on hash mismatch.
  /// @return The matching NamedInstrProfRecord, or an error on failure.
  Expected<NamedInstrProfRecord>
  getInstrProfRecord(StringRef FuncName, uint64_t FuncHash,
                     StringRef DeprecatedFuncName = "",
                     uint64_t *MismatchedFuncSum = nullptr);

  /// Return the memprof record for the function identified by llvm::md5(Name).
  /// \param FuncNameHash Hash of the function name (llvm::md5).
  /// @return The MemProf record on success, or an error if not found.
  Expected<memprof::MemProfRecord> getMemProfRecord(uint64_t FuncNameHash) {
    return MemProfReader.getMemProfRecord(FuncNameHash);
  }

  /// Return caller-callee pairs derived from the MemProf call graph.
  /// @return Map from caller hash to callee call-edge list.
  DenseMap<uint64_t, SmallVector<memprof::CallEdgeTy, 0>>
  getMemProfCallerCalleePairs() {
    return MemProfReader.getMemProfCallerCalleePairs();
  }

  /// Return the entire MemProf profile.
  /// @return All MemProf data from this indexed profile.
  memprof::AllMemProfData getAllMemProfData() const {
    return MemProfReader.getAllMemProfData();
  }

  /// Fill Counts with the profile data for the given function name.
  /// \param FuncName Function name whose counters are requested.
  /// \param FuncHash Function CFG hash that must match the record.
  /// \param Counts Output vector filled with the function's counter values.
  /// @return Success, or an error if the function counters cannot be read.
  Error getFunctionCounts(StringRef FuncName, uint64_t FuncHash,
                          std::vector<uint64_t> &Counts);

  /// Fill Bitmap with the profile data for the given function name.
  /// \param FuncName Function name whose bitmap is requested.
  /// \param FuncHash Function CFG hash that must match the record.
  /// \param Bitmap Output bit vector filled with the function's bitmap bytes.
  /// @return Success, or an error if the function bitmap cannot be read.
  Error getFunctionBitmap(StringRef FuncName, uint64_t FuncHash,
                          BitVector &Bitmap);

  /// Return the maximum of all known function counts.
  /// \param UseCS Whether to use the context-sensitive profile summary.
  /// @return Maximum function count from the selected profile summary.
  uint64_t getMaximumFunctionCount(bool UseCS) {
    if (UseCS) {
      assert(CS_Summary && "No context sensitive profile summary");
      return CS_Summary->getMaxFunctionCount();
    } else {
      assert(Summary && "No profile summary");
      return Summary->getMaxFunctionCount();
    }
  }

  /// Factory method to create an indexed reader.
  /// \param Path Path to the indexed profile file to open.
  /// \param FS Virtual file system used to open \p Path and \p RemappingPath.
  /// \param RemappingPath Optional path to a profile remapping file.
  /// @return An indexed reader for \p Path, or an error on failure.
  static Expected<std::unique_ptr<IndexedInstrProfReader>>
  create(const Twine &Path, vfs::FileSystem &FS,
         const Twine &RemappingPath = "");

  /// Factory method to create an indexed reader from memory buffers.
  /// \param Buffer Memory buffer holding the indexed profile contents.
  /// \param RemappingBuffer Optional buffer holding profile remapping data.
  /// @return An indexed reader for \p Buffer, or an error on failure.
  static Expected<std::unique_ptr<IndexedInstrProfReader>>
  create(std::unique_ptr<MemoryBuffer> Buffer,
         std::unique_ptr<MemoryBuffer> RemappingBuffer = nullptr);

  /// Set the endianness used when reading value profile data (testing only).
  /// \param Endianness Endianness of the value profiling payload.
  // Used for testing purpose only.
  void setValueProfDataEndianness(llvm::endianness Endianness) {
    Index->setValueProfDataEndianness(Endianness);
  }

  /// Return the PGO symbol table.
  ///
  /// See description in the base class. This interface is designed to be used
  /// by llvm-profdata (for dumping). Avoid using this when the client is the
  /// compiler.
  /// @return The PGO symbol table for this profile.
  InstrProfSymtab &getSymtab() override;

  /// Return the profile summary.
  /// \param UseCS Whether to use the context-sensitive profile summary.
  /// @return The selected (context-sensitive or regular) profile summary.
  ProfileSummary &getSummary(bool UseCS) {
    if (UseCS) {
      assert(CS_Summary && "No context sensitive summary");
      return *CS_Summary;
    } else {
      assert(Summary && "No profile summary");
      return *Summary;
    }
  }

  /// Return the MemProf summary. Will be null if unavailable (version < 4).
  /// @return The MemProf summary, or nullptr if unavailable.
  memprof::MemProfSummary *getMemProfSummary() const {
    return MemProfReader.getSummary();
  }

  /// Returns non-owned pointer to the data access profile data.
  /// Will be null if unavailable (version < 4).
  /// @return Non-owned pointer to data access profile data, or nullptr.
  memprof::DataAccessProfData *getDataAccessProfileData() const {
    return MemProfReader.getDataAccessProfileData();
  }

  /// Read a list of binary ids.
  /// \param BinaryIds Output vector filled with build IDs from the profile.
  /// @return Success, or an error if binary IDs cannot be read.
  Error readBinaryIds(std::vector<llvm::object::BuildID> &BinaryIds) override;
  /// Print binary ids.
  /// \param OS Output stream that receives the printed build IDs.
  /// @return Success, or an error if binary IDs cannot be printed.
  Error printBinaryIds(raw_ostream &OS) override;
};

} // end namespace llvm

#endif // LLVM_PROFILEDATA_INSTRPROFREADER_H
