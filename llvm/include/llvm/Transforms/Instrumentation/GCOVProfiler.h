//===- Transforms/Instrumentation/GCOVProfiler.h ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for the GCOV style profiler  pass.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_GCOVPROFILER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_GCOVPROFILER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Transforms/Utils/Instrumentation.h"

namespace llvm {
/// The gcov-style instrumentation pass
class GCOVProfilerPass : public OptionalPassInfoMixin<GCOVProfilerPass> {
public:
  /// Construct a GCOV-style instrumentation pass.
  /// @param Options Options controlling GCOV note/data emission and filters.
  /// @param VFS File system used to write .gcno note files.
  GCOVProfilerPass(
      const GCOVOptions &Options = GCOVOptions::getDefault(),
      IntrusiveRefCntPtr<vfs::FileSystem> VFS = vfs::getRealFileSystem())
      : GCOVOpts(Options), VFS(std::move(VFS)) {}
  /// Instrument \p M with GCOV-style edge counters and emit .gcno notes.
  /// @param M Module to instrument.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  GCOVOptions GCOVOpts;
  IntrusiveRefCntPtr<vfs::FileSystem> VFS;
};

} // namespace llvm
#endif
