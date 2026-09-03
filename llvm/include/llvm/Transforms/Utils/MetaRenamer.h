//===- MetaRenamer.h - Rename everything with metasyntatic names ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass renames everything with metasyntatic names. The intent is to use
// this pass after llvm-reduce reduction to conceal the nature of the original
// program.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_METARENAMER_H
#define LLVM_TRANSFORMS_UTILS_METARENAMER_H

#include "llvm/IR/PassManager.h"

namespace llvm {
/// Pass that renames everything with metasyntactic names.
///
/// Intended for use after llvm-reduce reduction to conceal the nature of the
/// original program.
struct MetaRenamerPass : OptionalPassInfoMixin<MetaRenamerPass> {
  /// Run the meta-renamer pass over the module.
  /// @param M Module whose symbols should be renamed.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_METARENAMER_H
