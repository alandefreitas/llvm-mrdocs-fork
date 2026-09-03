//===- llvm/TableGen/Main.h - tblgen entry point ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the common entry point for tblgen tools.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_MAIN_H
#define LLVM_TABLEGEN_MAIN_H

#include "llvm/Support/CommandLine.h"
#include <map>

namespace llvm {

class raw_ostream;
class RecordKeeper;

/// Holds the primary and any additional files produced by a TableGen backend.
struct TableGenOutputFiles {
  /// Contents of the primary output file written by the backend.
  std::string MainFile;

  /// Maps additional output file name suffixes to their contents.
  std::map<StringRef, std::string> AdditionalFiles;
};

/// Returns true on error, false otherwise.
using TableGenMainFn =
    function_ref<bool(raw_ostream &OS, const RecordKeeper &Records)>;

/// Perform the action using Records, and store output in OutFiles.
/// Returns true on error, false otherwise.
using MultiFileTableGenMainFn = function_ref<bool(TableGenOutputFiles &OutFiles,
                                                  const RecordKeeper &Records)>;

/// Run TableGen with a single-file backend callback.
///
/// Parses the input TableGen file, invokes \p MainFn (or a registered
/// emitter), and writes the generated output.
///
/// \param argv0 Program name used when reporting errors.
/// \param MainFn Optional callback that writes one output file from Records.
/// \returns Non-zero status on error, zero on success.
LLVM_ABI int TableGenMain(const char *argv0, TableGenMainFn MainFn = nullptr);

/// Run TableGen with a multi-file backend callback.
///
/// Parses the input TableGen file, invokes \p MainFn (or a registered
/// emitter), and writes the main and any additional output files.
///
/// \param argv0 Program name used when reporting errors.
/// \param MainFn Optional callback that fills OutFiles from Records.
/// \returns Non-zero status on error, zero on success.
LLVM_ABI int TableGenMain(const char *argv0,
                          MultiFileTableGenMainFn MainFn = nullptr);

/// Controls emitting large character arrays as strings or character arrays.
/// Typically set to false when building with MSVC.
extern LLVM_ABI cl::opt<bool> EmitLongStrLiterals;

} // end namespace llvm

#endif // LLVM_TABLEGEN_MAIN_H
