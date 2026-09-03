//===- llvm/Transforms/Utils.h - Utility Transformations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes for accessor functions that expose passes
// in the Utils transformations library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_H
#define LLVM_TRANSFORMS_UTILS_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class ModulePass;
class FunctionPass;
class Pass;

//===----------------------------------------------------------------------===//
/// Create a pass that removes invoke instructions, converting them to calls.
/// @return An owning pointer to the newly created FunctionPass.
LLVM_ABI FunctionPass *createLowerInvokePass();

/// Pass ID for the LowerInvoke pass.
LLVM_ABI extern char &LowerInvokePassID;

//===----------------------------------------------------------------------===//
/// Create a pass that converts SwitchInst instructions into chained binary
/// branches.
/// @return An owning pointer to the newly created FunctionPass.
LLVM_ABI FunctionPass *createLowerSwitchPass();

/// Pass ID for the LowerSwitch pass.
LLVM_ABI extern char &LowerSwitchID;

//===----------------------------------------------------------------------===//
/// Create a post-inlining pass that instruments function entry and exit.
///
/// Instruments function entry/exit with calls to mcount(),
/// @__cyg_profile_func_{enter,exit} and the like. There are two variants,
/// intended to run pre- and post-inlining, respectively. Only the
/// post-inlining variant is used with the legacy pass manager.
/// @return An owning pointer to the newly created FunctionPass.
LLVM_ABI FunctionPass *createPostInlineEntryExitInstrumenterPass();

//===----------------------------------------------------------------------===//
/// Create a pass that breaks all critical edges in the CFG.
///
/// Inserts a dummy basic block on each critical edge. This pass may be
/// "required" by passes that cannot deal with critical edges. This pass
/// obviously invalidates the CFG, but can update forward dominator (set,
/// immediate dominators, tree, and frontier) information.
/// @return An owning pointer to the newly created FunctionPass.
LLVM_ABI FunctionPass *createBreakCriticalEdgesPass();

/// Pass ID for the BreakCriticalEdges pass.
///
/// Passes that cannot deal with critical edges can require this pass with:
///
///   AU.addRequiredID(BreakCriticalEdgesID);
LLVM_ABI extern char &BreakCriticalEdgesID;

//===----------------------------------------------------------------------===//
/// Create a pass that inserts phi nodes at loop boundaries to form LCSSA.
///
/// This simplifies other loop optimizations.
/// @return An owning pointer to the newly created Pass.
LLVM_ABI Pass *createLCSSAPass();

/// Pass ID for the LCSSA pass.
LLVM_ABI extern char &LCSSAID;

//===----------------------------------------------------------------------===//
/// Create a pass that promotes memory references to register references.
///
/// A simple example of the transformation performed by this pass is:
///
///        FROM CODE                           TO CODE
///   %X = alloca i32, i32 1                 ret i32 42
///   store i32 42, i32 *%X
///   %Y = load i32* %X
///   ret i32 %Y
/// @return An owning pointer to the newly created FunctionPass.
LLVM_ABI FunctionPass *createPromoteMemoryToRegisterPass();

//===----------------------------------------------------------------------===//
/// Create a pass that demotes registers to memory references.
///
/// This undoes the PromoteMemoryToRegister pass to make CFG hacking easier.
/// @return An owning pointer to the newly created FunctionPass.
LLVM_ABI FunctionPass *createRegToMemWrapperPass();

//===----------------------------------------------------------------------===//
/// Create a pass that inserts preheader blocks into the CFG for every loop.
///
/// This pass updates dominator information and loop information, and does not
/// add critical edges to the CFG.
/// @return An owning pointer to the newly created Pass.
LLVM_ABI Pass *createLoopSimplifyPass();

/// Pass ID for the LoopSimplify pass.
///
/// Other passes can require this pass with:
///
///   AU.addRequiredID(LoopSimplifyID);
LLVM_ABI extern char &LoopSimplifyID;

//===----------------------------------------------------------------------===//
/// Create a pass that unifies each loop's exits through a single successor.
///
/// For each loop, creates a new block N such that all exiting blocks branch to
/// N, and then N distributes control flow to all the original exit blocks.
/// @return An owning pointer to the newly created FunctionPass.
LLVM_ABI FunctionPass *createUnifyLoopExitsPass();

//===----------------------------------------------------------------------===//
/// Create a pass that converts each irreducible SCC into a natural loop.
/// @return An owning pointer to the newly created FunctionPass.
LLVM_ABI FunctionPass *createFixIrreduciblePass();

//===----------------------------------------------------------------------===//
/// Create a pass that canonicalizes freeze instructions in loops.
///
/// Canonicalizes freeze instructions in loops so they don't block SCEV.
/// @return An owning pointer to the newly created Pass.
LLVM_ABI Pass *createCanonicalizeFreezeInLoopsPass();

//===----------------------------------------------------------------------===//
/// Create a legacy pass that lowers @llvm.global_dtors via atexit wrappers.
///
/// Lowers @llvm.global_dtors by creating wrapper functions that are registered
/// in @llvm.global_ctors and which contain a call to `__cxa_atexit` to register
/// their destructor functions.
/// @return An owning pointer to the newly created ModulePass.
LLVM_ABI ModulePass *createLowerGlobalDtorsLegacyPass();

//===----------------------------------------------------------------------===//
/// Create a pass that strips convergence intrinsics and operand bundles.
///
/// Strips convergence intrinsics and convergencectrl operand bundles.
/// @return An owning pointer to the newly created FunctionPass.
LLVM_ABI FunctionPass *createStripConvergenceIntrinsicsPass();
} // namespace llvm

#endif
