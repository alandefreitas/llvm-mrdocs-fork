//===--- ExpandMemCmp.h - Expand memcmp() to load/stores --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_EXPANDMEMCMP_H
#define LLVM_TRANSFORMS_SCALAR_EXPANDMEMCMP_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that expands memcmp calls into loads and stores.
class ExpandMemCmpPass : public OptionalPassInfoMixin<ExpandMemCmpPass> {
public:
  /// Run the memcmp expansion pass over the function.
  /// @param F Function whose memcmp calls may be expanded.
  /// @param FAM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_EXPANDMEMCMP_H
