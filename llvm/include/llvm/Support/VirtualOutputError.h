//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations of the OutputError class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_VIRTUALOUTPUTERROR_H
#define LLVM_SUPPORT_VIRTUALOUTPUTERROR_H

#include "llvm/Support/Error.h"
#include "llvm/Support/VirtualOutputConfig.h"

namespace llvm::vfs {

/// Return the error category for virtual filesystem output errors.
///
/// \return The error category for virtual filesystem output errors.
LLVM_ABI const std::error_category &output_category();

/// Error codes for virtual filesystem output operations.
///
/// Error code 0 is absent; use \c std::error_code() for success instead.
enum class OutputErrorCode {
  /// The output was destroyed without being closed.
  not_closed = 1,
  /// The output configuration is invalid.
  invalid_config,
  /// The output was already closed.
  already_closed,
  /// The output still has an open proxy stream.
  has_open_proxy,
};

/// Convert an \a OutputErrorCode to a \c std::error_code.
///
/// \param EV Output error code to convert.
/// \return A \c std::error_code corresponding to \p EV.
inline std::error_code make_error_code(OutputErrorCode EV) {
  return std::error_code(static_cast<int>(EV), output_category());
}

/// Error related to an \a OutputFile. Derives from \a ECError and adds \a
/// getOutputPath().
class LLVM_ABI OutputError : public ErrorInfo<OutputError, ECError> {
  void anchor() override;

public:
  /// Return the path of the output associated with this error.
  ///
  /// \return The path of the output associated with this error.
  StringRef getOutputPath() const { return OutputPath; }
  /// Print this error's message to an output stream.
  ///
  /// \param OS Stream to write the error message to.
  void log(raw_ostream &OS) const override;

  /// RTTI identifier used by ErrorInfo::classID.
  static char ID;

  /// Construct an OutputError from a path and a \c std::error_code.
  ///
  /// \param OutputPath Path of the output associated with the error.
  /// \param EC Non-success error code describing the failure.
  OutputError(const Twine &OutputPath, std::error_code EC)
      : ErrorInfo<OutputError, ECError>(EC), OutputPath(OutputPath.str()) {
    assert(EC && "Cannot create OutputError from success EC");
  }

  /// Construct an OutputError from a path and an \a OutputErrorCode.
  ///
  /// \param OutputPath Path of the output associated with the error.
  /// \param EV Output error code describing the failure.
  OutputError(const Twine &OutputPath, OutputErrorCode EV)
      : ErrorInfo<OutputError, ECError>(make_error_code(EV)),
        OutputPath(OutputPath.str()) {
    assert(EC && "Cannot create OutputError from success EC");
  }

private:
  std::string OutputPath;
};

/// Return \a Error::success() or use \p OutputPath to create an \a
/// OutputError, depending on \p EC.
///
/// \param OutputPath Path of the output associated with the error.
/// \param EC Error code; on success, returns \a Error::success().
/// \return \a Error::success() if \p EC indicates success; otherwise an
/// \a OutputError.
inline Error convertToOutputError(const Twine &OutputPath, std::error_code EC) {
  if (EC)
    return make_error<OutputError>(OutputPath, EC);
  return Error::success();
}

/// Error related to an OutputConfig for an \a OutputFile. Derives from \a
/// OutputError and adds \a getConfig().
class LLVM_ABI OutputConfigError
    : public ErrorInfo<OutputConfigError, OutputError> {
  void anchor() override;

public:
  /// Return the invalid output configuration associated with this error.
  ///
  /// \return The invalid output configuration associated with this error.
  OutputConfig getConfig() const { return Config; }
  /// Print this error's message to an output stream.
  ///
  /// \param OS Stream to write the error message to.
  void log(raw_ostream &OS) const override;

  /// RTTI identifier used by ErrorInfo::classID.
  static char ID;

  /// Construct an OutputConfigError from a config and output path.
  ///
  /// \param Config Invalid output configuration that caused the error.
  /// \param OutputPath Path of the output associated with the error.
  OutputConfigError(OutputConfig Config, const Twine &OutputPath)
      : ErrorInfo<OutputConfigError, OutputError>(
            OutputPath, OutputErrorCode::invalid_config),
        Config(Config) {}

private:
  OutputConfig Config;
};

/// Error related to a temporary file for an \a OutputFile. Derives from \a
/// OutputError and adds \a getTempPath().
class LLVM_ABI TempFileOutputError
    : public ErrorInfo<TempFileOutputError, OutputError> {
  void anchor() override;

public:
  /// Return the temporary file path associated with this error.
  ///
  /// \return The temporary file path associated with this error.
  StringRef getTempPath() const { return TempPath; }
  /// Print this error's message to an output stream.
  ///
  /// \param OS Stream to write the error message to.
  void log(raw_ostream &OS) const override;

  /// RTTI identifier used by ErrorInfo::classID.
  static char ID;

  /// Construct a TempFileOutputError from paths and a \c std::error_code.
  ///
  /// \param TempPath Path of the temporary file associated with the error.
  /// \param OutputPath Path of the final output associated with the error.
  /// \param EC Non-success error code describing the failure.
  TempFileOutputError(const Twine &TempPath, const Twine &OutputPath,
                      std::error_code EC)
      : ErrorInfo<TempFileOutputError, OutputError>(OutputPath, EC),
        TempPath(TempPath.str()) {}

  /// Construct a TempFileOutputError from paths and an \a OutputErrorCode.
  ///
  /// \param TempPath Path of the temporary file associated with the error.
  /// \param OutputPath Path of the final output associated with the error.
  /// \param EV Output error code describing the failure.
  TempFileOutputError(const Twine &TempPath, const Twine &OutputPath,
                      OutputErrorCode EV)
      : ErrorInfo<TempFileOutputError, OutputError>(OutputPath, EV),
        TempPath(TempPath.str()) {}

private:
  std::string TempPath;
};

/// Return \a Error::success() or use \p TempPath and \p OutputPath to create a
/// \a TempFileOutputError, depending on \p EC.
///
/// \param TempPath Path of the temporary file associated with the error.
/// \param OutputPath Path of the final output associated with the error.
/// \param EC Error code; on success, returns \a Error::success().
/// \return \a Error::success() if \p EC indicates success; otherwise a
/// \a TempFileOutputError.
inline Error convertToTempFileOutputError(const Twine &TempPath,
                                          const Twine &OutputPath,
                                          std::error_code EC) {
  if (EC)
    return make_error<TempFileOutputError>(TempPath, OutputPath, EC);
  return Error::success();
}

} // namespace llvm::vfs

#endif // LLVM_SUPPORT_VIRTUALOUTPUTERROR_H
