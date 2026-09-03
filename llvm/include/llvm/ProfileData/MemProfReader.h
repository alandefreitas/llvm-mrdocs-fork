//===- MemProfReader.h - Instrumented memory profiling reader ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for reading MemProf profiling data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_MEMPROFREADER_H_
#define LLVM_PROFILEDATA_MEMPROFREADER_H_

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/ProfileData/IndexedMemProfData.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/ProfileData/MemProfData.inc"
#include "llvm/ProfileData/MemProfRadixTree.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

#include <functional>

namespace llvm {
namespace memprof {
/// Reader for MemProf profile data populated from external sources.
class MemProfReader {
public:
  /// Return the profile kind, which is always InstrProfKind::MemProf.
  /// @return InstrProfKind::MemProf.
  InstrProfKind getProfileKind() const { return InstrProfKind::MemProf; }

  /// Pair of a function GUID and its MemProf record.
  using GuidMemProfRecordPair = std::pair<GlobalValue::GUID, MemProfRecord>;
  /// Iterator over GUID and MemProfRecord pairs in this reader.
  using Iterator = InstrProfIterator<GuidMemProfRecordPair, MemProfReader>;
  /// Return an end iterator for the MemProf records.
  /// @return Past-the-end iterator for the MemProf records.
  Iterator end() { return Iterator(); }
  /// Return an iterator positioned at the first MemProf record.
  /// @return Iterator positioned at the first MemProf record.
  Iterator begin() {
    Iter = MemProfData.Records.begin();
    return Iterator(this);
  }

  /// Take ownership of the complete MemProf profile data.
  ///
  /// Once this function is invoked, MemProfReader no longer owns the MemProf
  /// profile.
  /// @return The complete MemProf profile previously owned by this reader.
  IndexedMemProfData takeMemProfData() { return std::move(MemProfData); }

  /// Read the next GUID and MemProfRecord pair into \p GuidRecord.
  /// \param GuidRecord Output pair set to the next function GUID and record.
  /// \param Callback Optional callback that maps a FrameId to a Frame; when
  ///        null, uses idToFrame.
  /// @return Success, or an error if the next record cannot be read.
  virtual Error
  readNextRecord(GuidMemProfRecordPair &GuidRecord,
                 std::function<const Frame(const FrameId)> Callback = nullptr) {
    if (MemProfData.Records.empty())
      return make_error<InstrProfError>(instrprof_error::empty_raw_profile);

    if (Iter == MemProfData.Records.end())
      return make_error<InstrProfError>(instrprof_error::eof);

    if (Callback == nullptr)
      Callback = [&](FrameId Id) { return idToFrame(Id); };

    CallStackIdConverter<decltype(MemProfData.CallStacks)> CSIdConv(
        MemProfData.CallStacks, Callback);

    const IndexedMemProfRecord &IndexedRecord = Iter->second;
    GuidRecord = {
        Iter->first,
        IndexedRecord.toMemProfRecord(CSIdConv),
    };
    if (CSIdConv.LastUnmappedId)
      return make_error<InstrProfError>(instrprof_error::hash_mismatch);
    Iter++;
    return Error::success();
  }

  /// Construct an empty reader for derived classes that populate data later.
  MemProfReader() = default;
  /// Destroy the MemProf reader.
  virtual ~MemProfReader() = default;

