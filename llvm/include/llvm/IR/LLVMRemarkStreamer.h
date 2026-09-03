//===- llvm/IR/LLVMRemarkStreamer.h - Streamer for LLVM remarks--*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the conversion between IR Diagnostics and
// serializable remarks::Remark objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_LLVMREMARKSTREAMER_H
#define LLVM_IR_LLVMREMARKSTREAMER_H

#include "llvm/Remarks/Remark.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ToolOutputFile.h"
#include <memory>
#include <optional>
#include <string>

namespace llvm {

class DiagnosticInfoOptimizationBase;
class LLVMContext;
class ToolOutputFile;
namespace remarks {
class RemarkStreamer;
}

/// Streamer for LLVM remarks which has logic for dealing with DiagnosticInfo
/// objects.
class LLVMRemarkStreamer {
  remarks::RemarkStreamer &RS;
  /// Convert diagnostics into remark objects.
  /// The lifetime of the members of the result is bound to the lifetime of
  /// the LLVM diagnostics.
  remarks::Remark toRemark(const DiagnosticInfoOptimizationBase &Diag) const;

public:
  /// Construct a streamer that emits remarks through \p RS.
  ///
  /// \param RS Remark streamer that receives converted remarks.
  LLVMRemarkStreamer(remarks::RemarkStreamer &RS) : RS(RS) {}
  /// Emit a diagnostic through the streamer.
  ///
  /// \param Diag Optimization diagnostic to convert and emit as a remark.
  LLVM_ABI void emit(const DiagnosticInfoOptimizationBase &Diag);
};

/// CRTP base for errors raised while setting up optimization remarks.
template <typename ThisError>
struct LLVMRemarkSetupErrorInfo : public ErrorInfo<ThisError> {
  /// Human-readable description of the setup failure.
  std::string Msg;
  /// Error code associated with the setup failure.
  std::error_code EC;

  /// Construct from an Error, capturing its message and error code.
  ///
  /// \param E Error whose message and error code are stored.
  LLVMRemarkSetupErrorInfo(Error E) {
    handleAllErrors(std::move(E), [&](const ErrorInfoBase &EIB) {
      Msg = EIB.message();
      EC = EIB.convertToErrorCode();
    });
  }

  /// Write this error's message to \p OS.
  ///
  /// \param OS Stream that receives the message.
  void log(raw_ostream &OS) const override { OS << Msg; }
  /// Convert this error into a std::error_code.
  /// \return Error code associated with this setup failure.
  std::error_code convertToErrorCode() const override { return EC; }
};

/// Error raised when the remarks output file cannot be opened.
struct LLVMRemarkSetupFileError
    : LLVMRemarkSetupErrorInfo<LLVMRemarkSetupFileError> {
  /// ErrorInfo class identifier.
  LLVM_ABI static char ID;
  /// Inherit constructors from LLVMRemarkSetupErrorInfo.
  using LLVMRemarkSetupErrorInfo<
      LLVMRemarkSetupFileError>::LLVMRemarkSetupErrorInfo;
};

/// Error raised when a remark pass filter pattern is invalid.
struct LLVMRemarkSetupPatternError
    : LLVMRemarkSetupErrorInfo<LLVMRemarkSetupPatternError> {
  /// ErrorInfo class identifier.
  LLVM_ABI static char ID;
  /// Inherit constructors from LLVMRemarkSetupErrorInfo.
  using LLVMRemarkSetupErrorInfo<
      LLVMRemarkSetupPatternError>::LLVMRemarkSetupErrorInfo;
};

/// Error raised when a remark serialization format is invalid.
struct LLVMRemarkSetupFormatError
    : LLVMRemarkSetupErrorInfo<LLVMRemarkSetupFormatError> {
  /// ErrorInfo class identifier.
  LLVM_ABI static char ID;
  /// Inherit constructors from LLVMRemarkSetupErrorInfo.
  using LLVMRemarkSetupErrorInfo<
      LLVMRemarkSetupFormatError>::LLVMRemarkSetupErrorInfo;
};

/// RAII handle that manages the lifetime of the ToolOutputFile used to output
/// remarks.
///
/// On destruction (or when calling releaseFile()), this handle ensures that
/// the optimization remarks are finalized and the RemarkStreamer is correctly
/// deregistered from the LLVMContext.
class LLVMRemarkFileHandle final {
  struct Finalizer {
    LLVMContext *Context;

    Finalizer(LLVMContext *Ctx) : Context(Ctx) {}

    Finalizer(const Finalizer &) = delete;
    Finalizer &operator=(const Finalizer &) = delete;

