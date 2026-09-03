//===--- MemProfInstrumentation.h - Memory profiler instrumentation ----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MemProf instrumentation pass classes.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_MEMPROF_INSTRUMENTATION_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_MEMPROF_INSTRUMENTATION_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Function;
class Module;

/// Public interface to the memory profiler pass for instrumenting code to
/// profile memory accesses.
///
/// The profiler itself is a function pass that works by inserting various
/// calls to the MemProfiler runtime library functions. The runtime library
/// essentially replaces malloc() and free() with custom implementations that
/// record data about the allocations.
class MemProfilerPass : public RequiredPassInfoMixin<MemProfilerPass> {
public:
  /// Construct a MemProfiler function pass.
  LLVM_ABI explicit MemProfilerPass();
  /// Run MemProfiler instrumentation over the function.
  /// @param F Function to instrument.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Public interface to the memory profiler module pass for instrumenting code
/// to profile memory allocations and accesses.
class ModuleMemProfilerPass
    : public RequiredPassInfoMixin<ModuleMemProfilerPass> {
public:
  /// Construct a MemProfiler module pass.
  LLVM_ABI explicit ModuleMemProfilerPass();
  /// Run MemProfiler module instrumentation over the module.
  /// @param M Module to instrument.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif
