//===- StructurizeCFG.h ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_STRUCTURIZECFG_H
#define LLVM_TRANSFORMS_SCALAR_STRUCTURIZECFG_H

#include "llvm/IR/PassManager.h"

namespace llvm {
/// Pass that transforms regions into structured control flow.
///
/// Rewrites arbitrary CFGs into a structured form (if/then/else and loops)
/// suitable for targets that require structured control flow. Optionally
/// skips regions that are already uniform.
struct StructurizeCFGPass : RequiredPassInfoMixin<StructurizeCFGPass> {
private:
  bool SkipUniformRegions;

public:
  /// Construct a StructurizeCFG pass.
  /// @param SkipUniformRegions If true, leave uniform regions unchanged.
  LLVM_ABI StructurizeCFGPass(bool SkipUniformRegions = false);

  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

  /// Run the pass over the function.
  /// @param F Function whose CFG will be structurized.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_STRUCTURIZECFG_H
