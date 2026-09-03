//===---- CoroElide.h - Coroutine frame allocation elision ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file declares a pass that replaces dynamic allocation of coroutine
// frames with alloca and replaces calls to llvm.coro.resume and
// llvm.coro.destroy with direct calls to coroutine sub-functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_COROELIDE_H
#define LLVM_TRANSFORMS_COROUTINES_COROELIDE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass that replaces dynamic coroutine frame allocation with alloca.
struct CoroElidePass : RequiredPassInfoMixin<CoroElidePass> {
  /// Elide frame allocation and replace resume/destroy with direct calls.
  ///
  /// \param F The function to process.
  /// \param AM The function analysis manager.
  /// \return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_COROUTINES_COROELIDE_H
