//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines common utilities for generating C++ code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_CODEGENHELPERS_H
#define LLVM_TABLEGEN_CODEGENHELPERS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

namespace llvm {

/// RAII helper that emits an `#ifdef` / `#undef` / `#endif` scope.
///
/// `LateUndef` controls whether the undef is emitted at the start of the scope
/// (false) or at the end of the scope (true).
class IfDefEmitter {
public:
  /// Open an `#ifdef` scope for \p Name on \p OS.
  ///
  /// \param OS Stream that receives the generated directives.
  /// \param Name Macro name used in the `#ifdef` / `#undef` / `#endif`.
  /// \param LateUndef If false, emit `#undef` immediately; if true, defer it
  ///        until destruction.
  IfDefEmitter(raw_ostream &OS, StringRef Name, bool LateUndef = false)
      : Name(Name.str()), OS(OS), LateUndef(LateUndef) {
    OS << "#ifdef " << Name << "\n";
    if (!LateUndef)
      OS << "#undef " << Name << "\n";
    OS << "\n";
  }

  /// Copy construction is deleted; each emitter owns a unique stream scope.
  ///
  /// \param Other Unused; copy construction is not supported.
  IfDefEmitter(const IfDefEmitter &Other) = delete;
  /// Copy assignment is deleted; each emitter owns a unique stream scope.
  ///
  /// \param Other Unused; copy assignment is not supported.
  IfDefEmitter &operator=(const IfDefEmitter &Other) = delete;

  /// Emit the closing `#undef` (when deferred) and `#endif` for the macro.
  ~IfDefEmitter() {
    OS << "\n";
    if (LateUndef)
      OS << "#undef " << Name << "\n";
    OS << "#endif // " << Name << "\n\n";
  }

private:
  std::string Name;
  raw_ostream &OS;
  bool LateUndef;
};

/// Base class for RAII helpers that emit `#if`-family guards and matching
/// `#endif`.
class IfGuardEmitterBase {
protected:
  /// Emit \p If followed by \p Condition and retain them for the matching
  /// `#endif`.
  ///
  /// \param OS Stream that receives the generated directives.
  /// \param If Directive text such as `#if`, `#ifdef`, or `#ifndef`.
  /// \param Condition Condition or macro name written after \p If.
  IfGuardEmitterBase(raw_ostream &OS, StringRef If, StringRef Condition)
      : Condition(Condition.str()), OS(OS) {
    OS << If << " " << Condition << "\n\n";
  }

  /// Copy construction is deleted; each emitter owns a unique stream scope.
  ///
  /// \param Other Unused; copy construction is not supported.
  IfGuardEmitterBase(const IfGuardEmitterBase &Other) = delete;
  /// Copy assignment is deleted; each emitter owns a unique stream scope.
  ///
  /// \param Other Unused; copy assignment is not supported.
  IfGuardEmitterBase &operator=(const IfGuardEmitterBase &Other) = delete;

  /// Emit the closing `#endif` annotated with the stored condition.
  ~IfGuardEmitterBase() { OS << "\n#endif // " << Condition << "\n\n"; }

private:
  std::string Condition;
  raw_ostream &OS;
};

/// RAII emitter for an `#if <Condition>` / `#endif` guard.
///
/// Emits:
/// \code
/// #if <Condition>
/// #endif // <Condition>
/// \endcode
class IfGuardEmitter : private IfGuardEmitterBase {
public:
  /// Open an `#if` guard for \p Condition on \p OS.
  ///
  /// \param OS Stream that receives the generated directives.
  /// \param Condition Expression written after `#if`.
  IfGuardEmitter(raw_ostream &OS, StringRef Condition)
      : IfGuardEmitterBase(OS, "#if", Condition) {}
};

/// RAII emitter for an `#ifdef <Condition>` / `#endif` guard.
///
/// Emits:
/// \code
/// #ifdef <Condition>
/// #endif // <Condition>
/// \endcode
class IfDefGuardEmitter : private IfGuardEmitterBase {
public:
  /// Open an `#ifdef` guard for \p Condition on \p OS.
  ///
  /// \param OS Stream that receives the generated directives.
  /// \param Condition Macro name written after `#ifdef`.
  IfDefGuardEmitter(raw_ostream &OS, StringRef Condition)
      : IfGuardEmitterBase(OS, "#ifdef", Condition) {}
};

/// RAII emitter for an `#ifndef <Condition>` / `#endif` guard.
///
/// Emits:
/// \code
/// #ifndef <Condition>
/// #endif // <Condition>
/// \endcode
class IfNDefGuardEmitter : private IfGuardEmitterBase {
public:
  /// Open an `#ifndef` guard for \p Condition on \p OS.
  ///
  /// \param OS Stream that receives the generated directives.
  /// \param Condition Macro name written after `#ifndef`.
  IfNDefGuardEmitter(raw_ostream &OS, StringRef Condition)
      : IfGuardEmitterBase(OS, "#ifndef", Condition) {}
};

/// RAII helper that emits a header include guard (`#ifndef` / `#define` /
/// `#endif`).
class IncludeGuardEmitter {
public:
  /// Open an include guard named \p Name on \p OS.
  ///
  /// \param OS Stream that receives the generated directives.
  /// \param Name Include-guard macro name.
  IncludeGuardEmitter(raw_ostream &OS, StringRef Name)
      : Name(Name.str()), OS(OS) {
    OS << "#ifndef " << Name << "\n"
       << "#define " << Name << "\n\n";
  }

