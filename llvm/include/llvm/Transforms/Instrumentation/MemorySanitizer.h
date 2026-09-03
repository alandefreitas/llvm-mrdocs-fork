//===- MemorySanitizer.h - MemorySanitizer instrumentation ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the memoy sanitizer pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_MEMORYSANITIZER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_MEMORYSANITIZER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Module;
class StringRef;
class raw_ostream;

/// Options that control MemorySanitizer instrumentation.
struct MemorySanitizerOptions {
  /// Construct options with all features disabled.
  MemorySanitizerOptions() : MemorySanitizerOptions(0, false, false, false){};
  /// Construct options with the given origin-tracking, recovery, and kernel
  /// settings.
  /// @param TrackOrigins Origin-tracking depth (0 disables tracking).
  /// @param Recover Continue after detecting an error instead of terminating.
  /// @param Kernel Instrument for KernelMemorySanitizer instead of user-space
  ///        MSan.
  MemorySanitizerOptions(int TrackOrigins, bool Recover, bool Kernel)
      : MemorySanitizerOptions(TrackOrigins, Recover, Kernel, false) {}
  /// Construct options with the given instrumentation settings.
  /// @param TrackOrigins Origin-tracking depth (0 disables tracking).
  /// @param Recover Continue after detecting an error instead of terminating.
  /// @param Kernel Instrument for KernelMemorySanitizer instead of user-space
  ///        MSan.
  /// @param EagerChecks Enable early checks on function arguments.
  LLVM_ABI MemorySanitizerOptions(int TrackOrigins, bool Recover, bool Kernel,
                                  bool EagerChecks);
  /// Instrument for KernelMemorySanitizer instead of user-space MSan.
  bool Kernel;
  /// Origin-tracking depth (0 disables tracking).
  int TrackOrigins;
  /// Continue after detecting an error instead of terminating.
  bool Recover;
  /// Enable early checks on function arguments.
  bool EagerChecks;
};

/// A module pass for msan instrumentation.
///
/// Instruments functions to detect unitialized reads. This function pass
/// inserts calls to runtime library functions. If the functions aren't declared
/// yet, the pass inserts the declarations. Otherwise the existing globals are
/// used.
struct MemorySanitizerPass : public RequiredPassInfoMixin<MemorySanitizerPass> {
  /// Construct a MemorySanitizer pass with the given options.
  /// @param Options Instrumentation options for the pass.
  MemorySanitizerPass(MemorySanitizerOptions Options) : Options(Options) {}

  /// Run MemorySanitizer instrumentation over the module.
  /// @param M Module to instrument.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  MemorySanitizerOptions Options;
};
}

#endif /* LLVM_TRANSFORMS_INSTRUMENTATION_MEMORYSANITIZER_H */
