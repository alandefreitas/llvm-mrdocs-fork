//===- LowerAllowCheckPass.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for the pass responsible for removing
/// expensive ubsan checks.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_LOWERALLOWCHECKPASS_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_LOWERALLOWCHECKPASS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Pass that lowers allow-check intrinsics to remove optional traps from hot
/// code.
///
/// Removes optional traps, like llvm.ubsantrap, from the hot code by replacing
/// allow-check intrinsics with constants based on profile information.
class LowerAllowCheckPass : public RequiredPassInfoMixin<LowerAllowCheckPass> {
public:
  /// Configuration options for lowering allow-check intrinsics.
  struct Options {
    /// Per-kind hot-percentile cutoffs for `llvm.allow.ubsan.check`.
    std::vector<unsigned int> cutoffs;
    /// Hot-percentile cutoff for `llvm.allow.runtime_check` (0 disables).
    unsigned int runtime_check = 0;
  };

  /// Construct a lower-allow-check pass with the given options.
  /// @param Opts Cutoff options for the pass.
  explicit LowerAllowCheckPass(LowerAllowCheckPass::Options Opts)
      : Opts(std::move(Opts)) {};
  /// Lower allow-check intrinsics in the function.
  /// @param F Function to transform.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// Return true if this pass was requested via command-line options.
  /// @return True if this pass was requested via command-line options.
  LLVM_ABI static bool IsRequested();
  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  LowerAllowCheckPass::Options Opts;
};

} // namespace llvm

#endif
