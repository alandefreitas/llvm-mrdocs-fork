//===- RawError.h - Error extensions for raw PDB implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_RAWERROR_H
#define LLVM_DEBUGINFO_PDB_NATIVE_RAWERROR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {
/// Error codes for failures while reading or writing raw PDB files.
enum class raw_error_code {
  /// An unspecified raw PDB error occurred.
  unspecified = 1,
  /// The requested feature is unsupported by the implementation.
  feature_unsupported,
  /// A record is in an unexpected format.
  invalid_format,
  /// The PDB file is corrupt.
  corrupt_file,
  /// The buffer is not large enough for the requested number of bytes.
  insufficient_buffer,
  /// The specified stream could not be loaded.
  no_stream,
  /// The specified index is out of bounds.
  index_out_of_bounds,
  /// The specified block address is not valid.
  invalid_block_address,
  /// The entry already exists.
  duplicate_entry,
  /// The entry does not exist.
  no_entry,
  /// The PDB does not support writing.
  not_writable,
  /// The stream was longer than expected.
  stream_too_long,
  /// A type record has an invalid TPI hash value.
  invalid_tpi_hash,
};
} // namespace pdb
} // namespace llvm

namespace std {
template <>
struct is_error_code_enum<llvm::pdb::raw_error_code> : std::true_type {};
} // namespace std

namespace llvm {
namespace pdb {
/// Return the error category for \c raw_error_code values.
///
/// \returns The error category for raw PDB error codes.
LLVM_ABI const std::error_category &RawErrCategory();

/// Convert a \c raw_error_code to a \c std::error_code.
///
/// \param E Raw PDB error code to convert.
/// \returns A \c std::error_code corresponding to \p E.
inline std::error_code make_error_code(raw_error_code E) {
  return std::error_code(static_cast<int>(E), RawErrCategory());
}

/// Base class for errors originating when parsing raw PDB files
class RawError : public ErrorInfo<RawError, StringError> {
public:
  /// Inherit constructors from the ErrorInfo base.
  using ErrorInfo<RawError, StringError>::ErrorInfo; // inherit constructors
  /// Construct a RawError with an unspecified code and message \p S.
  ///
  /// \param S Human-readable error message.
  RawError(const Twine &S) : ErrorInfo(S, raw_error_code::unspecified) {}
  /// RTTI identifier used by ErrorInfo::classID.
  LLVM_ABI static char ID;
};
} // namespace pdb
} // namespace llvm
#endif
