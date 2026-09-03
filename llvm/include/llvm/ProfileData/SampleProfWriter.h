//===- SampleProfWriter.h - Write LLVM sample profile data ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions needed for writing sample profiles.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_PROFILEDATA_SAMPLEPROFWRITER_H
#define LLVM_PROFILEDATA_SAMPLEPROFWRITER_H

#include "llvm/ADT/Eytzinger.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <memory>
#include <system_error>

namespace llvm {
namespace sampleprof {

/// Section ordering layouts for extensible binary sample profiles.
enum SectionLayout {
  /// Default section layout.
  DefaultLayout,
  /// Layout that splits profiles with inlined functions from those without.
  ///
  /// When ThinLTO is enabled, the ThinLTO postlink phase only has to load
  /// profile with inlined functions and can skip the other part.
  CtxSplitLayout,
  /// Number of section layout variants.
  NumOfLayout,
};

/// Strategy for dropping functions when writing a size-limited profile.
///
/// When writing a profile with size limit, user may want to use a different
/// strategy to reduce function count other than dropping functions with fewest
/// samples first. In this case a class implementing the same interfaces should
/// be provided to SampleProfileWriter::writeWithSizeLimit().
class FunctionPruningStrategy {
protected:
  /// Profile map modified by Erase() during size-limited writes.
  SampleProfileMap &ProfileMap;
  /// Size limit in bytes for the output profile.
  size_t OutputSizeLimit;

public:
  /// Construct a pruning strategy for \p ProfileMap with \p OutputSizeLimit.
  ///
  /// @param ProfileMap A reference to the original profile map. It will be
  /// modified by Erase().
  /// @param OutputSizeLimit Size limit in bytes of the output profile. This is
  /// necessary to estimate how many functions to remove.
  FunctionPruningStrategy(SampleProfileMap &ProfileMap, size_t OutputSizeLimit)
      : ProfileMap(ProfileMap), OutputSizeLimit(OutputSizeLimit) {}

  /// Destroy the pruning strategy.
  virtual ~FunctionPruningStrategy() = default;

  /// Erase functions from the profile map to reduce output size.
  ///
  /// SampleProfileWriter::writeWithSizeLimit() calls this after every write
  /// iteration if the output size still exceeds the limit. This function
  /// should erase some functions from the profile map so that the writer tries
  /// to write the profile again with fewer functions. At least 1 entry from the
  /// profile map must be erased.
  ///
  /// @param CurrentOutputSize Number of bytes in the output if current profile
  /// map is written.
  virtual void Erase(size_t CurrentOutputSize) = 0;
};

/// Default pruning strategy that drops functions with the fewest samples.
class LLVM_ABI DefaultFunctionPruningStrategy : public FunctionPruningStrategy {
  std::vector<NameFunctionSamples> SortedFunctions;

public:
  /// Construct a default pruning strategy for \p ProfileMap.
  ///
  /// @param ProfileMap Profile map that Erase() may modify.
  /// @param OutputSizeLimit Size limit in bytes of the output profile.
  DefaultFunctionPruningStrategy(SampleProfileMap &ProfileMap,
                                 size_t OutputSizeLimit);

  /// Erase coldest functions until the estimated output fits the size limit.
  ///
  /// In this default implementation, functions with fewest samples are dropped
  /// first. Since the exact size of the output cannot be easily calculated due
  /// to compression, we use a heuristic to remove as many functions as
  /// necessary but not too many, aiming to minimize the number of write
  /// iterations.
  /// Empirically, functions with larger total sample count contain linearly
  /// more sample entries, meaning it takes linearly more space to write them.
  /// The cumulative length is therefore quadratic if all functions are sorted
  /// by total sample count.
  /// TODO: Find better heuristic.
  ///
  /// @param CurrentOutputSize Number of bytes in the output if current profile
  /// map is written.
  void Erase(size_t CurrentOutputSize) override;
};

/// Sample-based profile writer. Base class.
class LLVM_ABI SampleProfileWriter {
public:
  /// Destroy the sample profile writer.
  virtual ~SampleProfileWriter() = default;

