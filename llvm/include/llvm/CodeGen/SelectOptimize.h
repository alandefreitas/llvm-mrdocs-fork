//===--- llvm/CodeGen/SelectOptimize.h ---------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the SelectOptimizePass class,
/// its corresponding pass name is `select-optimize`.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTOPTIMIZE_H
#define LLVM_CODEGEN_SELECTOPTIMIZE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

/// New PM pass that converts select instructions to branches when profitable.
class SelectOptimizePass : public OptionalPassInfoMixin<SelectOptimizePass> {
  const TargetMachine *TM;

public:
  /// Construct a SelectOptimize pass for target machine \p TM.
  /// \param TM Target machine used when deciding select-to-branch conversions.
  explicit SelectOptimizePass(const TargetMachine &TM) : TM(&TM) {}
  /// Optimize select instructions in \p F by converting them to branches when
  /// profitable.
  /// \param F Function whose selects are optimized.
  /// \param FAM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_SELECTOPTIMIZE_H
