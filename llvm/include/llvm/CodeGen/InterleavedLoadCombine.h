//===- llvm/CodeGen/InterleavedLoadCombine.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_INTERLEAVEDLOADCOMBINE_H
#define LLVM_CODEGEN_INTERLEAVEDLOADCOMBINE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

/// New PM pass that combines interleaved loads into wide loads.
///
/// Identifies interleaved load patterns and combines them into wide loads that
/// InterleavedAccessPass can detect and lower to target-specific intrinsics.
class InterleavedLoadCombinePass
    : public OptionalPassInfoMixin<InterleavedLoadCombinePass> {
  const TargetMachine *TM;

public:
  /// Construct an interleaved-load-combine pass for target machine \p TM.
  /// \param TM Target machine used when combining interleaved loads.
  explicit InterleavedLoadCombinePass(const TargetMachine &TM) : TM(&TM) {}

  /// Combine interleaved loads into wide loads in function \p F.
  /// \param F Function whose interleaved loads are combined.
  /// \param FAM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // InterleavedLoadCombine
