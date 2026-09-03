//===-- PGOCtxProfLowering.h - Contextual PGO Instr. Lowering ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the PGOCtxProfLoweringPass class.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_PGOCTXPROFLOWERING_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_PGOCTXPROFLOWERING_H

#include "llvm/IR/PassManager.h"
namespace llvm {
class Type;

/// Pass that lowers contextual PGO instrumentation into runtime calls.
class PGOCtxProfLoweringPass
    : public OptionalPassInfoMixin<PGOCtxProfLoweringPass> {
public:
  /// Construct a contextual PGO instrumentation lowering pass.
  explicit PGOCtxProfLoweringPass() = default;
  /// Return true if contextual IR PGO instrumentation is enabled.
  /// @return True if contextual IR PGO instrumentation is enabled.
  LLVM_ABI static bool isCtxIRPGOInstrEnabled();

  /// Lower contextual PGO instrumentation in \p M.
  /// @param M Module containing contextual profile instrumentation to lower.
  /// @param MAM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

/// Utility pass that blocks inlining for functions that may be overridden by a
/// prevailing copy during linking.
///
/// This avoids confusingly collecting profiles for the same GUID corresponding
/// to different variants of the function. We could do like PGO and identify
/// functions by a (GUID, Hash) tuple, but since the ctxprof "use" waits for
/// thinlto to happen before performing any further optimizations, it's
/// unnecessary to collect profiles for non-prevailing copies.
class NoinlineNonPrevailing
    : public OptionalPassInfoMixin<NoinlineNonPrevailing> {
public:
  /// Construct a pass that marks non-prevailing functions as noinline.
  explicit NoinlineNonPrevailing() = default;

  /// Mark non-prevailing functions in \p M as noinline.
  /// @param M Module whose non-prevailing functions are marked noinline.
  /// @param MAM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm
#endif
