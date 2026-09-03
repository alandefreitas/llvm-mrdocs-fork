//===--------- Definition of the SanitizerCoverage class --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SanitizerCoverage is a simple code coverage implementation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_SANITIZERCOVERAGE_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_SANITIZERCOVERAGE_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Transforms/Utils/Instrumentation.h"

namespace llvm {
class Module;
namespace vfs {
class FileSystem;
} // namespace vfs

/// Module pass that instruments functions for SanitizerCoverage.
///
/// This is the ModuleSanitizerCoverage pass used in the new pass manager. The
/// pass instruments functions for coverage, adds initialization calls to the
/// module for trace PC guards and 8bit counters if they are requested, and
/// appends globals to llvm.compiler.used.
class SanitizerCoveragePass
    : public RequiredPassInfoMixin<SanitizerCoveragePass> {
public:
  /// Construct a SanitizerCoverage pass with the given options and lists.
  /// @param Options Instrumentation options for the pass.
  /// @param VFS Virtual filesystem used to load allowlist/blocklist files.
  /// @param AllowlistFiles Paths to allowlist files restricting instrumentation.
  /// @param BlocklistFiles Paths to blocklist files excluding instrumentation.
  LLVM_ABI explicit SanitizerCoveragePass(
      SanitizerCoverageOptions Options = SanitizerCoverageOptions(),
      IntrusiveRefCntPtr<vfs::FileSystem> VFS = nullptr,
      const std::vector<std::string> &AllowlistFiles = {},
      const std::vector<std::string> &BlocklistFiles = {});
  /// Run SanitizerCoverage instrumentation over the module.
  /// @param M Module to instrument.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  SanitizerCoverageOptions Options;
  IntrusiveRefCntPtr<vfs::FileSystem> VFS;
  std::unique_ptr<SpecialCaseList> Allowlist;
  std::unique_ptr<SpecialCaseList> Blocklist;
};

} // namespace llvm

#endif
