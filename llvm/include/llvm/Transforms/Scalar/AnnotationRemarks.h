//===- AnnotationRemarks.cpp - Emit remarks for !annotation MD --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file defines AnnotationRemarksPass for the new pass manager.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_ANNOTATIONREMARKS_H
#define LLVM_TRANSFORMS_SCALAR_ANNOTATIONREMARKS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// A pass that emits optimization remarks for instructions with !annotation.
///
/// Summarizes annotated instructions in a function and emits more detailed
/// remarks (such as auto-init) grouped by debug location.
struct AnnotationRemarksPass
    : public RequiredPassInfoMixin<AnnotationRemarksPass> {
  /// Emit annotation remarks for the function.
  /// @param F Function whose annotated instructions are analyzed.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_ANNOTATIONREMARKS_H
