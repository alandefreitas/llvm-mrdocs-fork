//===-- ObjCARC.h - ObjCARC Scalar Transformations --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes for accessor functions that expose passes
// in the ObjCARC Scalar Transformations library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_OBJCARC_H
#define LLVM_TRANSFORMS_OBJCARC_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Pass;

//===----------------------------------------------------------------------===//
/// Create a late ObjC ARC cleanup pass for the legacy pass manager.
/// @return A new ObjCARCContract pass for the legacy pass manager.
LLVM_ABI Pass *createObjCARCContractPass();

/// Optimize ObjC Automatic Reference Counting retain/release operations.
struct ObjCARCOptPass : public OptionalPassInfoMixin<ObjCARCOptPass> {
  /// Run ObjC ARC retain/release optimizations on a function.
  /// @param F Function whose ARC operations should be optimized.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Contract low-level ObjC ARC operations into higher-level forms late in the
/// pipeline.
struct ObjCARCContractPass : public OptionalPassInfoMixin<ObjCARCContractPass> {
  /// Run late ObjC ARC contraction on a function.
  /// @param F Function whose ARC operations should be contracted.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Expand ObjC ARC operations early for later ARC optimizations.
struct ObjCARCExpandPass : public OptionalPassInfoMixin<ObjCARCExpandPass> {
  /// Run early ObjC ARC expansion on a function.
  /// @param M Function whose ARC operations should be expanded.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &M, FunctionAnalysisManager &AM);
};

/// Evaluate ObjC ARC provenance analysis results for debugging.
struct PAEvalPass : public OptionalPassInfoMixin<PAEvalPass> {
  /// Run provenance analysis evaluation over a function.
  /// @param F Function whose named pointer values should be evaluated.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // End llvm namespace

#endif
