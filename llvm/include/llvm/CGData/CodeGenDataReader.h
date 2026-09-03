//===- CodeGenDataReader.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for reading codegen data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CGDATA_CODEGENDATAREADER_H
#define LLVM_CGDATA_CODEGENDATAREADER_H

#include "llvm/CGData/CodeGenData.h"
#include "llvm/CGData/OutlinedHashTreeRecord.h"
#include "llvm/CGData/StableFunctionMapRecord.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/VirtualFileSystem.h"

namespace llvm {

/// Abstract base class for reading codegen data from binary or text sources.
class CodeGenDataReader {
  cgdata_error LastError = cgdata_error::success;
  std::string LastErrorMsg;

public:
  /// Construct an empty codegen data reader.
  CodeGenDataReader() = default;
  /// Destroy the codegen data reader.
  virtual ~CodeGenDataReader() = default;

  /// Read the header.  Required before reading first record.
  ///
  /// \return Success, or an error if reading the header fails.
  virtual Error read() = 0;
  /// Return the codegen data version.
  ///
  /// \return The codegen data version.
  virtual uint32_t getVersion() const = 0;
  /// Return the codegen data kind.
  ///
  /// \return The codegen data kind.
  virtual CGDataKind getDataKind() const = 0;
  /// Return true if the data has an outlined hash tree.
  ///
  /// \return true if the data has an outlined hash tree.
  virtual bool hasOutlinedHashTree() const = 0;
  /// Return true if the data has a stable function map.
  ///
  /// \return true if the data has a stable function map.
  virtual bool hasStableFunctionMap() const = 0;
  /// Return the outlined hash tree that is released from the reader.
  ///
  /// \return Ownership of the outlined hash tree previously held by the reader.
  std::unique_ptr<OutlinedHashTree> releaseOutlinedHashTree() {
    return std::move(HashTreeRecord.HashTree);
  }
  /// Return the stable function map that is released from the reader.
  ///
  /// \return Ownership of the stable function map previously held by the
  /// reader.
  std::unique_ptr<StableFunctionMap> releaseStableFunctionMap() {
    return std::move(FunctionMapRecord.FunctionMap);
  }

  /// Factory method to create an appropriately typed reader for the given
  /// codegen data file path and file system.
  ///
  /// \param Path Path to the codegen data file.
  /// \param FS File system used to open \p Path.
  /// \return A reader for \p Path, or an error if opening or recognizing the
  /// format fails.
  LLVM_ABI static Expected<std::unique_ptr<CodeGenDataReader>>
  create(const Twine &Path, vfs::FileSystem &FS);

  /// Factory method to create an appropriately typed reader for the given
  /// memory buffer.
  ///
  /// \param Buffer Memory buffer holding the codegen data contents.
  /// \return A reader for \p Buffer, or an error if the format is unrecognized.
  LLVM_ABI static Expected<std::unique_ptr<CodeGenDataReader>>
  create(std::unique_ptr<MemoryBuffer> Buffer);

  /// Extract cgdata from object-file sections and merge into global records.
  ///
  /// This is a static helper that is used by `llvm-cgdata --merge` or
  /// ThinLTO's two-codegen rounds. Optionally, \p CombinedHash can be used to
  /// compute the combined hash of the merged data.
  ///
  /// \param Obj Object file whose cgdata sections are extracted.
  /// \param GlobalOutlineRecord Global outlined hash tree record to merge into.
  /// \param GlobalFunctionMapRecord Global stable function map record to merge
  /// into.
  /// \param CombinedHash Optional output for the combined hash of merged data.
  /// \return Success, or an error if extracting or merging fails.
  LLVM_ABI static Error
  mergeFromObjectFile(const object::ObjectFile *Obj,
                      OutlinedHashTreeRecord &GlobalOutlineRecord,
                      StableFunctionMapRecord &GlobalFunctionMapRecord,
                      stable_hash *CombinedHash = nullptr);

protected:
  /// The outlined hash tree that has been read. When it's released by
  /// releaseOutlinedHashTree(), it's no longer valid.
  OutlinedHashTreeRecord HashTreeRecord;

  /// The stable function map that has been read. When it's released by
  // releaseStableFunctionMap(), it's no longer valid.
  StableFunctionMapRecord FunctionMapRecord;

  /// Set the current error and return same.
  ///
  /// \param Err Codegen-data error code to record.
  /// \param ErrMsg Optional detail message associated with \p Err.
  /// \return Success if \p Err is success, otherwise a CGDataError for \p Err.
  Error error(cgdata_error Err, const std::string &ErrMsg = "") {
    LastError = Err;
    LastErrorMsg = ErrMsg;
    if (Err == cgdata_error::success)
      return Error::success();
    return make_error<CGDataError>(Err, ErrMsg);
  }

  /// Record a CGDataError from \p E and return a matching error.
  ///
  /// \param E Error to consume; expected to contain a CGDataError.
  /// \return A CGDataError matching the recorded error state.
  Error error(Error &&E) {
    handleAllErrors(std::move(E), [&](const CGDataError &IPE) {
      LastError = IPE.get();
      LastErrorMsg = IPE.getMessage();
    });
    return make_error<CGDataError>(LastError, LastErrorMsg);
  }

