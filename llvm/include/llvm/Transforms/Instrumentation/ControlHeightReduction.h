//===- ControlHeightReduction.h - Control Height Reduction ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass merges conditional blocks of code and reduces the number of
// conditional branches in the hot paths based on profiles.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_CONTROLHEIGHTREDUCTION_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_CONTROLHEIGHTREDUCTION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that merges conditional blocks to reduce branches on hot paths.
///
/// Merges conditional blocks of code and reduces the number of conditional
/// branches in the hot paths based on profiles.
class ControlHeightReductionPass
    : public OptionalPassInfoMixin<ControlHeightReductionPass> {
public:
  /// Construct a control-height reduction pass.
  LLVM_ABI ControlHeightReductionPass();
  /// Run control-height reduction on \p F using profile information.
  /// @param F Function to transform.
  /// @param FAM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_CONTROLHEIGHTREDUCTION_H
