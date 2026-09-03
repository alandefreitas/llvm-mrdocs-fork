//===-- KCFI.h - Generic KCFI operand bundle lowering -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass emits generic KCFI indirect call checks for targets that don't
// support lowering KCFI operand bundles in the back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_KCFI_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_KCFI_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
/// A pass that emits generic KCFI indirect call checks for targets that do not
/// lower KCFI operand bundles in the back-end.
class KCFIPass : public RequiredPassInfoMixin<KCFIPass> {
public:
  /// Run generic KCFI check lowering over the function.
  /// @param F Function to instrument.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm
#endif // LLVM_TRANSFORMS_INSTRUMENTATION_KCFI_H