  /// Write sample profiles in \p S.
  ///
  /// @param S Function sample profile to write.
  /// \returns status code of the file update operation.
  virtual std::error_code writeSample(const FunctionSamples &S) = 0;

  /// Write all the sample profiles in the given map of samples.
  ///
  /// @param ProfileMap Map of function samples to write.
  /// \returns status code of the file update operation.
  virtual std::error_code write(const SampleProfileMap &ProfileMap);

  /// Write sample profiles up to given size limit, using the pruning strategy
  /// to drop some functions if necessary.
  ///
  /// @param ProfileMap Map of function samples to write; may be pruned in place.
  /// @param OutputSizeLimit Maximum output size in bytes.
  /// \returns status code of the file update operation.
  template <typename FunctionPruningStrategy = DefaultFunctionPruningStrategy>
  std::error_code writeWithSizeLimit(SampleProfileMap &ProfileMap,
                                     size_t OutputSizeLimit) {
    FunctionPruningStrategy Strategy(ProfileMap, OutputSizeLimit);
    return writeWithSizeLimitInternal(ProfileMap, OutputSizeLimit, &Strategy);
  }

  /// Return the output stream used by this writer.
  ///
  /// \returns Reference to the writer's output stream.
  raw_ostream &getOutputStream() { return *OutputStream; }

  /// Profile writer factory.
  ///
  /// Create a new file writer based on the value of \p Format.
  ///
  /// @param Filename Path of the profile file to create.
  /// @param Format Sample profile format to write.
  /// \returns Writer instance, or an error code on failure.
  static ErrorOr<std::unique_ptr<SampleProfileWriter>>
  create(StringRef Filename, SampleProfileFormat Format);

  /// Create a new stream writer based on the value of \p Format.
  ///
  /// For testing.
  ///
  /// @param OS Output stream that receives the profile.
  /// @param Format Sample profile format to write.
  /// \returns Writer instance, or an error code on failure.
  static ErrorOr<std::unique_ptr<SampleProfileWriter>>
  create(std::unique_ptr<raw_ostream> &OS, SampleProfileFormat Format);

  /// Set the optional profile symbol list to write.
  ///
  /// @param PSL Profile symbol list, or null if none.
  virtual void setProfileSymbolList(ProfileSymbolList *PSL) {}
  /// Request compression of all extensible-binary sections.
  virtual void setToCompressAllSections() {}
  /// Use MD5 hashes instead of strings in the name table.
  virtual void setUseMD5() {}
  /// Mark the written profile as a partial (shared-code) profile.
  virtual void setPartialProfile() {}
  /// Use the context-split section layout when writing.
  virtual void setUseCtxSplitLayout() {}
  /// Write the profile symbol list as MD5 hashes.
  virtual void setUseMD5ProfileSymbolList() {}
  /// Use MD5-indexed name and function-offset tables.
  virtual void setUseMD5IndexedTables() {}

  /// Set the profile format version to write.
  ///
  /// @param V Format version number; must be supported by this writer.
  void setFormatVersion(uint64_t V) {
    assert(sampleprof::formatVersionIsSupported(V) &&
           "Unsupported format version");
    FormatVersion = V;
  }
  /// Return the profile format version that will be written.
  ///
  /// \returns Format version number that will be written.
  uint64_t getFormatVersion() const { return FormatVersion; }

protected:
  /// Construct a writer that owns \p OS.
  ///
  /// @param OS Output stream that receives the profile.
  SampleProfileWriter(std::unique_ptr<raw_ostream> &OS)
      : OutputStream(std::move(OS)) {}

  /// Write a file header for the profile file.
  ///
  /// @param ProfileMap Map of function samples used to populate the header.
  /// \returns status code of the write operation.
  virtual std::error_code writeHeader(const SampleProfileMap &ProfileMap) = 0;

  /// Write function profiles to the profile file.
  ///
  /// @param ProfileMap Map of function samples to write.
  /// \returns status code of the write operation.
  virtual std::error_code writeFuncProfiles(const SampleProfileMap &ProfileMap);

