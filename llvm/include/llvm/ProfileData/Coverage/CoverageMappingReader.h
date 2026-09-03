//===- CoverageMappingReader.h - Code coverage mapping reader ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for reading coverage mapping data for
// instrumentation based coverage.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_COVERAGE_COVERAGEMAPPINGREADER_H
#define LLVM_PROFILEDATA_COVERAGE_COVERAGEMAPPINGREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <vector>

namespace llvm {
namespace coverage {

class CoverageMappingReader;

/// Coverage mapping information for a single function.
struct CoverageMappingRecord {
  /// Name of the function described by this record.
  StringRef FunctionName;
  /// Hash that uniquely identifies the function's coverage mapping.
  uint64_t FunctionHash;
  /// Filenames referenced by the mapping regions.
  ArrayRef<StringRef> Filenames;
  /// Counter expressions used by the mapping regions.
  ArrayRef<CounterExpression> Expressions;
  /// Mapping regions that associate counters with source ranges.
  ArrayRef<CounterMappingRegion> MappingRegions;
};

/// A file format agnostic iterator over coverage mapping data.
class CoverageMappingIterator {
  CoverageMappingReader *Reader;
  CoverageMappingRecord Record;
  coveragemap_error ReadErr;

  LLVM_ABI void increment();

public:
  /// Input iterator category tag.
  using iterator_category = std::input_iterator_tag;
  /// Type of the coverage mapping record yielded by the iterator.
  using value_type = CoverageMappingRecord;
  /// Distance type between iterators (unused for input iterators).
  using difference_type = std::ptrdiff_t;
  /// Pointer to a coverage mapping record.
  using pointer = value_type *;
  /// Reference to a coverage mapping record.
  using reference = value_type &;

  /// Construct an empty end iterator.
  CoverageMappingIterator()
      : Reader(nullptr), ReadErr(coveragemap_error::success) {}

  /// Construct an iterator that begins reading from \p Reader.
  /// \param Reader Coverage mapping reader that supplies successive records.
  CoverageMappingIterator(CoverageMappingReader *Reader)
      : Reader(Reader), ReadErr(coveragemap_error::success) {
    increment();
  }

  /// Destroy the iterator, asserting that no unread error remains.
  ~CoverageMappingIterator() {
    if (ReadErr != coveragemap_error::success)
      llvm_unreachable("Unexpected error in coverage mapping iterator");
  }

  /// Advance to the next coverage mapping record.
  /// \return Reference to this iterator after advancing.
  CoverageMappingIterator &operator++() {
    increment();
    return *this;
  }
  /// Return true if this iterator and \p RHS refer to the same reader.
  /// \param RHS Other iterator to compare.
  /// \return True if both iterators refer to the same reader.
  bool operator==(const CoverageMappingIterator &RHS) const {
    return Reader == RHS.Reader;
  }
  /// Return true if this iterator and \p RHS refer to different readers.
  /// \param RHS Other iterator to compare.
  /// \return True if the iterators refer to different readers.
  bool operator!=(const CoverageMappingIterator &RHS) const {
    return Reader != RHS.Reader;
  }
  /// Return a reference to the current coverage mapping record.
  /// \return The current record, or an error if the last read failed.
  Expected<CoverageMappingRecord &> operator*() {
    if (ReadErr != coveragemap_error::success) {
      auto E = make_error<CoverageMapError>(ReadErr);
      ReadErr = coveragemap_error::success;
      return std::move(E);
    }
    return Record;
  }
  /// Return a pointer to the current coverage mapping record.
  /// \return Pointer to the current record, or an error if the last read failed.
  Expected<CoverageMappingRecord *> operator->() {
    if (ReadErr != coveragemap_error::success) {
      auto E = make_error<CoverageMapError>(ReadErr);
      ReadErr = coveragemap_error::success;
      return std::move(E);
    }
    return &Record;
  }
};

/// Interface for reading coverage mapping records from any supported format.
class CoverageMappingReader {
public:
  /// Destroy the coverage mapping reader.
  virtual ~CoverageMappingReader() = default;

