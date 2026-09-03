//===- MSFError.h - Error extensions for MSF Files --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_MSF_MSFERROR_H
#define LLVM_DEBUGINFO_MSF_MSFERROR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace msf {
/// Error codes for failures while reading or writing MSF files.
enum class msf_error_code {
  /// An unspecified MSF error occurred.
  unspecified = 1,
  /// The buffer does not contain enough bytes for the requested operation.
  insufficient_buffer,
  /// The MSF file or stream is not writable.
  not_writable,
  /// The requested stream does not exist.
  no_stream,
  /// The MSF file has an invalid format.
  invalid_format,
  /// A requested block is already in use.
  block_in_use,
  /// A size overflow occurred with a 4096-byte page size.
  size_overflow_4096,
  /// A size overflow occurred with an 8192-byte page size.
  size_overflow_8192,
  /// A size overflow occurred with a 16384-byte page size.
  size_overflow_16384,
  /// A size overflow occurred with a 32768-byte page size.
  size_overflow_32768,
  /// The stream directory grew beyond the capacity of the MSF layout.
  stream_directory_overflow,
};
} // namespace msf
} // namespace llvm

namespace std {
template <>
struct is_error_code_enum<llvm::msf::msf_error_code> : std::true_type {};
} // namespace std

namespace llvm {
namespace msf {
/// Return the error category for \c msf_error_code values.
///
/// \returns The error category for MSF error codes.
LLVM_ABI const std::error_category &MSFErrCategory();

/// Convert a \c msf_error_code to a \c std::error_code.
///
/// \param E MSF error code to convert.
/// \returns A \c std::error_code corresponding to \p E.
inline std::error_code make_error_code(msf_error_code E) {
  return std::error_code(static_cast<int>(E), MSFErrCategory());
}

/// Base class for errors originating when parsing raw PDB files
class MSFError : public ErrorInfo<MSFError, StringError> {
public:
  /// Inherit constructors from the ErrorInfo base.
  using ErrorInfo<MSFError, StringError>::ErrorInfo; // inherit constructors
  /// Construct an MSFError with an unspecified code and message \p S.
  ///
  /// \param S Human-readable error message.
  MSFError(const Twine &S) : ErrorInfo(S, msf_error_code::unspecified) {}

  /// Return true if this error indicates an MSF page or directory overflow.
  ///
  /// \returns True if the error is a page or directory overflow.
  bool isPageOverflow() const {
    switch (static_cast<msf_error_code>(convertToErrorCode().value())) {
    case msf_error_code::unspecified:
    case msf_error_code::insufficient_buffer:
    case msf_error_code::not_writable:
    case msf_error_code::no_stream:
    case msf_error_code::invalid_format:
    case msf_error_code::block_in_use:
      return false;
    case msf_error_code::size_overflow_4096:
    case msf_error_code::size_overflow_8192:
    case msf_error_code::size_overflow_16384:
    case msf_error_code::size_overflow_32768:
    case msf_error_code::stream_directory_overflow:
      return true;
    }
    llvm_unreachable("msf error code not implemented");
  }

  /// RTTI identifier used by ErrorInfo::classID.
  LLVM_ABI static char ID;
};
} // namespace msf
} // namespace llvm

#endif // LLVM_DEBUGINFO_MSF_MSFERROR_H