    Finalizer(Finalizer &&Other) : Context(Other.Context) {
      Other.Context = nullptr;
    }

    Finalizer &operator=(Finalizer &&Other) {
      std::swap(Context, Other.Context);
      return *this;
    }

    ~Finalizer() { finalize(); }

    LLVM_ABI void finalize();
  };

  std::unique_ptr<ToolOutputFile> OutputFile;
  Finalizer Finalize;

public:
  /// Construct an empty handle that owns no output file.
  LLVMRemarkFileHandle() : OutputFile(nullptr), Finalize(nullptr) {}

  /// Construct a handle that owns \p OutputFile and finalizes remarks for \p
  /// Ctx.
  ///
  /// \param OutputFile Owned remarks output file.
  /// \param Ctx LLVM context whose remark streamer is finalized with this
  /// file.
  LLVMRemarkFileHandle(std::unique_ptr<ToolOutputFile> OutputFile,
                       LLVMContext &Ctx)
      : OutputFile(std::move(OutputFile)), Finalize(&Ctx) {}

  /// Return a pointer to the owned ToolOutputFile, or null.
  /// \return Pointer to the owned ToolOutputFile, or null.
  ToolOutputFile *get() { return OutputFile.get(); }
  /// Return true if this handle owns a ToolOutputFile.
  /// \return True if this handle owns a ToolOutputFile.
  explicit operator bool() { return bool(OutputFile); }

  /// Finalize remark emission and release the underlying ToolOutputFile.
  /// \return Owned ToolOutputFile after finalizing remarks.
  std::unique_ptr<ToolOutputFile> releaseFile() {
    finalize();
    return std::move(OutputFile);
  }

  /// Finalize remark emission for the associated LLVM context.
  void finalize() { Finalize.finalize(); }

  /// Return a reference to the owned ToolOutputFile.
  /// \return Reference to the owned ToolOutputFile.
  ToolOutputFile &operator*() { return *OutputFile; }
  /// Return a pointer to the owned ToolOutputFile.
  /// \return Pointer to the owned ToolOutputFile.
  ToolOutputFile *operator->() { return &*OutputFile; }
};

/// Set up optimization remarks that output to a file.
///
/// The LLVMRemarkFileHandle manages the lifetime of the underlying
/// ToolOutputFile to ensure \ref finalizeLLVMOptimizationRemarks() is called
/// before the file is destroyed or released from the handle. The handle must
/// be kept alive until all remarks were emitted through the remark streamer.
///
/// \param Context LLVM context that receives the remark streamer.
/// \param RemarksFilename Output path for optimization remarks.
/// \param RemarksPasses Regex of pass names whose remarks should be emitted.
/// \param RemarksFormat Serialization format for remarks.
/// \param RemarksWithHotness Whether to include profile hotness information.
/// \param RemarksHotnessThreshold Minimum hotness required to emit a remark.
/// \return Handle owning the remarks output file, or an error on failure.
LLVM_ABI Expected<LLVMRemarkFileHandle> setupLLVMOptimizationRemarks(
    LLVMContext &Context, StringRef RemarksFilename, StringRef RemarksPasses,
    StringRef RemarksFormat, bool RemarksWithHotness,
    std::optional<uint64_t> RemarksHotnessThreshold = 0);

/// Set up optimization remarks that output directly to a raw_ostream.
///
/// \p OS is managed by the caller and must be open for writing until
/// \ref finalizeLLVMOptimizationRemarks() is called.
///
/// \param Context LLVM context that receives the remark streamer.
/// \param OS Stream that receives serialized remarks.
/// \param RemarksPasses Regex of pass names whose remarks should be emitted.
/// \param RemarksFormat Serialization format for remarks.
/// \param RemarksWithHotness Whether to include profile hotness information.
/// \param RemarksHotnessThreshold Minimum hotness required to emit a remark.
/// \return Success, or an error if remark setup fails.
LLVM_ABI Error setupLLVMOptimizationRemarks(
    LLVMContext &Context, raw_ostream &OS, StringRef RemarksPasses,
    StringRef RemarksFormat, bool RemarksWithHotness,
    std::optional<uint64_t> RemarksHotnessThreshold = 0);

/// Finalize optimization remarks and deregister the RemarkStreamer from the
/// Context.
///
/// This must be called before closing the (file) stream that was used to set
/// up the remarks.
///
/// \param Context LLVM context whose remark streamer should be torn down.
LLVM_ABI void finalizeLLVMOptimizationRemarks(LLVMContext &Context);

} // end namespace llvm

#endif // LLVM_IR_LLVMREMARKSTREAMER_H
