//===- SimplifyCFG.h - Simplify and canonicalize the CFG --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for the pass responsible for both
/// simplifying and canonicalizing the CFG.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SIMPLIFYCFG_H
#define LLVM_TRANSFORMS_SCALAR_SIMPLIFYCFG_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Utils/SimplifyCFGOptions.h"

namespace llvm {

/// A pass to simplify and canonicalize the CFG of a function.
///
/// This pass iteratively simplifies the entire CFG of a function. It may change
/// or remove control flow to put the CFG into a canonical form expected by
/// other passes of the mid-level optimizer. Depending on the specified options,
/// it may further optimize control-flow to create non-canonical forms.
class SimplifyCFGPass : public OptionalPassInfoMixin<SimplifyCFGPass> {
  SimplifyCFGOptions Options;

public:
  /// Construct a SimplifyCFG pass with default options for canonical IR.
  ///
  /// The default constructor sets the pass options to create canonical IR,
  /// rather than optimal IR. That is, by default we bypass transformations that
  /// are likely to improve performance but make analysis for other passes more
  /// difficult.
  LLVM_ABI SimplifyCFGPass();

  /// Construct a pass with optional optimizations.
  /// @param PassOptions Options controlling which CFG simplifications are
  /// enabled.
  LLVM_ABI SimplifyCFGPass(const SimplifyCFGOptions &PassOptions);

  /// Run the pass over the function.
  /// @param F Function whose CFG will be simplified.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);
};
}

#endif
