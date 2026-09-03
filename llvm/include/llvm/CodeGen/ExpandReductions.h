//===- ExpandReductions.h - Expand reduction intrinsics ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_EXPANDREDUCTIONS_H
#define LLVM_CODEGEN_EXPANDREDUCTIONS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// New PM pass that expands reduction intrinsics for the target.
///
/// Implements IR expansion for reduction intrinsics, allowing targets to keep
/// the intrinsics until just before codegen.
class ExpandReductionsPass
    : public RequiredPassInfoMixin<ExpandReductionsPass> {
public:
  /// Expand reduction intrinsics in \p F.
  /// \param F Function whose reduction intrinsics are expanded.
  /// \param AM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_CODEGEN_EXPANDREDUCTIONS_H
