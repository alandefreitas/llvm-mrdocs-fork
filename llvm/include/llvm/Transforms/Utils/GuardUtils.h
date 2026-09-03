//===-- GuardUtils.h - Utils for work with guards ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Utils that are used to perform transformations related to guards and their
// conditions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_GUARDUTILS_H
#define LLVM_TRANSFORMS_UTILS_GUARDUTILS_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class CondBrInst;
class CallInst;
class Function;
class Value;

/// Splits control flow at \p Guard into an explicit branch with a deopt path.
///
/// Replaces the guard with an explicit branch on the condition of the guard's
/// first argument. The taken branch then goes to the block that contains
/// \p Guard's successors, and the non-taken branch goes to a newly-created
/// deopt block that contains a sole call of the deoptimize function
/// \p DeoptIntrinsic. If \p UseWC is set, preserve the widenable nature of the
/// guard by lowering to an equivalent form. If not set, lower to a form
/// without widenable semantics.
/// @param DeoptIntrinsic Deoptimize function to call on the non-taken path.
/// @param Guard Guard intrinsic whose control flow is made explicit.
/// @param UseWC Whether to preserve widenable-condition semantics.
LLVM_ABI void makeGuardControlFlowExplicit(Function *DeoptIntrinsic,
                                           CallInst *Guard, bool UseWC);

/// Widens a widenable branch so that \p NewCond also holds on the taken path.
///
/// Given a branch we know is widenable (defined per Analysis/GuardUtils.h),
/// widen it such that condition \p NewCond is also known to hold on the taken
/// path. The branch remains widenable after the transform.
/// @param WidenableBR Widenable conditional branch to widen.
/// @param NewCond Additional condition that must hold on the taken path.
LLVM_ABI void widenWidenableBranch(CondBrInst *WidenableBR, Value *NewCond);

/// Sets a widenable branch's condition to \p Cond while keeping it widenable.
///
/// Given a branch we know is widenable (defined per Analysis/GuardUtils.h),
/// set its condition such that (only) \p Cond is known to hold on the taken
/// path and that the branch remains widenable after the transform.
/// @param WidenableBR Widenable conditional branch whose condition is set.
/// @param Cond Condition that must hold on the taken path.
LLVM_ABI void setWidenableBranchCond(CondBrInst *WidenableBR, Value *Cond);

} // llvm

#endif // LLVM_TRANSFORMS_UTILS_GUARDUTILS_H