  /// Write profiles under \p OutputSizeLimit using \p Strategy to prune.
  ///
  /// @param ProfileMap Map of function samples to write; may be pruned in place.
  /// @param OutputSizeLimit Maximum output size in bytes.
  /// @param Strategy Pruning strategy invoked when the size limit is exceeded.
  /// \returns status code of the write operation.
  std::error_code writeWithSizeLimitInternal(SampleProfileMap &ProfileMap,
                                             size_t OutputSizeLimit,
                                             FunctionPruningStrategy *Strategy);

  /// Newline count used to correct text-mode size estimates on Windows.
  ///
  /// For writeWithSizeLimit in text mode, each newline takes 1 additional byte
  /// on Windows when actually written to the file, but not written to a memory
  /// buffer. This needs to be accounted for when rewriting the profile.
  size_t LineCount;

  /// Output stream where to emit the profile to.
  std::unique_ptr<raw_ostream> OutputStream;

  /// Profile summary.
  std::unique_ptr<ProfileSummary> Summary;

  /// Compute summary for this profile.
  ///
  /// @param ProfileMap Map of function samples to summarize.
  void computeSummary(const SampleProfileMap &ProfileMap);

  /// Profile format.
  SampleProfileFormat Format = SPF_None;

  /// Format version to write.
  uint64_t FormatVersion = sampleprof::DefaultVersion;
};

/// Sample-based profile writer (text format).
class LLVM_ABI SampleProfileWriterText : public SampleProfileWriter {
public:
  /// Write sample profiles for function \p S in text format.
  ///
  /// @param S Function sample profile to write.
  /// \returns status code of the write operation.
  std::error_code writeSample(const FunctionSamples &S) override;

protected:
  /// Construct a text-format writer that owns \p OS.
  ///
  /// @param OS Output stream that receives the profile.
  SampleProfileWriterText(std::unique_ptr<raw_ostream> &OS)
      : SampleProfileWriter(OS) {}

  /// Write the text-format header and reset the line counter.
  ///
  /// @param ProfileMap Map of function samples (unused for text headers).
  /// \returns success.
  std::error_code writeHeader(const SampleProfileMap &ProfileMap) override {
    LineCount = 0;
    return sampleprof_error::success;
  }

  /// Mark flat functions with metadata when writing text profiles.
  void setUseCtxSplitLayout() override { MarkFlatProfiles = true; }

private:
  /// Indent level to use when writing.
  ///
  /// This is used when printing inlined callees.
  unsigned Indent = 0;

  /// If set, writes metadata "!Flat" to functions without inlined functions.
  /// This flag is for manual inspection only, it has no effect for the profile
  /// reader because a text sample profile is read sequentially and functions
  /// cannot be skipped.
  bool MarkFlatProfiles = false;

  LLVM_ABI friend ErrorOr<std::unique_ptr<SampleProfileWriter>>
  SampleProfileWriter::create(std::unique_ptr<raw_ostream> &OS,
                              SampleProfileFormat Format);
};

/// Sample-based profile writer (binary format).
class LLVM_ABI SampleProfileWriterBinary : public SampleProfileWriter {
public:
  /// Construct a binary-format writer that owns \p OS.
  ///
  /// @param OS Output stream that receives the profile.
  SampleProfileWriterBinary(std::unique_ptr<raw_ostream> &OS)
      : SampleProfileWriter(OS) {}

  /// Write sample profiles for function \p S in binary format.
  ///
  /// @param S Function sample profile to write.
  /// \returns status code of the write operation.
  std::error_code writeSample(const FunctionSamples &S) override;

protected:
  /// Return the name table used to encode function identifiers.
  ///
  /// \returns Mutable map from function id to its name-table index.
  virtual MapVector<FunctionId, uint32_t> &getNameTable() { return NameTable; }
  /// Write the magic identifier for \p Format.
  ///
  /// @param Format Sample profile format whose magic should be written.
  /// \returns status code of the write operation.
  virtual std::error_code writeMagicIdent(SampleProfileFormat Format);
  /// Write the name table section.
  ///
  /// \returns status code of the write operation.
  virtual std::error_code writeNameTable();
  /// Write the binary profile header for \p ProfileMap.
  ///
  /// @param ProfileMap Map of function samples used to populate the header.
  /// \returns status code of the write operation.
  std::error_code writeHeader(const SampleProfileMap &ProfileMap) override;
  /// Write the profile summary section.
  ///
  /// \returns status code of the write operation.
  std::error_code writeSummary();
  /// Write the name-table index for \p Context.
  ///
  /// @param Context Sample context whose index should be written.
  /// \returns status code of the write operation.
  virtual std::error_code writeContextIdx(const SampleContext &Context);
  /// Write the name-table index for function \p FName.
  ///
  /// @param FName Function identifier to look up in the name table.
  /// \returns status code of the write operation.
  std::error_code writeNameIdx(FunctionId FName);
  /// Write the body of function sample profile \p S.
  ///
  /// @param S Function sample profile body to write.
  /// \returns status code of the write operation.
  std::error_code writeBody(const FunctionSamples &S);

