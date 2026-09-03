//===- AllocToken.h - Allocation token instrumentation --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the AllocTokenPass, an instrumentation pass that
// replaces allocation calls with ones including an allocation token.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ALLOCTOKEN_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ALLOCTOKEN_H

#include "llvm/IR/Analysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/AllocToken.h"
#include <optional>

namespace llvm {

class Module;

/// Options that control AllocToken instrumentation.
struct AllocTokenOptions {
  /// Mode used to assign allocation token IDs.
  AllocTokenMode Mode = DefaultAllocTokenMode;
  /// Maximum number of distinct token IDs (0 means target SIZE_MAX).
  uint64_t MaxTokens = 0;
  /// Encode the token ID in the allocation function name instead of an argument.
  bool FastABI = false;
  /// Also instrument custom allocation functions marked with !alloc_token.
  bool Extended = false;
  /// Construct AllocToken options with default values.
  AllocTokenOptions() = default;
};

/// A module pass that rewrites heap allocations to use token-enabled
/// allocation functions based on various source-level properties.
class AllocTokenPass : public RequiredPassInfoMixin<AllocTokenPass> {
public:
  /// Construct an AllocToken module pass with the given options.
  /// @param Opts Instrumentation options for the pass.
  LLVM_ABI explicit AllocTokenPass(AllocTokenOptions Opts = {});
  /// Run AllocToken instrumentation over the module.
  /// @param M Module to instrument.
  /// @param MAM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  const AllocTokenOptions Options;
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_ALLOCTOKEN_H
