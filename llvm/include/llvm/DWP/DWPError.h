#ifndef LLVM_DWP_DWPERROR_H
#define LLVM_DWP_DWPERROR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include <string>

namespace llvm {
/// Error type for failures while building or writing DWP files.
class DWPError : public ErrorInfo<DWPError> {
public:
  /// Construct a DWPError with a human-readable message.
  ///
  /// \param Info Message describing the DWP failure.
  DWPError(std::string Info) : Info(std::move(Info)) {}

  /// Print this error's message to an output stream.
  ///
  /// \param OS Stream to write the error message to.
  void log(raw_ostream &OS) const override { OS << Info; }

  /// Convert this error to a std::error_code.
  ///
  /// This conversion is not implemented and will abort if called.
  ///
  /// \return Never returns; aborts because conversion is not implemented.
  std::error_code convertToErrorCode() const override {
    llvm_unreachable("Not implemented");
  }

  /// RTTI identifier used by ErrorInfo::classID.
  LLVM_ABI static char ID;

private:
  std::string Info;
};
} // namespace llvm

#endif // LLVM_DWP_DWPERROR_H
