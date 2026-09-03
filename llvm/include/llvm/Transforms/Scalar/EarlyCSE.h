//===- EarlyCSE.h - Simple and fast CSE pass --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for a simple, fast CSE pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_EARLYCSE_H
#define LLVM_TRANSFORMS_SCALAR_EARLYCSE_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Function;

/// A simple and fast domtree-based CSE pass.
///
/// This pass does a simple depth-first walk over the dominator tree,
/// eliminating trivially redundant instructions and using instsimplify to
/// canonicalize things as it goes. It is intended to be fast and catch obvious
/// cases so that instcombine and other passes are more effective. It is
/// expected that a later pass of GVN will catch the interesting/hard cases.
struct EarlyCSEPass : OptionalPassInfoMixin<EarlyCSEPass> {
  /// Construct an EarlyCSE pass, optionally using MemorySSA.
  /// @param UseMemorySSA When true, use MemorySSA for more precise CSE of
  /// memory operations.
  EarlyCSEPass(bool UseMemorySSA = false) : UseMemorySSA(UseMemorySSA) {}

  /// Run the pass over the function.
  /// @param F Function to run early CSE on.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

  /// Whether this pass should use MemorySSA for CSE of memory operations.
  bool UseMemorySSA;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_EARLYCSE_H
