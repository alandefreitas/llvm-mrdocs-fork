//===--------------------- llvm/CodeGen/SafeStack.h -------------*- C++-*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SAFESTACK_H
#define LLVM_CODEGEN_SAFESTACK_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

/// New PM pass that splits the stack into a safe stack and an unsafe stack.
///
/// The safe stack is left for the LLVM backend; the unsafe stack is allocated
/// and managed through the runtime support library.
class SafeStackPass : public RequiredPassInfoMixin<SafeStackPass> {
  const TargetMachine *TM;

public:
  /// Construct a SafeStack pass for target machine \p TM_.
  /// \param TM_ Target machine used when placing unsafe stack objects.
  explicit SafeStackPass(const TargetMachine &TM_) : TM(&TM_) {}
  /// Split the stack of \p F into safe and unsafe regions when requested.
  /// \param F Function to transform.
  /// \param FAM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_SAFESTACK_H
