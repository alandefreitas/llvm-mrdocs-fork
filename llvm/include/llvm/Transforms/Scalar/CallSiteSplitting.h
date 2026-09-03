//===- CallSiteSplitting..h - Callsite Splitting ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_CALLSITESPLITTING_H
#define LLVM_TRANSFORMS_SCALAR_CALLSITESPLITTING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass that splits call sites to expose constrained arguments.
///
/// When a call-site argument is predicated in the control flow, this pass
/// tries to split the call site so later passes (inliner, jump threading,
/// IPA-CP based cloning, and similar) see more constrained arguments.
struct CallSiteSplittingPass : OptionalPassInfoMixin<CallSiteSplittingPass> {
  /// Run call site splitting over the function.
  /// @param F Function whose call sites may be split.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_CALLSITESPLITTING_H
