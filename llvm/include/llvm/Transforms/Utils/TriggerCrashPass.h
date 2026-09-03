//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM Exceptions
//
//===----------------------------------------------------------------------===//
//
// This file provides passes that trigger crashes for testing purposes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_TRIGGERCRASHPASS_H
#define LLVM_TRANSFORMS_UTILS_TRIGGERCRASHPASS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

/// Pass that aborts when run over a module, for testing crash handling.
class TriggerCrashModulePass
    : public OptionalPassInfoMixin<TriggerCrashModulePass> {
public:
  /// Abort immediately to test crash handling in the module pipeline.
  /// @param M Module on which the pass is invoked.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Pass that aborts when run over a function, for testing crash handling.
class TriggerCrashFunctionPass
    : public OptionalPassInfoMixin<TriggerCrashFunctionPass> {
public:
  /// Abort immediately to test crash handling in the function pipeline.
  /// @param F Function on which the pass is invoked.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Create a legacy pass manager instance of the trigger-crash function pass.
/// @return A new FunctionPass that aborts when run.
LLVM_ABI FunctionPass *createTriggerCrashFunctionPass();

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_TRIGGERCRASHPASS_H
