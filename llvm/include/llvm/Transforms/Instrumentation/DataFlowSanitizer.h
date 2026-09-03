//===- DataFlowSanitizer.h - dynamic data flow analysis ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_DATAFLOWSANITIZER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_DATAFLOWSANITIZER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <string>
#include <vector>

namespace llvm {
class Module;

/// Module pass that instruments code for dynamic data-flow analysis (DFSan).
///
/// Propagates taint labels through memory and computation and inserts calls to
/// the DFSan runtime. Optional ABI list files describe how external functions
/// interact with labels.
class DataFlowSanitizerPass
    : public RequiredPassInfoMixin<DataFlowSanitizerPass> {
private:
  std::vector<std::string> ABIListFiles;
  IntrusiveRefCntPtr<vfs::FileSystem> FS;

public:
  /// Construct a DataFlowSanitizer pass with optional ABI lists and filesystem.
  /// @param ABIListFiles Paths to ABI list files describing external functions.
  /// @param FS Filesystem used to read ABI list files (defaults to the real FS).
  DataFlowSanitizerPass(
      const std::vector<std::string> &ABIListFiles = std::vector<std::string>(),
      IntrusiveRefCntPtr<vfs::FileSystem> FS = vfs::getRealFileSystem())
      : ABIListFiles(ABIListFiles), FS(std::move(FS)) {}
  /// Run DataFlowSanitizer instrumentation over the module.
  /// @param M Module to instrument.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif
