//===- Transforms/Instrumentation/CGProfile.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for LLVM's Call Graph Profile pass.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_CGPROFILE_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_CGPROFILE_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;
/// Pass that builds call-graph profile counts and attaches them as module
/// metadata.
class CGProfilePass : public OptionalPassInfoMixin<CGProfilePass> {
public:
  /// Construct a call-graph profile pass.
  /// @param InLTO Whether this pass runs in an LTO post-link pipeline.
  CGProfilePass(bool InLTO) : InLTO(InLTO) {}
  /// Collect call-graph profile counts for \p M and emit them as module flags.
  /// @param M Module to analyze.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  bool InLTO = false;
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_CGPROFILE_H
