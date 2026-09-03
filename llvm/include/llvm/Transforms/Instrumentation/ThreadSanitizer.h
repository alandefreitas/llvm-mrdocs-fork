//===- ThreadSanitizer.h - ThreadSanitizer instrumentation ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the thread sanitizer pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_THREADSANITIZER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_THREADSANITIZER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Function;
class Module;

/// A function pass for tsan instrumentation.
///
/// Instruments functions to detect race conditions reads. This function pass
/// inserts calls to runtime library functions. If the functions aren't declared
/// yet, the pass inserts the declarations. Otherwise the existing globals are
struct ThreadSanitizerPass : public RequiredPassInfoMixin<ThreadSanitizerPass> {
  /// Run ThreadSanitizer instrumentation over the function.
  /// @param F Function to instrument.
  /// @param FAM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

/// A module pass for tsan instrumentation.
///
/// Create ctor and init functions.
struct ModuleThreadSanitizerPass
    : public RequiredPassInfoMixin<ModuleThreadSanitizerPass> {
  /// Run ThreadSanitizer module instrumentation over the module.
  /// @param M Module to instrument.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm
#endif /* LLVM_TRANSFORMS_INSTRUMENTATION_THREADSANITIZER_H */
