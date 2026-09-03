//===-- CrossDSOCFI.cpp - Externalize this module's CFI checks --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass exports all llvm.bitset's found in the module in the form of a
// __cfi_check function, which can be used to verify cross-DSO call targets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_CROSSDSOCFI_H
#define LLVM_TRANSFORMS_IPO_CROSSDSOCFI_H

#include "llvm/IR/PassManager.h"

namespace llvm {
/// Pass that exports this module's CFI checks for cross-DSO verification.
///
/// Exports all llvm.bitset's found in the module in the form of a
/// __cfi_check function, which can be used to verify cross-DSO call targets.
class CrossDSOCFIPass : public OptionalPassInfoMixin<CrossDSOCFIPass> {
public:
  /// Run cross-DSO CFI export over the given module.
  ///
  /// \param M Module whose bitsets are exported as a __cfi_check function.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};
}
#endif // LLVM_TRANSFORMS_IPO_CROSSDSOCFI_H

