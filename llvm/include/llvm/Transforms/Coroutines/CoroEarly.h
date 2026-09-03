//===---- CoroEarly.h - Lower early coroutine intrinsics --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file provides the interface to the early coroutine intrinsic lowering
// pass. This pass lowers coroutine intrinsics that hide the details of the
// exact calling convention for coroutine resume and destroy functions and
// details of the structure of the coroutine frame.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_COROEARLY_H
#define LLVM_TRANSFORMS_COROUTINES_COROEARLY_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Pass that lowers early coroutine intrinsics.
struct CoroEarlyPass : RequiredPassInfoMixin<CoroEarlyPass> {
  /// Lower early coroutine intrinsics in module \p M.
  ///
  /// \param M The module to process.
  /// \param AM The module analysis manager.
  /// \return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_COROUTINES_COROEARLY_H
