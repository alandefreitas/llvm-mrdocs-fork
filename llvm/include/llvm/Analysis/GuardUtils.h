//===-- GuardUtils.h - Utils for work with guards ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Utils that are used to perform analyzes related to guards and their
// conditions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_GUARDUTILS_H
#define LLVM_ANALYSIS_GUARDUTILS_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class BasicBlock;
class Use;
class User;
class Value;
template <typename T> class SmallVectorImpl;

/// Returns true iff \p U has semantics of a guard expressed in a form of call
/// of llvm.experimental.guard intrinsic.
/// @param U User to check for guard semantics.
/// @return True if \p U has llvm.experimental.guard semantics.
LLVM_ABI bool isGuard(const User *U);

/// Returns true iff \p V has semantics of llvm.experimental.widenable.condition
/// call.
/// @param V Value to check for a widenable condition.
/// @return True if \p V is an llvm.experimental.widenable.condition call.
LLVM_ABI bool isWidenableCondition(const Value *V);

/// Returns true iff \p U is a widenable branch (that is,
/// extractWidenableCondition returns widenable condition).
/// @param U User to check for a widenable branch.
/// @return True if \p U is a widenable branch.
LLVM_ABI bool isWidenableBranch(const User *U);

/// Returns true iff \p U has semantics of a guard expressed in a form of a
/// widenable conditional branch to deopt block.
/// @param U User to check for a guard-as-widenable-branch pattern.
/// @return True if \p U is a guard expressed as a widenable branch.
LLVM_ABI bool isGuardAsWidenableBranch(const User *U);

/// Parses a widenable branch and returns its condition, widenable condition,
/// and successor blocks.
///
/// If U is widenable branch looking like:
///   %cond = ...
///   %wc = call i1 @llvm.experimental.widenable.condition()
///   %branch_cond = and i1 %cond, %wc
///   br i1 %branch_cond, label %if_true_bb, label %if_false_bb ; <--- U
/// The function returns true, and the values %cond and %wc and blocks
/// %if_true_bb, if_false_bb are returned in
/// the parameters (Condition, WidenableCondition, IfTrueBB and IfFalseBB)
/// respectively. If \p U does not match this pattern, return false.
/// @param U User expected to be the widenable branch.
/// @param Condition Set to the non-widenable part of the branch condition.
/// @param WidenableCondition Set to the widenable.condition call result.
/// @param IfTrueBB Set to the true successor of the branch.
/// @param IfFalseBB Set to the false successor of the branch.
/// @return True if \p U matches the widenable branch pattern.
LLVM_ABI bool parseWidenableBranch(const User *U, Value *&Condition,
                                   Value *&WidenableCondition,
                                   BasicBlock *&IfTrueBB,
                                   BasicBlock *&IfFalseBB);

/// Parses a widenable branch and returns Uses so they can be modified.
///
/// Analogous to the above, but return the Uses so that they can be
/// modified. Unlike previous version, Condition is optional and may be null.
/// @param U User expected to be the widenable branch.
/// @param Cond Set to the Use of the non-widenable condition, or null.
/// @param WC Set to the Use of the widenable.condition call result.
/// @param IfTrueBB Set to the true successor of the branch.
/// @param IfFalseBB Set to the false successor of the branch.
/// @return True if \p U matches the widenable branch pattern.
LLVM_ABI bool parseWidenableBranch(User *U, Use *&Cond, Use *&WC,
                                   BasicBlock *&IfTrueBB,
                                   BasicBlock *&IfFalseBB);

/// Collects the individual checks from a widenable guard's condition.
///
/// The guard condition is expected to be in form of:
///   cond1 && cond2 && cond3 ...
/// or in case of widenable branch:
///   cond1 && cond2 && cond3 && widenable_condition ...
/// Method collects the list of checks, but skips widenable_condition.
/// @param U Guard or widenable branch whose checks are collected.
/// @param Checks Filled with the non-widenable conjuncts of the condition.
LLVM_ABI void parseWidenableGuard(const User *U,
                                  llvm::SmallVectorImpl<Value *> &Checks);

/// Returns the widenable condition in \p U's expression tree, if uniquely used.
///
/// Returns widenable_condition if it exists in the expression tree rooting from
/// \p U and has only one use.
/// @param U Root of the expression tree to search.
/// @return The uniquely used widenable.condition value, or nullptr if none.
LLVM_ABI Value *extractWidenableCondition(const User *U);
} // llvm

#endif // LLVM_ANALYSIS_GUARDUTILS_H
