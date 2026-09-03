//===- GenericError.h - system_error extensions for PDB ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_GENERICERROR_H
#define LLVM_DEBUGINFO_PDB_GENERICERROR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {

/// Error codes for generic failures while working with PDB files.
enum class pdb_error_code {
  /// The PDB file path is not a valid UTF-8 sequence.
  invalid_utf8_path = 1,
  /// LLVM was not built with DIA SDK support.
  dia_sdk_not_present,
  /// Loading the DIA SDK failed.
  dia_failed_loading,
  /// A PDB or related file signature does not match and may be out of date.
  signature_out_of_date,
  /// No matching precompiled header could be located.
  no_matching_pch,
  /// An unspecified PDB error occurred.
  unspecified,
};
} // namespace pdb
} // namespace llvm

namespace std {
template <>
struct is_error_code_enum<llvm::pdb::pdb_error_code> : std::true_type {};
} // namespace std

namespace llvm {
namespace pdb {
/// Return the error category for \c pdb_error_code values.
///
/// \returns The singleton error category used for PDB generic errors.
LLVM_ABI const std::error_category &PDBErrCategory();

/// Convert a \c pdb_error_code to a \c std::error_code.
///
/// \param E PDB error code to convert.
/// \returns A \c std::error_code corresponding to \p E.
inline std::error_code make_error_code(pdb_error_code E) {
  return std::error_code(static_cast<int>(E), PDBErrCategory());
}

/// Base class for errors originating when parsing raw PDB files
class PDBError : public ErrorInfo<PDBError, StringError> {
public:
  /// Inherit constructors from the ErrorInfo base.
  using ErrorInfo<PDBError, StringError>::ErrorInfo; // inherit constructors
  /// Construct a PDBError with an unspecified code and message \p S.
  ///
  /// \param S Human-readable error message.
  PDBError(const Twine &S) : ErrorInfo(S, pdb_error_code::unspecified) {}
  /// RTTI identifier used by ErrorInfo::classID.
  LLVM_ABI static char ID;
};
} // namespace pdb
} // namespace llvm
#endif
