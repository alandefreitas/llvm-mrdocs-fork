//===- llvm/TextAPI/TextAPIError.h - TAPI Error -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Define TAPI specific error codes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_TEXTAPIERROR_H
#define LLVM_TEXTAPI_TEXTAPIERROR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm::MachO {
/// Error codes for failures while reading or writing TextAPI files.
enum class TextAPIErrorCode {
  /// The requested architecture slice was not found.
  NoSuchArchitecture,
  /// An operation produced no usable results.
  EmptyResults,
  /// A generic TextAPI frontend failure occurred.
  GenericFrontendError,
  /// The input did not match an expected TextAPI format.
  InvalidInputFormat,
  /// The target triple or platform is not supported.
  UnsupportedTarget
};

/// ErrorInfo specialization for TextAPI-specific failures.
class LLVM_ABI TextAPIError : public llvm::ErrorInfo<TextAPIError> {
public:
  /// RTTI identifier used by ErrorInfo::classID.
  static char ID;
  /// TextAPI error code describing the failure.
  TextAPIErrorCode EC;
  /// Optional human-readable context appended to the logged message.
  std::string Msg;

  /// Construct a TextAPIError from an error code.
  ///
  /// \param EC The TextAPI error code describing the failure.
  TextAPIError(TextAPIErrorCode EC) : EC(EC) {}
  /// Construct a TextAPIError from an error code and message.
  ///
  /// \param EC The TextAPI error code describing the failure.
  /// \param Msg Additional context appended when logging the error.
  TextAPIError(TextAPIErrorCode EC, std::string Msg)
      : EC(EC), Msg(std::move(Msg)) {}

  /// Write this error's message to \p OS.
  ///
  /// \param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override;
  /// Convert this error to a std::error_code.
  ///
  /// This conversion is not supported and will abort if called.
  /// \return Does not return; aborts because conversion is unsupported.
  std::error_code convertToErrorCode() const override;
};

} // namespace llvm::MachO
#endif // LLVM_TEXTAPI_TEXTAPIERROR_H