  /// Read the next coverage mapping record into \p Record.
  /// \param Record Output parameter set to the next coverage mapping record.
  /// \return Success, or an error if the next record cannot be read.
  virtual Error readNextRecord(CoverageMappingRecord &Record) = 0;
  /// Return an iterator positioned at the first coverage mapping record.
  /// \return Iterator positioned at the first coverage mapping record.
  CoverageMappingIterator begin() { return CoverageMappingIterator(this); }
  /// Return an end iterator for the coverage mapping records.
  /// \return Past-the-end iterator for the coverage mapping records.
  CoverageMappingIterator end() { return CoverageMappingIterator(); }
};

/// Base class for the raw coverage mapping and filenames data readers.
class RawCoverageReader {
protected:
  /// Remaining unparsed bytes of the raw coverage data.
  StringRef Data;

  /// Construct a reader over the raw coverage bytes in \p Data.
  /// \param Data Buffer of encoded coverage mapping or filename data.
  RawCoverageReader(StringRef Data) : Data(Data) {}

  /// Decode a ULEB128 integer from the data stream into \p Result.
  /// \param Result Output parameter set to the decoded value.
  /// \return Success, or an error if the ULEB128 cannot be decoded.
  LLVM_ABI Error readULEB128(uint64_t &Result);
  /// Decode an integer that must be strictly less than \p MaxPlus1.
  /// \param Result Output parameter set to the decoded value.
  /// \param MaxPlus1 Exclusive upper bound; values >= this are errors.
  /// \return Success, or an error if the value is missing or out of range.
  LLVM_ABI Error readIntMax(uint64_t &Result, uint64_t MaxPlus1);
  /// Decode a size value that must not exceed the remaining data length.
  /// \param Result Output parameter set to the decoded size.
  /// \return Success, or an error if the size is invalid or exceeds the data.
  LLVM_ABI Error readSize(uint64_t &Result);
  /// Decode a length-prefixed string from the data stream into \p Result.
  /// \param Result Output parameter set to the decoded string.
  /// \return Success, or an error if the string cannot be decoded.
  LLVM_ABI Error readString(StringRef &Result);
};

/// Checks if the given coverage mapping data is exported for
/// an unused function.
class RawCoverageMappingDummyChecker : public RawCoverageReader {
public:
  /// Construct a dummy checker over the mapping data in \p MappingData.
  /// \param MappingData Raw coverage mapping bytes to inspect.
  RawCoverageMappingDummyChecker(StringRef MappingData)
      : RawCoverageReader(MappingData) {}

