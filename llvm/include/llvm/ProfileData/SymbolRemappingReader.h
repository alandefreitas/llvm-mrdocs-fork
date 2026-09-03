//===- SymbolRemappingReader.h - Read symbol remapping file -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions needed for reading and applying symbol
// remapping files.
//
// Support is provided only for the Itanium C++ name mangling scheme for now.
//
// NOTE: If you are making changes to this file format, please remember
//       to document them in the Clang documentation at
//       tools/clang/docs/UsersManual.rst.
//
// File format
// -----------
//
// The symbol remappings are written as an ASCII text file. Blank lines and
// lines starting with a # are ignored. All other lines specify a kind of
// mangled name fragment, along with two fragments of that kind that should
// be treated as equivalent, separated by spaces.
//
// See http://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling for a
// description of the Itanium name mangling scheme.
//
// The accepted fragment kinds are:
//
//  * name  A <name>, such as 6foobar or St3__1
//  * type  A <type>, such as Ss or N4llvm9StringRefE
//  * encoding  An <encoding> (a complete mangling without the leading _Z)
//
// For example:
//
// # Ignore int / long differences to treat symbols from 32-bit and 64-bit
// # builds with differing size_t / ptrdiff_t / intptr_t as equivalent.
// type i l
// type j m
//
// # Ignore differences between libc++ and libstdc++, and between libstdc++'s
// # C++98 and C++11 ABIs.
// name 3std St3__1
// name 3std St7__cxx11
//
// # Remap a function overload to a specialization of a template (including
// # any local symbols declared within it).
// encoding N2NS1fEi N2NS1fIiEEvT_
//
// # Substitutions must be remapped separately from namespace 'std' for now.
// name Sa NSt3__19allocatorE
// name Sb NSt3__112basic_stringE
// type Ss NSt3__112basic_stringIcSt11char_traitsIcESaE
// # ...
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_SYMBOLREMAPPINGREADER_H
#define LLVM_PROFILEDATA_SYMBOLREMAPPINGREADER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ProfileData/ItaniumManglingCanonicalizer.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {

class MemoryBuffer;

/// ErrorInfo specialization for symbol remapping parse failures.
class SymbolRemappingParseError : public ErrorInfo<SymbolRemappingParseError> {
public:
  /// Construct a parse error for a remapping file.
  /// \param File Path of the remapping file being parsed.
  /// \param Line Line number where the error occurred.
  /// \param Message Human-readable error description.
  SymbolRemappingParseError(StringRef File, int64_t Line, const Twine &Message)
      : File(File), Line(Line), Message(Message.str()) {}

  /// Write the error location and message to \p OS.
  /// \param OS Output stream.
  void log(llvm::raw_ostream &OS) const override {
    OS << File << ':' << Line << ": " << Message;
  }
  /// Convert this error into a std::error_code.
  /// \return An inconvertible error code for this parse failure.
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }

  /// Return the path of the remapping file that failed to parse.
  /// \return The remapping file path.
  StringRef getFileName() const { return File; }
  /// Return the line number where the parse error occurred.
  /// \return The 1-based line number of the parse error.
  int64_t getLineNum() const { return Line; }
  /// Return the human-readable parse error message.
  /// \return The parse error message text.
  StringRef getMessage() const { return Message; }

  /// ErrorInfo class identity key.
  LLVM_ABI static char ID;

private:
  std::string File;
  int64_t Line;
  std::string Message;
};

/// Reader for symbol remapping files.
///
/// Remaps the symbol names in profile data to match those in the program
/// according to a set of rules specified in a given file.
class SymbolRemappingReader {
public:
  /// Read remappings from the given buffer, which must live as long as
  /// the remapper.
  /// \param B Memory buffer containing the remapping file contents.
  /// \return Success, or a parse error if the remapping file is invalid.
  LLVM_ABI Error read(MemoryBuffer &B);

  /// A Key represents an equivalence class of symbol names.
  using Key = uintptr_t;

  /// Construct a key for the given symbol.
  ///
  /// Return an existing key if an equivalent name has already been inserted.
  /// The symbol name must live as long as the remapper.
  ///
  /// The result will be Key() if the name cannot be remapped (typically
  /// because it is not a valid mangled name).
  /// \param FunctionName Symbol name to insert into the remapper.
  /// \return The key for \p FunctionName, or Key() if it cannot be remapped.
  Key insert(StringRef FunctionName) {
    return Canonicalizer.canonicalize(FunctionName);
  }

  /// Map the given symbol name into the key for the corresponding equivalence
  /// class.
  ///
  /// The result will typically be Key() if no equivalent symbol has been
  /// inserted, but this is not guaranteed: a Key different from all keys ever
  /// returned by \c insert may be returned instead.
  /// \param FunctionName Symbol name to look up.
  /// \return The key for the equivalence class of \p FunctionName.
  Key lookup(StringRef FunctionName) {
    return Canonicalizer.lookup(FunctionName);
  }

private:
  ItaniumManglingCanonicalizer Canonicalizer;
};

} // end namespace llvm

#endif // LLVM_PROFILEDATA_SYMBOLREMAPPINGREADER_H
