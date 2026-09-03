//===-- llvm/Remarks/Remark.h - The remark type -----------------*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an interface for parsing remarks in LLVM.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_REMARKPARSER_H
#define LLVM_REMARKS_REMARKPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Remarks/RemarkFormat.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <optional>

namespace llvm {
namespace remarks {

struct Remark;

/// Error indicating that remark parsing has reached the end of the input.
class EndOfFileError : public ErrorInfo<EndOfFileError> {
public:
  /// Unique ErrorInfo RTTI key for EndOfFileError.
  LLVM_ABI static char ID;

  /// Construct an end-of-file error.
  EndOfFileError() = default;

  /// Write this error's message to \p OS.
  ///
  /// \param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override { OS << "End of file reached."; }
  /// Convert this error to a std::error_code.
  ///
  /// End-of-file has no corresponding std::error_code; returns
  /// inconvertibleErrorCode().
  ///
  /// \return An inconvertible error code.
  std::error_code convertToErrorCode() const override {
    return inconvertibleErrorCode();
  }
};

/// Parser used to parse a raw buffer to remarks::Remark objects.
struct RemarkParser {
  /// The format of the parser.
  Format ParserFormat;
  /// Path to prepend when opening an external remark file.
  std::string ExternalFilePrependPath;

  /// Construct a remark parser for the given \p ParserFormat.
  ///
  /// \param ParserFormat Remark serialization format this parser handles.
  RemarkParser(Format ParserFormat) : ParserFormat(ParserFormat) {}

  /// Return the next parsed remark, or an error.
  ///
  /// If no error occurs, this returns a valid Remark object.
  /// If an error of type EndOfFileError occurs, it is safe to recover from it
  /// by stopping the parsing.
  /// If any other error occurs, it should be propagated to the user.
  /// The pointer should never be null.
  ///
  /// \return The next remark, or an error (including EndOfFileError).
  virtual Expected<std::unique_ptr<Remark>> next() = 0;

  /// Destroy the remark parser.
  virtual ~RemarkParser() = default;
};

/// In-memory representation of the string table parsed from a buffer (e.g. the
/// remarks section).
struct ParsedStringTable {
  /// The buffer mapped from the section contents.
  StringRef Buffer;
  /// This object has high changes to be std::move'd around, so don't use a
  /// SmallVector for once.
  std::vector<size_t> Offsets;

  /// Construct a parsed string table from the serialized buffer \p Buffer.
  ///
  /// \param Buffer Null-separated string table contents (e.g. a remarks
  /// section).
  LLVM_ABI ParsedStringTable(StringRef Buffer);
  /// Disable copying; parsed string tables are move-only.
  ///
  /// \param Other Unused; copy construction is deleted.
  ParsedStringTable(const ParsedStringTable &Other) = delete;
  /// Disable copy assignment; parsed string tables are move-only.
  ///
  /// \param Other Unused; copy assignment is deleted.
  ParsedStringTable &operator=(const ParsedStringTable &Other) = delete;
  /// Move-construct from another parsed string table.
  ///
  /// \param Other Source table whose contents are taken.
  ParsedStringTable(ParsedStringTable &&Other) = default;
  /// Move-assign from another parsed string table.
  ///
  /// \param Other Source table whose contents are taken.
  /// \return Reference to this table after the move.
  ParsedStringTable &operator=(ParsedStringTable &&Other) = default;

  /// Return the number of strings in the table.
  ///
  /// \return The number of entries in the string table.
  size_t size() const { return Offsets.size(); }
  /// Return the string at \p Index, or an error if out of bounds.
  ///
  /// \param Index Zero-based index into the string table.
  /// \return The string at \p Index, or an error if out of bounds.
  LLVM_ABI Expected<StringRef> operator[](size_t Index) const;
};

/// Create a remark parser for a standalone remark buffer.
///
/// \param ParserFormat Desired or auto-detected remark format.
/// \param Buf Buffer containing serialized remarks.
/// \return A remark parser for \p Buf, or an error on failure.
LLVM_ABI Expected<std::unique_ptr<RemarkParser>>
createRemarkParser(Format ParserFormat, StringRef Buf);

/// Create a remark parser from remark metadata that may reference an external
/// file.
///
/// \param ParserFormat Desired or auto-detected remark format.
/// \param Buf Buffer containing remark metadata (or a standalone container).
/// \param ExternalFilePrependPath Optional path prefix for external remark
/// files.
/// \return A remark parser for the metadata in \p Buf, or an error on failure.
LLVM_ABI Expected<std::unique_ptr<RemarkParser>> createRemarkParserFromMeta(
    Format ParserFormat, StringRef Buf,
    std::optional<StringRef> ExternalFilePrependPath = std::nullopt);

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_REMARKPARSER_H
