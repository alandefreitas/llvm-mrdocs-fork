//===- SampleProfile.h - SamplePGO pass ---------- --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for the sampled PGO loader pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_SAMPLEPROFILE_H
#define LLVM_TRANSFORMS_IPO_SAMPLEPROFILE_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {

class Module;

/// Hot callsite threshold for priority-based sample profile loader inlining.
LLVM_ABI extern cl::opt<int> SampleHotCallSiteThreshold;
/// Threshold for inlining cold callsites under sample profile loading.
LLVM_ABI extern cl::opt<int> SampleColdCallSiteThreshold;
/// Size growth ratio limit for priority-based sample profile loader inlining.
LLVM_ABI extern cl::opt<int> ProfileInlineGrowthLimit;
/// Lower bound of size growth limit for priority-based sample profile inlining.
LLVM_ABI extern cl::opt<int> ProfileInlineLimitMin;
/// Upper bound of size growth limit for priority-based sample profile inlining.
LLVM_ABI extern cl::opt<int> ProfileInlineLimitMax;
/// Sort profiled recursion SCC members by edge weights.
LLVM_ABI extern cl::opt<bool> SortProfiledSCC;

namespace vfs {
class FileSystem;
} // namespace vfs

/// The sample profiler data loader pass.
class SampleProfileLoaderPass
    : public OptionalPassInfoMixin<SampleProfileLoaderPass> {
public:
  /// Construct a sample profile loader pass.
  ///
  /// \param File Path to the sample profile file; empty uses the default.
  /// \param RemappingFile Optional profile remapping file path.
  /// \param LTOPhase Thin/full LTO phase in which this pass runs.
  /// \param FS File system used to read profile and remapping files.
  /// \param DisableSampleProfileInlining If true, skip inline transforms.
  /// \param UseFlattenedProfile If true, load a flattened sample profile.
  LLVM_ABI SampleProfileLoaderPass(
      std::string File = "", std::string RemappingFile = "",
      ThinOrFullLTOPhase LTOPhase = ThinOrFullLTOPhase::None,
      IntrusiveRefCntPtr<vfs::FileSystem> FS = nullptr,
      bool DisableSampleProfileInlining = false,
      bool UseFlattenedProfile = false);

  /// Run sample profile loading over the given module.
  ///
  /// \param M Module annotated with sample-profile metadata.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  std::string ProfileFileName;
  std::string ProfileRemappingFileName;
  const ThinOrFullLTOPhase LTOPhase;
  IntrusiveRefCntPtr<vfs::FileSystem> FS;
  bool DisableSampleProfileInlining;
  bool UseFlattenedProfile;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_SAMPLEPROFILE_H
