//===- LowerGlobalDtors.h - Lower @llvm.global_dtors ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers @llvm.global_dtors by creating wrapper functions that are
// registered in @llvm.global_ctors and which contain a call to `__cxa_atexit`
// to register their destructor functions.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_UTILS_LOWERGLOBALDTORS_H
#define LLVM_TRANSFORMS_UTILS_LOWERGLOBALDTORS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that lowers @llvm.global_dtors via wrappers registered in
/// @llvm.global_ctors.
///
/// Creates wrapper functions that are registered in @llvm.global_ctors and
/// which contain a call to `__cxa_atexit` to register their destructor
/// functions.
class LowerGlobalDtorsPass
    : public OptionalPassInfoMixin<LowerGlobalDtorsPass> {
public:
  /// Run the lower-global-dtors pass over the module.
  /// @param M Module whose @llvm.global_dtors entries are lowered.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOWERGLOBALDTORS_H
