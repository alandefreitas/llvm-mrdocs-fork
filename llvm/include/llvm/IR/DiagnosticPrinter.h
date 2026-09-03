//===- llvm/IR/DiagnosticPrinter.h - Diagnostic Printer ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the main interface for printer backend diagnostic.
//
// Clients of the backend diagnostics should overload this interface based
// on their needs.
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DIAGNOSTICPRINTER_H
#define LLVM_IR_DIAGNOSTICPRINTER_H

#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {

// Forward declarations.
class Module;
class raw_ostream;
class SMDiagnostic;
class StringRef;
class Twine;
class Value;

/// Interface for custom diagnostic printing.
class DiagnosticPrinter {
public:
  /// Destroy the diagnostic printer.
  virtual ~DiagnosticPrinter() = default;

  // Simple types.
  /// Print character \p C to the diagnostic output.
  ///
  /// \param C Character to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(char C) = 0;
  /// Print unsigned character \p C to the diagnostic output.
  ///
  /// \param C Character to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(unsigned char C) = 0;
  /// Print signed character \p C to the diagnostic output.
  ///
  /// \param C Character to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(signed char C) = 0;
  /// Print \p Str to the diagnostic output.
  ///
  /// \param Str String contents to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(StringRef Str) = 0;
  /// Print null-terminated C string \p Str to the diagnostic output.
  ///
  /// \param Str Null-terminated C string to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(const char *Str) = 0;
  /// Print std::string \p Str to the diagnostic output.
  ///
  /// \param Str String to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(const std::string &Str) = 0;
  /// Print unsigned long \p N to the diagnostic output.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(unsigned long N) = 0;
  /// Print long \p N to the diagnostic output.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(long N) = 0;
  /// Print unsigned long long \p N to the diagnostic output.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(unsigned long long N) = 0;
  /// Print long long \p N to the diagnostic output.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(long long N) = 0;
  /// Print pointer \p P to the diagnostic output.
  ///
  /// \param P Pointer value to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(const void *P) = 0;
  /// Print unsigned int \p N to the diagnostic output.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(unsigned int N) = 0;
  /// Print int \p N to the diagnostic output.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(int N) = 0;
  /// Print floating-point value \p N to the diagnostic output.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(double N) = 0;
  /// Print Twine \p Str to the diagnostic output.
  ///
  /// \param Str Twine contents to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(const Twine &Str) = 0;

  // IR related types.
  /// Print IR value \p V to the diagnostic output.
  ///
  /// \param V Value to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(const Value &V) = 0;
  /// Print module \p M to the diagnostic output.
  ///
  /// \param M Module to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(const Module &M) = 0;

  // Other types.
  /// Print source-manager diagnostic \p Diag to the diagnostic output.
  ///
  /// \param Diag Source-manager diagnostic to print.
  /// \return Reference to this printer for chaining.
  virtual DiagnosticPrinter &operator<<(const SMDiagnostic &Diag) = 0;
};

/// Basic diagnostic printer that uses an underlying raw_ostream.
class LLVM_ABI DiagnosticPrinterRawOStream : public DiagnosticPrinter {
protected:
  /// Underlying stream used for diagnostic output.
  raw_ostream &Stream;

public:
  /// Construct a printer that writes to \p Stream.
  ///
  /// \param Stream Output stream that receives diagnostic text.
  DiagnosticPrinterRawOStream(raw_ostream &Stream) : Stream(Stream) {}

  // Simple types.
  /// Print character \p C to the underlying stream.
  ///
  /// \param C Character to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(char C) override;
  /// Print unsigned character \p C to the underlying stream.
  ///
  /// \param C Character to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(unsigned char C) override;
  /// Print signed character \p C to the underlying stream.
  ///
  /// \param C Character to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(signed char C) override;
  /// Print \p Str to the underlying stream.
  ///
  /// \param Str String contents to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(StringRef Str) override;
  /// Print null-terminated C string \p Str to the underlying stream.
  ///
  /// \param Str Null-terminated C string to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(const char *Str) override;
  /// Print std::string \p Str to the underlying stream.
  ///
  /// \param Str String to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(const std::string &Str) override;
  /// Print unsigned long \p N to the underlying stream.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(unsigned long N) override;
  /// Print long \p N to the underlying stream.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(long N) override;
  /// Print unsigned long long \p N to the underlying stream.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(unsigned long long N) override;
  /// Print long long \p N to the underlying stream.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(long long N) override;
  /// Print pointer \p P to the underlying stream.
  ///
  /// \param P Pointer value to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(const void *P) override;
  /// Print unsigned int \p N to the underlying stream.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(unsigned int N) override;
  /// Print int \p N to the underlying stream.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(int N) override;
  /// Print floating-point value \p N to the underlying stream.
  ///
  /// \param N Value to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(double N) override;
  /// Print Twine \p Str to the underlying stream.
  ///
  /// \param Str Twine contents to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(const Twine &Str) override;

  // IR related types.
  /// Print IR value \p V to the underlying stream.
  ///
  /// \param V Value to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(const Value &V) override;
  /// Print module \p M to the underlying stream.
  ///
  /// \param M Module to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(const Module &M) override;

  // Other types.
  /// Print source-manager diagnostic \p Diag to the underlying stream.
  ///
  /// \param Diag Source-manager diagnostic to print.
  /// \return Reference to this printer for chaining.
  DiagnosticPrinter &operator<<(const SMDiagnostic &Diag) override;
};

} // end namespace llvm

#endif // LLVM_IR_DIAGNOSTICPRINTER_H
