//===-- CoroCleanup.h - Lower all coroutine related intrinsics --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file delcares a pass that lowers all remaining coroutine intrinsics.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_COROCLEANUP_H
#define LLVM_TRANSFORMS_COROUTINES_COROCLEANUP_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Pass that lowers all remaining coroutine intrinsics.
struct CoroCleanupPass : RequiredPassInfoMixin<CoroCleanupPass> {
  /// Lower remaining coroutine intrinsics in module \p M.
  ///
  /// \param M The module to process.
  /// \param MAM The module analysis manager.
  /// \return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_COROUTINES_COROCLEANUP_H
