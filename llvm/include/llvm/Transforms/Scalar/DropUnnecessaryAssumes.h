//===------------------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pass that drops assumes that are unlikely to be useful.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_DROPUNNECESSARYASSUMES_H
#define LLVM_TRANSFORMS_SCALAR_DROPUNNECESSARYASSUMES_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that drops assume intrinsics that are unlikely to be useful.
///
/// Removes \@llvm.assume calls and selected operand bundles whose affected
/// values are only used ephemerally, so the assume provides no lasting
/// information for later optimizations.
struct DropUnnecessaryAssumesPass
    : public OptionalPassInfoMixin<DropUnnecessaryAssumesPass> {
  /// Construct a pass, optionally also dropping dereferenceable bundles.
  /// @param DropDereferenceable When true, also drop "dereferenceable"
  /// operand bundles (for example after loop vectorization).
  DropUnnecessaryAssumesPass(bool DropDereferenceable = false)
      : DropDereferenceable(DropDereferenceable) {}

  /// Run the pass over the function.
  /// @param F Function whose assumes may be dropped.
  /// @param AM Function analysis manager providing AssumptionAnalysis.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  bool DropDereferenceable;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_DROPUNNECESSARYASSUMES_H