  /// Map from function id to its index in the binary name table.
  MapVector<FunctionId, uint32_t> NameTable;

  /// Ensure \p FName is present in the name table.
  ///
  /// @param FName Function identifier to insert if missing.
  void addName(FunctionId FName);
  /// Ensure names from \p Context are present in the name table.
  ///
  /// @param Context Sample context whose names should be recorded.
  virtual void addContext(const SampleContext &Context);
  /// Ensure all names referenced by \p S are present in the name table.
  ///
  /// @param S Function sample profile whose names should be recorded.
  void addNames(const FunctionSamples &S);

  /// Write \p CallsiteTypeMap to the output stream \p OS.
  ///
  /// @param CallsiteTypeMap Callsite vtable type profiles to write.
  /// @param OS Output stream that receives the serialized map.
  /// \returns status code of the write operation.
  std::error_code
  writeCallsiteVTableProf(const CallsiteTypeMap &CallsiteTypeMap,
                          raw_ostream &OS);

  /// Whether vtable profiles should be written for callsites.
  bool WriteVTableProf = false;

private:
  LLVM_ABI friend ErrorOr<std::unique_ptr<SampleProfileWriter>>
  SampleProfileWriter::create(std::unique_ptr<raw_ostream> &OS,
                              SampleProfileFormat Format);
};

/// Sample-based profile writer for the raw binary format.
class SampleProfileWriterRawBinary : public SampleProfileWriterBinary {
  using SampleProfileWriterBinary::SampleProfileWriterBinary;
};

/// Section header layouts for each extensible binary SectionLayout.
///
/// Note that SecFuncOffsetTable section is written after SecLBRProfile
/// in the profile, but is put before SecLBRProfile in SectionHdrLayout.
/// This is because sample reader follows the order in SectionHdrLayout
/// to read each section. To read function profiles on demand, sample
/// reader need to get the offset of each function profile first.
const std::array<SmallVector<SecHdrTableEntry, 8>, NumOfLayout>
    ExtBinaryHdrLayoutTable = {
        // DefaultLayout
        SmallVector<SecHdrTableEntry, 8>({{SecProfSummary, 0, 0, 0, 0},
                                          {SecNameTable, 0, 0, 0, 0},
                                          {SecCSNameTable, 0, 0, 0, 0},
                                          {SecFuncOffsetTable, 0, 0, 0, 0},
                                          {SecLBRProfile, 0, 0, 0, 0},
                                          {SecProfileSymbolList, 0, 0, 0, 0},
                                          {SecFuncMetadata, 0, 0, 0, 0}}),
        // CtxSplitLayout
        SmallVector<SecHdrTableEntry, 8>(
            {{SecProfSummary, 0, 0, 0, 0},
             {SecNameTable, 0, 0, 0, 0},
             // profile with inlined functions
             // for next two sections
             {SecFuncOffsetTable, 0, 0, 0, 0},
             {SecLBRProfile, 0, 0, 0, 0},
             // profile without inlined functions
             // for next two sections
             {SecFuncOffsetTable,
              static_cast<uint64_t>(SecCommonFlags::SecFlagFlat), 0, 0, 0},
             {SecLBRProfile, static_cast<uint64_t>(SecCommonFlags::SecFlagFlat),
              0, 0, 0},
             {SecProfileSymbolList, 0, 0, 0, 0},
             {SecFuncMetadata, 0, 0, 0, 0}}),
};

/// Base writer for the extensible binary sample profile format.
class LLVM_ABI SampleProfileWriterExtBinaryBase
    : public SampleProfileWriterBinary {
  using SampleProfileWriterBinary::SampleProfileWriterBinary;

public:
  /// Write all sample profiles in \p ProfileMap in extensible binary form.
  ///
  /// @param ProfileMap Map of function samples to write.
  /// \returns status code of the write operation.
  std::error_code write(const SampleProfileMap &ProfileMap) override;

  /// Request compression of every section in the extensible binary profile.
  void setToCompressAllSections() override;
  /// Request compression of section \p Type.
  ///
  /// @param Type Section type to mark for compression.
  void setToCompressSection(SecType Type);
  /// Write sample profiles for function \p S into the LBR profile section.
  ///
  /// @param S Function sample profile to write.
  /// \returns status code of the write operation.
  std::error_code writeSample(const FunctionSamples &S) override;

  /// Set to use MD5 to represent string in NameTable.
  void setUseMD5() override {
    UseMD5 = true;
    addSectionFlag(SecNameTable, SecNameTableFlags::SecFlagMD5Name);
    // MD5 will be stored as plain uint64_t instead of variable-length
    // quantity format in NameTable section.
    addSectionFlag(SecNameTable, SecNameTableFlags::SecFlagFixedLengthMD5);
  }

  /// Mark the profile as partial for shared or common code.
  ///
  /// Set the profile to be partial. It means the profile is for
  /// common/shared code. The common profile is usually merged from
  /// profiles collected from running other targets.
  void setPartialProfile() override {
    addSectionFlag(SecProfSummary, SecProfSummaryFlags::SecFlagPartial);
  }

  /// Set the profile symbol list to write.
  ///
  /// @param PSL Profile symbol list, or null if none.
  void setProfileSymbolList(ProfileSymbolList *PSL) override {
    ProfSymList = PSL;
  };

  /// Switch the writer to the context-split section layout.
  void setUseCtxSplitLayout() override {
    resetSecLayout(SectionLayout::CtxSplitLayout);
  }

  /// Write the profile symbol list using MD5 hashes.
  void setUseMD5ProfileSymbolList() override { UseMD5ProfSymList = true; }

  /// Use MD5-indexed name and function-offset tables.
  void setUseMD5IndexedTables() override { UseMD5IndexedTables = true; }

  /// Reset the section layout to \p SL and refresh the header layout table.
  ///
  /// @param SL Section layout to switch to.
  void resetSecLayout(SectionLayout SL) {
    verifySecLayout(SL);
#ifndef NDEBUG
    // Make sure resetSecLayout is called before any flag setting.
    for (auto &Entry : SectionHdrLayout) {
      assert(Entry.Flags == 0 &&
             "resetSecLayout has to be called before any flag setting");
    }
#endif
    SecLayout = SL;
    SectionHdrLayout = ExtBinaryHdrLayoutTable[SL];
  }

protected:
  /// Record the start offset of section \p Type at layout index \p LayoutIdx.
  ///
  /// @param Type Section type being started.
  /// @param LayoutIdx Index of the matching entry in SectionHdrLayout.
  /// \returns Byte offset where the section payload begins.
  uint64_t markSectionStart(SecType Type, uint32_t LayoutIdx);
  /// Finalize section \p Sec started at \p SectionStart into the header table.
  ///
  /// @param Sec Section type being finalized.
  /// @param LayoutIdx Index of the matching entry in SectionHdrLayout.
  /// @param SectionStart Byte offset where the section payload began.
  /// \returns status code of the write operation.
  std::error_code addNewSection(SecType Sec, uint32_t LayoutIdx,
                                uint64_t SectionStart);
  /// Add \p Flag to every SectionHdrLayout entry of type \p Type.
  ///
  /// @param Type Section type whose flags should be updated.
  /// @param Flag Section flag bit to set.
  template <class SecFlagType>
  void addSectionFlag(SecType Type, SecFlagType Flag) {
    for (auto &Entry : SectionHdrLayout) {
      if (Entry.Type == Type)
        addSecFlag(Entry, Flag);
    }
  }
  /// Ensure names from \p Context are present for extensible binary writing.
  ///
  /// @param Context Sample context whose names should be recorded.
  void addContext(const SampleContext &Context) override;

  /// Write a format-specific custom section of type \p Type.
  ///
  /// Placeholder for subclasses to dispatch their own section writers.
  ///
  /// @param Type Custom section type to write.
  /// \returns status code of the write operation.
  virtual std::error_code writeCustomSection(SecType Type) = 0;
  /// Verify that section layout \p SL is supported by this format.
  ///
  /// @param SL Section layout to validate.
  virtual void verifySecLayout(SectionLayout SL) = 0;

  /// Write all sections for \p ProfileMap in the layout-defined order.
  ///
  /// @param ProfileMap Map of function samples to write.
  /// \returns status code of the write operation.
  virtual std::error_code writeSections(const SampleProfileMap &ProfileMap) = 0;

  /// Find the first unwritten SectionHdrLayout entry matching \p Type.
  ///
  /// @param Type Section type to locate.
  /// \returns Layout index of the first matching unwritten entry.
  unsigned findUnwrittenEntry(SecType Type);

  /// Dispatch the section writer for section \p Type.
  ///
  /// @param Type Section type to write.
  /// @param ProfileMap Map of function samples available to section writers.
  /// \returns status code of the write operation.
  virtual std::error_code writeOneSection(SecType Type,
                                          const SampleProfileMap &ProfileMap);

  /// Write the name table for the extensible binary format.
  ///
  /// \returns status code of the write operation.
  std::error_code writeNameTable() override;
  /// Write the context index for \p Context.
  ///
  /// @param Context Sample context whose index should be written.
  /// \returns status code of the write operation.
  std::error_code writeContextIdx(const SampleContext &Context) override;
  /// Write the CS name-table index for \p Context.
  ///
  /// @param Context Sample context whose CS name index should be written.
  /// \returns status code of the write operation.
  std::error_code writeCSNameIdx(const SampleContext &Context);
  /// Write the context-sensitive name table section.
  ///
  /// \returns status code of the write operation.
  std::error_code writeCSNameTableSection();

  /// Write function metadata for every profile in \p Profiles.
  ///
  /// @param Profiles Map of function samples whose metadata should be written.
  /// \returns status code of the write operation.
  std::error_code writeFuncMetadata(const SampleProfileMap &Profiles);
  /// Write function metadata for a single profile \p Profile.
  ///
  /// @param Profile Function sample profile whose metadata should be written.
  /// \returns status code of the write operation.
  std::error_code writeFuncMetadata(const FunctionSamples &Profile);

  /// Write the name table section for \p ProfileMap.
  ///
  /// @param ProfileMap Map of function samples whose names should be written.
  /// \returns status code of the write operation.
  std::error_code writeNameTableSection(const SampleProfileMap &ProfileMap);
  /// Write the name table section in Eytzinger layout for \p ProfileMap.
  ///
  /// @param ProfileMap Map of function samples whose names should be written.
  /// \returns status code of the write operation.
  std::error_code
  writeEytzingerNameTableSection(const SampleProfileMap &ProfileMap);
  /// Write the function offset table.
  ///
  /// @param IsNested True to write nested-context offsets; false for flat.
  /// \returns status code of the write operation.
  std::error_code writeFuncOffsetTable(bool IsNested);
  /// Write the function offset table in Eytzinger layout.
  ///
  /// @param IsNested True to write nested-context offsets; false for flat.
  /// \returns status code of the write operation.
  std::error_code writeEytzingerFuncOffsetTable(bool IsNested);
  /// Write the legacy (non-Eytzinger) function offset table.
  ///
  /// \returns status code of the write operation.
  std::error_code writeLegacyFuncOffsetTable();
  /// Write the profile symbol list section.
  ///
  /// \returns status code of the write operation.
  std::error_code writeProfileSymbolListSection();
  /// Write the profile symbol list as strings.
  ///
  /// \returns status code of the write operation.
  std::error_code writeStringBasedProfileSymbolListSection();
  /// Write the profile symbol list as MD5 hashes.
  ///
  /// \returns status code of the write operation.
  std::error_code writeMD5ProfileSymbolListSection();

  /// Active section layout used when writing the extensible binary profile.
  SectionLayout SecLayout = DefaultLayout;
  /// Ordered section header layout used by the reader and writer.
  ///
  /// Specify the order of sections in section header table. Note
  /// the order of sections in SecHdrTable may be different that the
  /// order in SectionHdrLayout. sample Reader will follow the order
  /// in SectionHdrLayout to read each section.
  SmallVector<SecHdrTableEntry, 8> SectionHdrLayout =
      ExtBinaryHdrLayoutTable[DefaultLayout];

  /// Start offset of the SecLBRProfile section in the output stream.
  ///
  /// Save the start of SecLBRProfile so we can compute the offset to the
  /// start of SecLBRProfile for each Function's Profile and will keep it
  /// in FuncOffsetTable.
  uint64_t SecLBRProfileStart = 0;

private:
  void allocSecHdrTable();
  std::error_code writeSecHdrTable();
  std::error_code writeHeader(const SampleProfileMap &ProfileMap) override;
  std::error_code compressAndOutput();

  // We will swap the raw_ostream held by LocalBufStream and that
  // held by OutputStream if we try to add a section which needs
  // compression. After the swap, all the data written to output
  // will be temporarily buffered into the underlying raw_string_ostream
  // originally held by LocalBufStream. After the data writing for the
  // section is completed, compress the data in the local buffer,
  // swap the raw_ostream back and write the compressed data to the
  // real output.
  std::unique_ptr<raw_ostream> LocalBufStream;
  // The location where the output stream starts.
  uint64_t FileStart;
  // The location in the output stream where the SecHdrTable should be
  // written to.
  uint64_t SecHdrTableOffset;
  // The table contains SecHdrTableEntry entries in order of how they are
  // populated in the writer. It may be different from the order in
  // SectionHdrLayout which specifies the sequence in which sections will
  // be read.
  std::vector<SecHdrTableEntry> SecHdrTable;

  // FuncOffsetTable maps function context to its profile offset in
  // SecLBRProfile section. It is used to load function profile on demand.
  MapVector<SampleContext, uint64_t> FuncOffsetTable;
  // Whether to use MD5 to represent string.
  bool UseMD5 = false;
  // Whether to write the profile symbol list as 64-bit MD5 hashes in Eytzinger
  // layout.
  bool UseMD5ProfSymList = false;
  // Whether to write MD5-based indexed NameTable and parallel FuncOffsetTable
  // in Eytzinger layout.
  bool UseMD5IndexedTables = false;
  size_t NumNested = 0;
  size_t NumFlat = 0;

  /// CSNameTable maps function context to its offset in SecCSNameTable section.
  /// The offset will be used everywhere where the context is referenced.
  MapVector<SampleContext, uint32_t> CSNameTable;

  ProfileSymbolList *ProfSymList = nullptr;
};

/// Writer for the standard extensible binary sample profile format.
class LLVM_ABI SampleProfileWriterExtBinary
    : public SampleProfileWriterExtBinaryBase {
public:
  /// Construct an extensible binary writer that owns \p OS.
  ///
  /// @param OS Output stream that receives the profile.
  SampleProfileWriterExtBinary(std::unique_ptr<raw_ostream> &OS);

private:
  std::error_code writeDefaultLayout(const SampleProfileMap &ProfileMap);
  std::error_code writeCtxSplitLayout(const SampleProfileMap &ProfileMap);

  std::error_code writeSections(const SampleProfileMap &ProfileMap) override;

  std::error_code writeCustomSection(SecType Type) override {
    return sampleprof_error::success;
  };

  void verifySecLayout(SectionLayout SL) override {
    assert((SL == DefaultLayout || SL == CtxSplitLayout) &&
           "Unsupported layout");
  }
};

} // end namespace sampleprof
} // end namespace llvm

#endif // LLVM_PROFILEDATA_SAMPLEPROFWRITER_H
