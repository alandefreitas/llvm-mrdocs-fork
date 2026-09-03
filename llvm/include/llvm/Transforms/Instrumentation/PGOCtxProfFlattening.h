//===-- PGOCtxProfFlattening.h - Contextual Instr. Flattening ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the PGOCtxProfFlattening class.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_PGOCTXPROFFLATTENING_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_PGOCTXPROFFLATTENING_H

#include "llvm/IR/PassManager.h"
namespace llvm {

/// Pass that flattens contextual profiles into MD_prof metadata.
class PGOCtxProfFlatteningPass
    : public OptionalPassInfoMixin<PGOCtxProfFlatteningPass> {
  const bool IsPreThinlink;

public:
  /// Construct a contextual profile flattening pass.
  /// @param IsPreThinlink True when running before ThinLTO linking.
  explicit PGOCtxProfFlatteningPass(bool IsPreThinlink)
      : IsPreThinlink(IsPreThinlink) {}
  /// Flatten contextual profiles in \p M into MD_prof metadata.
  /// @param M Module whose contextual profiles are flattened.
  /// @param MAM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};
} // namespace llvm
#endif
