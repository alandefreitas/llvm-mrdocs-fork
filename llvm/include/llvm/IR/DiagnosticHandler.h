//===- DiagnosticHandler.h - DiagnosticHandler class for LLVM ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Base DiagnosticHandler class declaration. Derive from this class to provide
// custom diagnostic reporting.
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DIAGNOSTICHANDLER_H
#define LLVM_IR_DIAGNOSTICHANDLER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class DiagnosticInfo;

/// This is the base class for diagnostic handling in LLVM.
///
/// The handleDiagnostics method must be overridden by the subclasses to handle
/// diagnostic. The *RemarkEnabled methods can be overridden to control
/// which remarks are enabled.
struct LLVM_ABI DiagnosticHandler {
  /// Opaque client context passed to \c DiagHandlerCallback.
  void *DiagnosticContext = nullptr;
  /// True if an error diagnostic has been reported.
  bool HasErrors = false;
  /// Construct a handler, optionally storing an opaque client context.
  ///
  /// \param DiagContext Opaque context pointer passed to \c DiagHandlerCallback.
  DiagnosticHandler(void *DiagContext = nullptr)
      : DiagnosticContext(DiagContext) {}
  /// Destroy the diagnostic handler.
  virtual ~DiagnosticHandler() = default;

  /// Function pointer type for C-style diagnostic callbacks.
  using DiagnosticHandlerTy = void (*)(const DiagnosticInfo *DI, void *Context);

  /// Callback set from the C API and invoked by handleDiagnostics().
  ///
  /// DiagHandlerCallback is settable from the C API and the base implementation
  /// of DiagnosticHandler will call it from handleDiagnostics(). Any derived
  /// class of DiagnosticHandler should not use the callback but
  /// implement handleDiagnostics().
  DiagnosticHandlerTy DiagHandlerCallback = nullptr;

  /// Handle a diagnostic, returning true if reporting was consumed.
  ///
  /// Override handleDiagnostics to provide a custom implementation. Return true
  /// if it handles diagnostics reporting properly otherwise return false to
  /// make LLVMContext::diagnose() print the message with a prefix based on the
  /// severity.
  ///
  /// \param DI Diagnostic to handle.
  /// \return True if the diagnostic was handled; false to let LLVMContext
  ///         print it.
  virtual bool handleDiagnostics(const DiagnosticInfo &DI) {
    if (DiagHandlerCallback) {
      DiagHandlerCallback(&DI, DiagnosticContext);
      return true;
    }
    return false;
  }

  /// Return true if analysis remarks are enabled, override
  /// to provide different implementation.
  ///
  /// \param PassName Name of the pass whose remarks are queried.
  /// \return True if analysis remarks are enabled for \p PassName.
  virtual bool isAnalysisRemarkEnabled(StringRef PassName) const;

  /// Return true if missed optimization remarks are enabled, override
  /// to provide different implementation.
  ///
  /// \param PassName Name of the pass whose remarks are queried.
  /// \return True if missed optimization remarks are enabled for \p PassName.
  virtual bool isMissedOptRemarkEnabled(StringRef PassName) const;

  /// Return true if passed optimization remarks are enabled, override
  /// to provide different implementation.
  ///
  /// \param PassName Name of the pass whose remarks are queried.
  /// \return True if passed optimization remarks are enabled for \p PassName.
  virtual bool isPassedOptRemarkEnabled(StringRef PassName) const;

  /// Return true if any type of remarks are enabled for this pass.
  ///
  /// \param PassName Name of the pass whose remarks are queried.
  /// \return True if any remark type is enabled for \p PassName.
  bool isAnyRemarkEnabled(StringRef PassName) const {
    return (isMissedOptRemarkEnabled(PassName) ||
            isPassedOptRemarkEnabled(PassName) ||
            isAnalysisRemarkEnabled(PassName));
  }

  /// Return true if any type of remarks are enabled for any pass.
  ///
  /// \return True if any remark type is enabled for any pass.
  virtual bool isAnyRemarkEnabled() const;
};
} // namespace llvm

#endif // LLVM_IR_DIAGNOSTICHANDLER_H