  /// Construct a reader that takes ownership of \p MemProfData.
  /// \param MemProfData Complete MemProf profile to own.
  MemProfReader(IndexedMemProfData &&MemProfData)
      : MemProfData(std::move(MemProfData)) {}

protected:
  /// Look up the Frame for \p Id in the IdToFrame map.
  /// \param Id Frame identifier to resolve.
  /// @return Const reference to the Frame mapped from \p Id.
  const Frame &idToFrame(const FrameId Id) const {
    auto It = MemProfData.Frames.find(Id);
    assert(It != MemProfData.Frames.end() && "Id not found in map.");
    return It->second;
  }
  /// Complete package of the MemProf profile owned by this reader.
  IndexedMemProfData MemProfData;
  /// Iterator into the internal function profile record map.
  llvm::MapVector<GlobalValue::GUID, IndexedMemProfRecord>::iterator Iter;
};

/// Map from stack-depot id to virtual addresses for each PC in a callstack.
using CallStackMap = llvm::DenseMap<uint64_t, llvm::SmallVector<uint64_t>>;

/// MemProfReader that populates data from raw binary instrumentation profiles.
class LLVM_ABI RawMemProfReader final : public MemProfReader {
public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  RawMemProfReader(const RawMemProfReader &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  RawMemProfReader &operator=(const RawMemProfReader &Other) = delete;
  /// Destroy the raw MemProf reader.
  ~RawMemProfReader() override;

  /// Print the contents of the profile in YAML format.
  /// \param OS Output stream that receives the YAML dump.
  void printYAML(raw_ostream &OS);

  /// Return true if \p DataBuffer starts with raw binary memprof magic bytes.
  /// \param DataBuffer Memory buffer to test for the raw memprof format.
  /// @return True if \p DataBuffer is in the raw memprof format.
  static bool hasFormat(const MemoryBuffer &DataBuffer);
  /// Return true if the file at \p Path starts with raw binary memprof magic.
  /// \param Path Path to the file to test for the raw memprof format.
  /// @return True if the file at \p Path is in the raw memprof format.
  static bool hasFormat(const StringRef Path);

  /// Create a RawMemProfReader after sanity checking the file at \p Path.
  ///
  /// The binary from which the profile has been collected is specified via a
  /// path in \p ProfiledBinary.
  /// \param Path Path to the raw memprof profile file.
  /// \param ProfiledBinary Path to the binary that was profiled.
  /// \param KeepName Whether to retain symbol names for each frame after
  ///        hashing.
  /// @return A RawMemProfReader for \p Path, or an error on failure.
  static Expected<std::unique_ptr<RawMemProfReader>>
  create(const Twine &Path, StringRef ProfiledBinary, bool KeepName = false);
  /// Create a RawMemProfReader after sanity checking \p Buffer.
  ///
  /// The binary from which the profile has been collected is specified via a
  /// path in \p ProfiledBinary.
  /// \param Buffer Memory buffer holding the raw memprof profile contents.
  /// \param ProfiledBinary Path to the binary that was profiled.
  /// \param KeepName Whether to retain symbol names for each frame after
  ///        hashing.
  /// @return A RawMemProfReader for \p Buffer, or an error on failure.
  static Expected<std::unique_ptr<RawMemProfReader>>
  create(std::unique_ptr<MemoryBuffer> Buffer, StringRef ProfiledBinary,
         bool KeepName = false);

  /// Return the build ids recorded in the segment information.
  /// \param DataBuffer Memory buffer holding the raw memprof profile.
  /// @return Build ids from the profile's segment information.
  static std::vector<std::string> peekBuildIds(MemoryBuffer *DataBuffer);

  /// Read the next GUID and MemProfRecord pair into \p GuidRecord.
  /// \param GuidRecord Output pair set to the next function GUID and record.
  /// \param Callback Callback that maps a FrameId to a Frame.
  /// @return Success, or an error if the next record cannot be read.
  Error
  readNextRecord(GuidMemProfRecordPair &GuidRecord,
                 std::function<const Frame(const FrameId)> Callback) override;

  /// Construct a reader from mock symbolizer and profile data for unit tests.
  /// \param Sym Symbolizable module used to resolve callstack frames.
  /// \param Seg Executable segment entries for the profiled binary.
  /// \param Prof Map from callstack id to heap MemInfoBlock data.
  /// \param SM Map from callstack id to virtual addresses.
  /// \param KeepName Whether to retain symbol names for each frame after
  ///        hashing.
  RawMemProfReader(std::unique_ptr<llvm::symbolize::SymbolizableModule> Sym,
                   llvm::SmallVectorImpl<SegmentEntry> &Seg,
                   llvm::MapVector<uint64_t, MemInfoBlock> &Prof,
                   CallStackMap &SM, bool KeepName = false)
      : SegmentInfo(Seg.begin(), Seg.end()), CallstackProfileData(Prof),
        StackMap(SM), KeepSymbolName(KeepName) {
    // We don't call initialize here since there is no raw profile to read. The
    // test should pass in the raw profile as structured data.

    // If there is an error here then the mock symbolizer has not been
    // initialized properly.
    if (Error E = symbolizeAndFilterStackFrames(std::move(Sym)))
      report_fatal_error(std::move(E));
    if (Error E = mapRawProfileToRecords())
      report_fatal_error(std::move(E));
  }

private:
  RawMemProfReader(object::OwningBinary<object::Binary> &&Bin, bool KeepName)
      : Binary(std::move(Bin)), KeepSymbolName(KeepName) {}
  // Initializes the RawMemProfReader with the contents in `DataBuffer`.
  Error initialize(std::unique_ptr<MemoryBuffer> DataBuffer);
  // Read and parse the contents of the `DataBuffer` as a binary format profile.
  Error readRawProfile(std::unique_ptr<MemoryBuffer> DataBuffer);
  // Initialize the segment mapping information for symbolization.
  Error setupForSymbolization();
  // Symbolize and cache all the virtual addresses we encounter in the
  // callstacks from the raw profile. Also prune callstack frames which we can't
  // symbolize or those that belong to the runtime. For profile entries where
  // the entire callstack is pruned, we drop the entry from the profile.
  Error symbolizeAndFilterStackFrames(
      std::unique_ptr<llvm::symbolize::SymbolizableModule> Symbolizer);
  // Construct memprof records for each function and store it in the
  // `FunctionProfileData` map. A function may have allocation profile data or
  // callsite data or both.
  Error mapRawProfileToRecords();

  object::SectionedAddress getModuleOffset(uint64_t VirtualAddress);

  llvm::SmallVector<std::pair<uint64_t, MemInfoBlock>>
  readMemInfoBlocks(const char *Ptr);

  // The profiled binary.
  object::OwningBinary<object::Binary> Binary;
  // Version of raw memprof binary currently being read. Defaults to most up
  // to date version.
  uint64_t MemprofRawVersion = MEMPROF_RAW_VERSION;
  // The preferred load address of the executable segment.
  uint64_t PreferredTextSegmentAddress = 0;
  // The base address of the text segment in the process during profiling.
  uint64_t ProfiledTextSegmentStart = 0;
  // The limit address of the text segment in the process during profiling.
  uint64_t ProfiledTextSegmentEnd = 0;

  // The memory mapped segment information for all executable segments in the
  // profiled binary (filtered from the raw profile using the build id).
  llvm::SmallVector<SegmentEntry, 2> SegmentInfo;

  // A map from callstack id (same as key in CallStackMap below) to the heap
  // information recorded for that allocation context.
  llvm::MapVector<uint64_t, MemInfoBlock> CallstackProfileData;
  CallStackMap StackMap;

  // Cached symbolization from PC to Frame.
  llvm::DenseMap<uint64_t, llvm::SmallVector<FrameId>> SymbolizedFrame;

  // Whether to keep the symbol name for each frame after hashing.
  bool KeepSymbolName = false;
  // A mapping of the hash to symbol name, only used if KeepSymbolName is true.
  llvm::DenseMap<uint64_t, std::string> GuidToSymbolName;
};

/// MemProfReader that populates data from a YAML MemProf profile.
class YAMLMemProfReader final : public MemProfReader {
public:
  /// Construct an empty YAML MemProf reader.
  YAMLMemProfReader() = default;

