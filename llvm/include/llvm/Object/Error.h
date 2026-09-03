//===- Error.h - system_error extensions for Object -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This declares a new error_category for the Object library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ERROR_H
#define LLVM_OBJECT_ERROR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <system_error>

namespace llvm {

class Twine;

namespace object {

/// Error category for object-file library errors.
///
/// \return The singleton error_category used by object_error codes.
LLVM_ABI const std::error_category &object_category();

/// Error codes for object-file parsing and inspection failures.
enum class object_error {
  // Error code 0 is absent. Use std::error_code() instead.
  arch_not_found = 1,          ///< Requested architecture was not found.
  invalid_file_type,           ///< File was not a valid object file.
  parse_failed,                ///< Object file parsing failed.
  unexpected_eof,              ///< Unexpected end of file while parsing.
  string_table_non_null_end,   ///< String table entry was not null-terminated.
  invalid_section_index,       ///< Invalid section index.
  bitcode_section_not_found,   ///< Expected bitcode section was not found.
  invalid_symbol_index,        ///< Invalid symbol index.
  section_stripped,            ///< Requested section has been stripped.
};

/// Convert an object_error enumerator to a std::error_code.
///
/// \param e Object-error enumerator to convert.
/// \return A std::error_code in the object_category for \p e.
inline std::error_code make_error_code(object_error e) {
  return std::error_code(static_cast<int>(e), object_category());
}

/// Base class for all errors indicating malformed binary files.
///
/// Having a subclass for all malformed binary files allows archive-walking
/// code to skip malformed files without having to understand every possible
/// way that a binary file might be malformed.
///
/// Currently inherits from ECError for easy interoperability with
/// std::error_code, but this will be removed in the future.
class LLVM_ABI BinaryError : public ErrorInfo<BinaryError, ECError> {
  void anchor() override;
public:
  /// Unique ErrorInfo RTTI key for BinaryError.
  static char ID;
  /// Construct a BinaryError with object_error::parse_failed by default.
  BinaryError() {
    // Default to parse_failed, can be overridden with setErrorCode.
    setErrorCode(make_error_code(object_error::parse_failed));
  }
};

/// Generic binary error.
///
/// For errors that don't require their own specific sub-error (most errors)
/// this class can be used to describe the error via a string message.
class LLVM_ABI GenericBinaryError
    : public ErrorInfo<GenericBinaryError, BinaryError> {
public:
  /// Unique ErrorInfo RTTI key for GenericBinaryError.
  static char ID;
  /// Construct with message \p Msg (error code parse_failed).
  ///
  /// \param Msg Human-readable description of the error.
  GenericBinaryError(const Twine &Msg);
  /// Construct with message \p Msg and error code \p ECOverride.
  ///
  /// \param Msg Human-readable description of the error.
  /// \param ECOverride Object-error code to associate with this error.
  GenericBinaryError(const Twine &Msg, object_error ECOverride);
  /// Return the human-readable error message.
  ///
  /// \return The error message string associated with this error.
  const std::string &getMessage() const { return Msg; }
  /// Write this error's message to \p OS.
  ///
  /// \param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override;
private:
  std::string Msg;
};

/// Consume \p Err when it is invalid_file_type; otherwise return it.
///
/// isNotObjectErrorInvalidFileType() is used when looping through the children
/// of an archive after calling getAsBinary() on the child and it returns an
/// llvm::Error.  In the cases we want to loop through the children and ignore the
/// non-objects in the archive this is used to test the error to see if an
/// error() function needs to called on the llvm::Error.
///
/// \param Err Error returned from getAsBinary() (or similar) to classify.
/// \return Success if \p Err was invalid_file_type; otherwise \p Err unchanged.
LLVM_ABI Error isNotObjectErrorInvalidFileType(llvm::Error Err);

/// Create a parse_failed StringError with message \p Err.
///
/// \param Err Message describing the parse failure.
/// \return An Error wrapping a StringError with object_error::parse_failed.
inline Error createError(const Twine &Err) {
  return make_error<StringError>(Err, object_error::parse_failed);
}

} // end namespace object.

} // end namespace llvm.

namespace std {
template <>
struct is_error_code_enum<llvm::object::object_error> : std::true_type {};
}

#endif
