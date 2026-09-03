//===- CodeViewError.h - Error extensions for CodeView ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CODEVIEWERROR_H
#define LLVM_DEBUGINFO_CODEVIEW_CODEVIEWERROR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
/// Error codes for failures while reading or writing CodeView records.
enum class cv_error_code {
  /// An unspecified CodeView error occurred.
  unspecified = 1,
  /// The buffer does not contain enough bytes for the requested operation.
  insufficient_buffer,
  /// The requested operation is not supported.
  operation_unsupported,
  /// A CodeView record is corrupt or malformed.
  corrupt_record,
  /// No CodeView records were available.
  no_records,
  /// An unrecognized member record kind was encountered.
  unknown_member_record,
};
} // namespace codeview
} // namespace llvm

namespace std {
template <>
struct is_error_code_enum<llvm::codeview::cv_error_code> : std::true_type {};
} // namespace std

namespace llvm {
namespace codeview {
/// Return the error category for \c cv_error_code values.
///
/// \returns The \c std::error_category used by CodeView error codes.
LLVM_ABI const std::error_category &CVErrorCategory();

/// Convert a \c cv_error_code to a \c std::error_code.
///
/// \param E CodeView error code to convert.
/// \returns A \c std::error_code for \p E in the CodeView error category.
inline std::error_code make_error_code(cv_error_code E) {
  return std::error_code(static_cast<int>(E), CVErrorCategory());
}

/// Base class for errors originating when parsing raw PDB files
class CodeViewError : public ErrorInfo<CodeViewError, StringError> {
public:
  /// Inherit constructors from the ErrorInfo base.
  using ErrorInfo<CodeViewError,
                  StringError>::ErrorInfo; // inherit constructors
  /// Construct a CodeViewError with an unspecified code and message \p S.
  ///
  /// \param S Human-readable error message.
  CodeViewError(const Twine &S) : ErrorInfo(S, cv_error_code::unspecified) {}
  /// RTTI identifier used by ErrorInfo::classID.
  LLVM_ABI static char ID;
};

} // namespace codeview
} // namespace llvm

#endif
