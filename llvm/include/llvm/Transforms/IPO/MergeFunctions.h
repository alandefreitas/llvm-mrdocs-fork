//===- MergeFunctions.h - Merge Identical Functions -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass transforms simple global variables that never have their address
// taken.  If obviously true, it marks read/write globals as constant, deletes
// variables only stored to, etc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_MERGEFUNCTIONS_H
#define LLVM_TRANSFORMS_IPO_MERGEFUNCTIONS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Module;
class Function;

/// Merge identical functions.
class MergeFunctionsPass : public OptionalPassInfoMixin<MergeFunctionsPass> {
public:
  /// Run function merging over the given module.
  ///
  /// \param M Module whose identical functions are merged.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  /// Merge identical functions in the given module.
  ///
  /// \param M Module whose identical functions are merged.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \returns True if any functions were merged.
  LLVM_ABI static bool runOnModule(Module &M, ModuleAnalysisManager &AM);

  /// Merge identical functions from the given set.
  ///
  /// \param Funcs Functions to consider for merging.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \returns Map from each merged function to the function it was replaced
  /// with.
  LLVM_ABI static DenseMap<Function *, Function *>
  runOnFunctions(ArrayRef<Function *> Funcs, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_MERGEFUNCTIONS_H
