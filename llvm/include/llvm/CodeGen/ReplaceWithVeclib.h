//===- ReplaceWithVeclib.h - Replace vector intrinsics with veclib calls --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Replaces calls to LLVM vector intrinsics (i.e., calls to LLVM intrinsics
// with vector operands) with matching calls to functions from a vector
// library (e.g., libmvec, SVML) according to TargetLibraryInfo.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_REPLACEWITHVECLIB_H
#define LLVM_CODEGEN_REPLACEWITHVECLIB_H

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Function;

/// New PM pass that replaces vector intrinsics with vector-library calls.
///
/// Replaces calls to LLVM vector intrinsics with matching calls to functions
/// from a vector library (e.g., libmvec, SVML) according to TargetLibraryInfo.
struct ReplaceWithVeclib : public RequiredPassInfoMixin<ReplaceWithVeclib> {
  /// Replace vector intrinsics in \p F with vector-library calls.
  ///
  /// \param F Function whose vector intrinsic calls are replaced.
  /// \param AM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy FunctionPass that replaces vector intrinsics with vector-library
/// calls.
struct LLVM_ABI ReplaceWithVeclibLegacy : public FunctionPass {
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the legacy ReplaceWithVeclib pass.
  ReplaceWithVeclibLegacy() : FunctionPass(ID) {}
  /// Declare required and preserved analyses for this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Replace vector intrinsics in \p F with vector-library calls.
  ///
  /// \param F Function whose vector intrinsic calls are replaced.
  /// \return True if the function was modified.
  bool runOnFunction(Function &F) override;
};

} // End namespace llvm
#endif // LLVM_CODEGEN_REPLACEWITHVECLIB_H
