//===---- CoroConditionalWrapper.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_COROCONDITIONALWRAPPER_H
#define LLVM_TRANSFORMS_COROUTINES_COROCONDITIONALWRAPPER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Module pass that runs nested passes only when the module declares coroutine
/// intrinsics.
struct CoroConditionalWrapper : RequiredPassInfoMixin<CoroConditionalWrapper> {
  /// Construct a wrapper around the given module pass manager.
  /// @param PM Pass manager to run when coroutine intrinsics are present.
  LLVM_ABI CoroConditionalWrapper(ModulePassManager &&PM);
  /// Run the nested passes on \p M if it declares any coroutine intrinsics.
  /// @param M Module to process.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  ModulePassManager PM;
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_COROUTINES_COROCONDITIONALWRAPPER_H
