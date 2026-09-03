//===- InstCount.h - Collects the count of all instructions -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass collects the count of all instructions and reports them
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_INSTCOUNT_H
#define LLVM_ANALYSIS_INSTCOUNT_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass that counts instructions and reports them as statistics.
class InstCountPass : public RequiredPassInfoMixin<InstCountPass> {
  bool IsPreOptimization;

public:
  /// Construct an instruction-count pass, optionally as pre-optimization.
  /// @param IsPreOptimization When true, record stats as pre-optimization.
  explicit InstCountPass(bool IsPreOptimization = false)
      : IsPreOptimization(IsPreOptimization) {}

  /// Count instructions in \p F and preserve all analyses.
  /// @param F Function whose instructions are counted.
  /// @param FAM Function analysis manager (unused).
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_INSTCOUNT_H
