//===- MaterializationUtils.h - Utilities for doing materialization -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Coroutines/SuspendCrossingInfo.h"

#ifndef LLVM_TRANSFORMS_COROUTINES_MATERIALIZATIONUTILS_H
#define LLVM_TRANSFORMS_COROUTINES_MATERIALIZATIONUTILS_H

namespace llvm::coro {

/// Return true if \p I is trivially rematerializable.
///
/// Examples include InsertElementInst.
///
/// \param I Instruction to test for trivial rematerializability.
/// \return true if \p I is trivially rematerializable.
LLVM_ABI bool isTriviallyMaterializable(Instruction &I);

/// Rematerialize instructions across suspend points instead of spilling them.
///
/// Invoked from buildCoroutineFrame.
///
/// \param F The coroutine function to rewrite.
/// \param Checker Suspend-crossing analysis used to find rematerialization
///        candidates.
/// \param IsMaterializable Callback that returns true for instructions that
///        may be rematerialized instead of spilled into the frame.
LLVM_ABI void
doRematerializations(Function &F, SuspendCrossingInfo &Checker,
                     std::function<bool(Instruction &)> IsMaterializable);

} // namespace llvm::coro

#endif // LLVM_TRANSFORMS_COROUTINES_MATERIALIZATIONUTILS_H
