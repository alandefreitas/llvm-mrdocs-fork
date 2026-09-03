//===- BinaryStreamError.h - Error extensions for Binary Streams *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BINARYSTREAMERROR_H
#define LLVM_SUPPORT_BINARYSTREAMERROR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

#include <string>

namespace llvm {
/// Error codes for failures while reading or writing binary streams.
enum class stream_error_code {
  /// An unspecified stream error occurred.
  unspecified,
  /// The stream does not contain enough bytes for the requested operation.
  stream_too_short,
  /// The buffer size is not a multiple of the array element size.
  invalid_array_size,
  /// The specified offset is outside the bounds of the stream.
  invalid_offset,
  /// An I/O error occurred on the underlying file system.
  filesystem_error
};

/// Base class for errors originating when parsing raw PDB files
class LLVM_ABI BinaryStreamError : public ErrorInfo<BinaryStreamError> {
public:
  /// RTTI identifier used by ErrorInfo::classID.
  static char ID;

  /// Construct a BinaryStreamError from a stream error code.
  ///
  /// \param C The stream error code describing the failure.
  explicit BinaryStreamError(stream_error_code C);

  /// Construct a BinaryStreamError with an unspecified code and context.
  ///
  /// \param Context Additional context appended to the error message.
  explicit BinaryStreamError(StringRef Context);

  /// Construct a BinaryStreamError from a code and optional context.
  ///
  /// \param C The stream error code describing the failure.
  /// \param Context Additional context appended to the error message.
  BinaryStreamError(stream_error_code C, StringRef Context);

  /// Print this error's message to an output stream.
  ///
  /// \param OS Stream to write the error message to.
  void log(raw_ostream &OS) const override;

  /// Convert this error to a std::error_code.
  ///
  /// \return A std::error_code corresponding to this stream error.
  std::error_code convertToErrorCode() const override;

  /// Return the human-readable message for this error.
  ///
  /// \return The human-readable error message.
  StringRef getErrorMessage() const;

  /// Return the stream_error_code associated with this error.
  ///
  /// \return The stream_error_code for this error.
  stream_error_code getErrorCode() const { return Code; }

private:
  std::string ErrMsg;
  stream_error_code Code;
};
} // namespace llvm

#endif // LLVM_SUPPORT_BINARYSTREAMERROR_H