  /// Return true if the mapping data is a dummy record for an unused function.
  /// \return True if the mapping is a dummy unused-function record, or an error.
  LLVM_ABI Expected<bool> isDummy();
};

/// Reader for the raw coverage mapping data.
class RawCoverageMappingReader : public RawCoverageReader {
  ArrayRef<std::string> &TranslationUnitFilenames;
  std::vector<StringRef> &Filenames;
  std::vector<CounterExpression> &Expressions;
  std::vector<CounterMappingRegion> &MappingRegions;

public:
  /// Construct a reader that decodes mapping data into the given outputs.
  /// \param MappingData Raw coverage mapping bytes to decode.
  /// \param TranslationUnitFilenames Filenames for the translation unit.
  /// \param Filenames Output vector filled with filenames used by the mapping.
  /// \param Expressions Output vector filled with decoded counter expressions.
  /// \param MappingRegions Output vector filled with decoded mapping regions.
  RawCoverageMappingReader(StringRef MappingData,
                           ArrayRef<std::string> &TranslationUnitFilenames,
                           std::vector<StringRef> &Filenames,
                           std::vector<CounterExpression> &Expressions,
                           std::vector<CounterMappingRegion> &MappingRegions)
      : RawCoverageReader(MappingData),
        TranslationUnitFilenames(TranslationUnitFilenames),
        Filenames(Filenames), Expressions(Expressions),
        MappingRegions(MappingRegions) {}
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  RawCoverageMappingReader(const RawCoverageMappingReader &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  RawCoverageMappingReader &
  operator=(const RawCoverageMappingReader &Other) = delete;

  /// Decode the raw coverage mapping data into the bound output containers.
  /// \return Success, or an error if the mapping data cannot be decoded.
  LLVM_ABI Error read();

private:
  Error decodeCounter(unsigned Value, Counter &C);
  Error readCounter(Counter &C);
  Error
  readMappingRegionsSubArray(std::vector<CounterMappingRegion> &MappingRegions,
                             unsigned InferredFileID, size_t NumFileIDs);
};

/// Reader for the coverage mapping data that is emitted by the
/// frontend and stored in an object file.
class LLVM_ABI BinaryCoverageReader : public CoverageMappingReader {
public:
  /// Coverage mapping payload for one function within a binary object.
  struct ProfileMappingRecord {
    /// Coverage mapping format version for this record.
    CovMapVersion Version;
    /// Name of the function described by this record.
    StringRef FunctionName;
    /// Hash that uniquely identifies the function's coverage mapping.
    uint64_t FunctionHash;
    /// Encoded coverage mapping bytes for the function.
    StringRef CoverageMapping;
    /// Index of the first filename associated with this function.
    size_t FilenamesBegin;
    /// Number of filenames associated with this function.
    size_t FilenamesSize;

    /// Construct a profile mapping record from the given field values.
    /// \param Version Coverage mapping format version.
    /// \param FunctionName Name of the covered function.
    /// \param FunctionHash Hash of the function's coverage mapping.
    /// \param CoverageMapping Encoded coverage mapping payload.
    /// \param FilenamesBegin Index of the first associated filename.
    /// \param FilenamesSize Number of associated filenames.
    ProfileMappingRecord(CovMapVersion Version, StringRef FunctionName,
                         uint64_t FunctionHash, StringRef CoverageMapping,
                         size_t FilenamesBegin, size_t FilenamesSize)
        : Version(Version), FunctionName(FunctionName),
          FunctionHash(FunctionHash), CoverageMapping(CoverageMapping),
          FilenamesBegin(FilenamesBegin), FilenamesSize(FilenamesSize) {}
  };

  /// Ownership of the function records buffer tied to this reader.
  using FuncRecordsStorage = std::unique_ptr<MemoryBuffer>;
  /// Ownership of an optional coverage-map copy tied to this reader.
  using CoverageMapCopyStorage = std::unique_ptr<MemoryBuffer>;

private:
  std::vector<std::string> Filenames;
  std::vector<ProfileMappingRecord> MappingRecords;
  std::unique_ptr<InstrProfSymtab> ProfileNames;
  size_t CurrentRecord = 0;
  std::vector<StringRef> FunctionsFilenames;
  std::vector<CounterExpression> Expressions;
  std::vector<CounterMappingRegion> MappingRegions;

  // Used to tie the lifetimes of coverage function records to the lifetime of
  // this BinaryCoverageReader instance. Needed to support the format change in
  // D69471, which can split up function records into multiple sections on ELF.
  FuncRecordsStorage FuncRecords;

  // Used to tie the lifetimes of an optional copy of the coverage mapping data
  // to the lifetime of this BinaryCoverageReader instance. Needed to support
  // Wasm object format, which might require realignment of section contents.
  CoverageMapCopyStorage CoverageMapCopy;