  /// Clear the current error and return a successful one.
  ///
  /// \return A successful Error.
  Error success() { return error(cgdata_error::success); }
};

/// Command-line option enabling lazy loading of indexed codegen data.
LLVM_ABI extern cl::opt<bool> IndexedCodeGenDataLazyLoading;

/// Reader for binary (indexed) codegen data.
class LLVM_ABI IndexedCodeGenDataReader : public CodeGenDataReader {
  /// The codegen data file contents.
  std::unique_ptr<MemoryBuffer> DataBuffer;
  /// The header
  IndexedCGData::Header Header;

public:
  /// Construct a reader over the given binary codegen data buffer.
  ///
  /// \param DataBuffer Memory buffer holding the indexed codegen data.
  IndexedCodeGenDataReader(std::unique_ptr<MemoryBuffer> DataBuffer)
      : DataBuffer(std::move(DataBuffer)) {}
  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copying is not allowed.
  IndexedCodeGenDataReader(const IndexedCodeGenDataReader &Other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param Other Unused; copy assignment is not supported.
  IndexedCodeGenDataReader &
  operator=(const IndexedCodeGenDataReader &Other) = delete;

  /// Return true if the given buffer is in binary codegen data format.
  ///
  /// \param Buffer Memory buffer to inspect for the binary format.
  /// \return true if the buffer is in binary codegen data format.
  static bool hasFormat(const MemoryBuffer &Buffer);
  /// Read the contents including the header.
  ///
  /// \return Success, or an error if reading the binary data fails.
  Error read() override;
  /// Return the codegen data version.
  ///
  /// \return The codegen data version.
  uint32_t getVersion() const override { return Header.Version; }
  /// Return the codegen data kind.
  ///
  /// \return The codegen data kind.
  CGDataKind getDataKind() const override {
    return static_cast<CGDataKind>(Header.DataKind);
  }
  /// Return true if the header indicates the data has an outlined hash tree.
  /// This does not mean that the data is still available.
  ///
  /// \return true if the header indicates the data has an outlined hash tree.
  bool hasOutlinedHashTree() const override {
    return Header.DataKind &
           static_cast<uint32_t>(CGDataKind::FunctionOutlinedHashTree);
  }
  /// Return true if the header indicates the data has a stable function map.
  ///
  /// \return true if the header indicates the data has a stable function map.
  bool hasStableFunctionMap() const override {
    return Header.DataKind &
           static_cast<uint32_t>(CGDataKind::StableFunctionMergingMap);
  }
};

/// Reader for text codegen data suitable for test inputs.
///
/// The header is a custom format starting with `:` per line to indicate which
/// codegen data is recorded. `#` is used to indicate a comment.
/// The subsequent data is a YAML format per each codegen data in order.
/// Currently, it only has a function outlined hash tree.
class LLVM_ABI TextCodeGenDataReader : public CodeGenDataReader {
  /// The codegen data file contents.
  std::unique_ptr<MemoryBuffer> DataBuffer;
  /// Iterator over the profile data.
  line_iterator Line;
  /// Describe the kind of the codegen data.
  CGDataKind DataKind = CGDataKind::Unknown;

public:
  /// Construct a reader over the given text codegen data buffer.
  ///
  /// \param DataBuffer_ Memory buffer holding the text codegen data.
  TextCodeGenDataReader(std::unique_ptr<MemoryBuffer> DataBuffer_)
      : DataBuffer(std::move(DataBuffer_)), Line(*DataBuffer, true, '#') {}
  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copying is not allowed.
  TextCodeGenDataReader(const TextCodeGenDataReader &Other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param Other Unused; copy assignment is not supported.
  TextCodeGenDataReader &operator=(const TextCodeGenDataReader &Other) = delete;

  /// Return true if the given buffer is in text codegen data format.
  ///
  /// \param Buffer Memory buffer to inspect for the text format.
  /// \return true if the buffer is in text codegen data format.
  static bool hasFormat(const MemoryBuffer &Buffer);
  /// Read the contents including the header.
  ///
  /// \return Success, or an error if reading the text data fails.
  Error read() override;
  /// Text format does not have version, so return 0.
  ///
  /// \return 0, since the text format has no version.
  uint32_t getVersion() const override { return 0; }
  /// Return the codegen data kind.
  ///
  /// \return The codegen data kind.
  CGDataKind getDataKind() const override { return DataKind; }
  /// Return true if the header indicates the data has an outlined hash tree.
  /// This does not mean that the data is still available.
  ///
  /// \return true if the header indicates the data has an outlined hash tree.
  bool hasOutlinedHashTree() const override {
    return static_cast<uint32_t>(DataKind) &
           static_cast<uint32_t>(CGDataKind::FunctionOutlinedHashTree);
  }
  /// Return true if the header indicates the data has a stable function map.
  /// This does not mean that the data is still available.
  ///
  /// \return true if the header indicates the data has a stable function map.
  bool hasStableFunctionMap() const override {
    return static_cast<uint32_t>(DataKind) &
           static_cast<uint32_t>(CGDataKind::StableFunctionMergingMap);
  }
};

} // end namespace llvm

#endif // LLVM_CGDATA_CODEGENDATAREADER_H
