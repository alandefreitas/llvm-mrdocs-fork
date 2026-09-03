//===-- ExtractGV.h -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_EXTRACTGV_H
#define LLVM_TRANSFORMS_IPO_EXTRACTGV_H

#include "llvm/ADT/SetVector.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class GlobalValue;

/// Pass that extracts or deletes specified global values from a module.
class ExtractGVPass : public OptionalPassInfoMixin<ExtractGVPass> {
private:
  SetVector<GlobalValue *> Named;
  bool deleteStuff;
  bool keepConstInit;

public:
  /// Construct a global-value extraction pass for the given globals.
  ///
  /// \param GVs Global values to extract or delete.
  /// \param deleteS If true, delete the specified global values; otherwise
  /// delete everything except those values.
  /// \param keepConstInit If true, keep initializers of constant globals that
  /// would otherwise be deleted.
  LLVM_ABI ExtractGVPass(std::vector<GlobalValue *> &GVs, bool deleteS = true,
                         bool keepConstInit = false);

  /// Run global-value extraction over the given module.
  ///
  /// \param M Module whose global values are extracted or deleted.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_EXTRACTGV_H
