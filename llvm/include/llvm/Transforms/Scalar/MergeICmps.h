//===- MergeICmps.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_MERGEICMPS_H
#define LLVM_TRANSFORMS_SCALAR_MERGEICMPS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass that merges chains of integer comparisons into a memcmp call.
struct MergeICmpsPass : OptionalPassInfoMixin<MergeICmpsPass> {
  /// Run merge-icmp optimizations over the function.
  /// @param F Function whose integer comparison chains may be merged.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_MERGEICMPS_H
