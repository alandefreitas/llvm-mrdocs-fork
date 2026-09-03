//===- RewriteStatepointsForGC.h - ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides interface to "Rewrite Statepoints for GC" pass.
//
// This passe rewrites call/invoke instructions so as to make potential
// relocations performed by the garbage collector explicit in the IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_REWRITESTATEPOINTSFORGC_H
#define LLVM_TRANSFORMS_SCALAR_REWRITESTATEPOINTSFORGC_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class DominatorTree;
class Function;
class Module;
class TargetTransformInfo;
class TargetLibraryInfo;

/// Pass that rewrites calls and invokes to make GC relocations explicit in IR.
///
/// Transforms call/invoke sites so potential relocations performed by the
/// garbage collector are represented explicitly, after PlaceSafepoints has
/// inserted safepoints.
struct RewriteStatepointsForGC
    : public OptionalPassInfoMixin<RewriteStatepointsForGC> {
  /// Run rewrite-statepoints-for-GC over the module.
  /// @param M Module whose functions may need statepoint rewriting.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  /// Rewrite statepoints in \p F using the given analyses.
  /// @param F Function to transform.
  /// @param DT Dominator tree for the function.
  /// @param TTI Target transform info used by the rewrite.
  /// @param TLI Target library info used by the rewrite.
  /// @return True if the function was changed.
  LLVM_ABI bool runOnFunction(Function &F, DominatorTree &DT,
                              TargetTransformInfo &TTI,
                              const TargetLibraryInfo &TLI);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_REWRITESTATEPOINTSFORGC_H
