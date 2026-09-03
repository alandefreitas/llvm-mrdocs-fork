//===-- FuzzerCLI.h - Common logic for CLIs of fuzzers ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Common logic needed to implement LLVM's fuzz targets' CLIs - including LLVM
// concepts like cl::opt and libFuzzer concepts like -ignore_remaining_args=1.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_FUZZERCLI_H
#define LLVM_FUZZMUTATE_FUZZERCLI_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include <stddef.h>

namespace llvm {

class StringRef;

/// Parse cl::opts from a fuzz target commandline.
///
/// This handles all arguments after -ignore_remaining_args=1 as cl::opts.
///
/// \param ArgC Argument count from the process command line.
/// \param ArgV Argument vector from the process command line.
LLVM_ABI void parseFuzzerCLOpts(int ArgC, char *ArgV[]);

/// Handle backend options that are encoded in the executable name.
///
/// Parses some common backend options out of a specially crafted executable
/// name (argv[0]). For example, a name like llvm-foo-fuzzer--aarch64-gisel
/// might set up an AArch64 triple and the Global ISel selector. This should be
/// called *before* parseFuzzerCLOpts if calling both.
///
/// This is meant to be used for environments like OSS-Fuzz that aren't capable
/// of passing in command line arguments in the normal way.
///
/// \param ExecName Executable name (typically argv[0]) encoding backend opts.
LLVM_ABI void handleExecNameEncodedBEOpts(StringRef ExecName);

/// Handle optimizer options which are encoded in the executable name.
/// Same semantics as in 'handleExecNameEncodedBEOpts'.
///
/// \param ExecName Executable name (typically argv[0]) encoding optimizer opts.
LLVM_ABI void handleExecNameEncodedOptimizerOpts(StringRef ExecName);

/// Callback type for testing a single fuzzer input.
using FuzzerTestFun = int (*)(const uint8_t *Data, size_t Size);

/// Callback type for initializing a fuzzer before running inputs.
using FuzzerInitFun = int (*)(int *argc, char ***argv);

/// Runs a fuzz target on the inputs specified on the command line.
///
/// Useful for testing fuzz targets without linking to libFuzzer. Finds inputs
/// in the argument list in a libFuzzer compatible way.
///
/// \param ArgC Argument count from the process command line.
/// \param ArgV Argument vector from the process command line.
/// \param TestOne Callback invoked once per input file with its contents.
/// \param Init Optional initialization callback, run before any inputs.
/// \return Zero on success, or a non-zero exit code on initialization or I/O
/// failure.
LLVM_ABI int runFuzzerOnInputs(
    int ArgC, char *ArgV[], FuzzerTestFun TestOne,
    FuzzerInitFun Init = [](int *, char ***) { return 0; });

} // namespace llvm

#endif // LLVM_FUZZMUTATE_FUZZERCLI_H