  /// Return true if \p DataBuffer starts with "---" indicating YAML content.
  /// \param DataBuffer Memory buffer to test for the YAML memprof format.
  /// @return True if \p DataBuffer looks like a YAML memprof profile.
  LLVM_ABI static bool hasFormat(const MemoryBuffer &DataBuffer);
  /// Return true if the file at \p Path starts with YAML document markers.
  /// \param Path Path to the file to test for the YAML memprof format.
  /// @return True if the file at \p Path looks like a YAML memprof profile.
  LLVM_ABI static bool hasFormat(const StringRef Path);

  /// Create a YAMLMemProfReader after sanity checking the file at \p Path.
  /// \param Path Path to the YAML memprof profile file.
  /// @return A YAMLMemProfReader for \p Path, or an error on failure.
  LLVM_ABI static Expected<std::unique_ptr<YAMLMemProfReader>>
  create(const Twine &Path);
  /// Create a YAMLMemProfReader after sanity checking \p Buffer.
  /// \param Buffer Memory buffer holding the YAML memprof profile contents.
  /// @return A YAMLMemProfReader for \p Buffer, or an error on failure.
  LLVM_ABI static Expected<std::unique_ptr<YAMLMemProfReader>>
  create(std::unique_ptr<MemoryBuffer> Buffer);

  /// Parse MemProf profile data from the YAML text in \p YAMLData.
  /// \param YAMLData YAML document text to parse into this reader.
  LLVM_ABI void parse(StringRef YAMLData);

  /// Take ownership of the parsed data-access profile data.
  /// @return Owned data-access profile, or null if none was parsed.
  std::unique_ptr<memprof::DataAccessProfData> takeDataAccessProfData() {
    return std::move(DataAccessProfileData);
  }

private:
  // Called by `parse` to set data access profiles after parsing them from Yaml
  // files.
  void
  setDataAccessProfileData(std::unique_ptr<memprof::DataAccessProfData> Data) {
    DataAccessProfileData = std::move(Data);
  }

  std::unique_ptr<memprof::DataAccessProfData> DataAccessProfileData;
};
} // namespace memprof
} // namespace llvm

#endif // LLVM_PROFILEDATA_MEMPROFREADER_H_