  BinaryCoverageReader(std::unique_ptr<InstrProfSymtab> Symtab,
                       FuncRecordsStorage &&FuncRecords,
                       CoverageMapCopyStorage &&CoverageMapCopy)
      : ProfileNames(std::move(Symtab)), FuncRecords(std::move(FuncRecords)),
        CoverageMapCopy(std::move(CoverageMapCopy)) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  BinaryCoverageReader(const BinaryCoverageReader &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  BinaryCoverageReader &operator=(const BinaryCoverageReader &Other) = delete;

  /// Create readers for coverage mapping data embedded in \p ObjectBuffer.
  /// \param ObjectBuffer Memory buffer containing the object file.
  /// \param Arch Target architecture to select when the object is universal.
  /// \param ObjectFileBuffers Storage that retains buffers owned by the readers.
  /// \param CompilationDir Compilation directory used to resolve relative paths.
  /// \param BinaryIDs Optional output list of build IDs found in the object.
  /// \return Readers for each coverage mapping in the object, or an error.
  static Expected<std::vector<std::unique_ptr<BinaryCoverageReader>>>
  create(MemoryBufferRef ObjectBuffer, StringRef Arch,
         SmallVectorImpl<std::unique_ptr<MemoryBuffer>> &ObjectFileBuffers,
         StringRef CompilationDir = "",
         SmallVectorImpl<object::BuildIDRef> *BinaryIDs = nullptr);

  /// Create a reader from already-extracted coverage mapping buffers.
  /// \param Coverage Coverage mapping section contents.
  /// \param FuncRecords Storage owning the function records buffer.
  /// \param CoverageMap Storage owning an optional coverage-map copy.
  /// \param ProfileNamesPtr Symbol table of profiled function names.
  /// \param BytesInAddress Size in bytes of a target address.
  /// \param Endian Endianness of the coverage mapping data.
  /// \param CompilationDir Compilation directory used to resolve relative paths.
  /// \return A binary coverage reader, or an error if creation fails.
  static Expected<std::unique_ptr<BinaryCoverageReader>>
  createCoverageReaderFromBuffer(
      StringRef Coverage, FuncRecordsStorage &&FuncRecords,
      CoverageMapCopyStorage &&CoverageMap,
      std::unique_ptr<InstrProfSymtab> ProfileNamesPtr, uint8_t BytesInAddress,
      llvm::endianness Endian, StringRef CompilationDir = "");

  /// Read the next coverage mapping record into \p Record.
  /// \param Record Output parameter set to the next coverage mapping record.
  /// \return Success, or an error if the next record cannot be read.
  Error readNextRecord(CoverageMappingRecord &Record) override;
};

/// Reader for the raw coverage filenames.
class RawCoverageFilenamesReader : public RawCoverageReader {
  std::vector<std::string> &Filenames;
  StringRef CompilationDir;

  // Read an uncompressed sequence of filenames.
  Error readUncompressed(CovMapVersion Version, uint64_t NumFilenames);

public:
  /// Construct a reader that decodes filenames into \p Filenames.
  /// \param Data Raw coverage filenames section contents.
  /// \param Filenames Output vector filled with the decoded filenames.
  /// \param CompilationDir Compilation directory used to resolve relative paths.
  RawCoverageFilenamesReader(StringRef Data,
                             std::vector<std::string> &Filenames,
                             StringRef CompilationDir = "")
      : RawCoverageReader(Data), Filenames(Filenames),
        CompilationDir(CompilationDir) {}
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  RawCoverageFilenamesReader(const RawCoverageFilenamesReader &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  RawCoverageFilenamesReader &
  operator=(const RawCoverageFilenamesReader &Other) = delete;

  /// Decode the filenames for coverage mapping format \p Version.
  /// \param Version Coverage mapping format version of the filenames payload.
  /// \return Success, or an error if the filenames cannot be decoded.
  LLVM_ABI Error read(CovMapVersion Version);
};

} // end namespace coverage
} // end namespace llvm

#endif // LLVM_PROFILEDATA_COVERAGE_COVERAGEMAPPINGREADER_H