  /// Copy construction is deleted; each emitter owns a unique stream scope.
  ///
  /// \param Other Unused; copy construction is not supported.
  IncludeGuardEmitter(const IncludeGuardEmitter &Other) = delete;
  /// Copy assignment is deleted; each emitter owns a unique stream scope.
  ///
  /// \param Other Unused; copy assignment is not supported.
  IncludeGuardEmitter &operator=(const IncludeGuardEmitter &Other) = delete;

  /// Emit the closing `#endif` for the include guard.
  ~IncludeGuardEmitter() { OS << "\n#endif // " << Name << "\n\n"; }

private:
  std::string Name;
  raw_ostream &OS;
};

/// RAII helper that emits a C++ namespace scope.
///
/// Name can be a single namespace or a nested namespace. If the name is empty,
/// no namespace scope is generated.
class NamespaceEmitter {
public:
  /// Open a `namespace` scope for \p NameUntrimmed on \p OS.
  ///
  /// \param OS Stream that receives the generated namespace braces.
  /// \param NameUntrimmed Namespace name, optionally with a leading `::`.
  NamespaceEmitter(raw_ostream &OS, const Twine &NameUntrimmed)
      : Name(trim(NameUntrimmed.str()).str()), OS(OS) {
    if (!Name.empty())
      OS << "namespace " << Name << " {\n\n";
  }

  /// Copy construction is deleted; each emitter owns a unique stream scope.
  ///
  /// \param Other Unused; copy construction is not supported.
  NamespaceEmitter(const NamespaceEmitter &Other) = delete;
  /// Copy assignment is deleted; each emitter owns a unique stream scope.
  ///
  /// \param Other Unused; copy assignment is not supported.
  NamespaceEmitter &operator=(const NamespaceEmitter &Other) = delete;

  /// Close the namespace scope if one was opened.
  ~NamespaceEmitter() {
    if (!Name.empty())
      OS << "\n} // namespace " << Name << "\n";
  }

private:
  // Trim "::" prefix. If the namespace specified is ""::mlir::toy", then the
  // generated namespace scope needs to use
  //
  // namespace mlir::toy {
  // }
  //
  // and cannot use "namespace ::mlir::toy".
  static StringRef trim(StringRef Name) {
    Name.consume_front("::");
    return Name;
  }

  std::string Name;
  raw_ostream &OS;
};

/// RAII helper that emits an anonymous `namespace { ... }` scope.
class AnonNamespaceEmitter {
public:
  /// Open an anonymous namespace on \p OS.
  ///
  /// \param OS Stream that receives the generated namespace braces.
  AnonNamespaceEmitter(raw_ostream &OS) : OS(OS) { OS << "namespace {\n\n"; }
  /// Copy construction is deleted; each emitter owns a unique stream scope.
  ///
  /// \param Other Unused; copy construction is not supported.
  AnonNamespaceEmitter(const AnonNamespaceEmitter &Other) = delete;
  /// Copy assignment is deleted; each emitter owns a unique stream scope.
  ///
  /// \param Other Unused; copy assignment is not supported.
  AnonNamespaceEmitter &operator=(const AnonNamespaceEmitter &Other) = delete;
  /// Close the anonymous namespace.
  ~AnonNamespaceEmitter() { OS << "} // namespace\n"; }

private:
  raw_ostream &OS;
};

} // end namespace llvm

#endif // LLVM_TABLEGEN_CODEGENHELPERS_H
