//===- CoroSplit.h - Converts a coroutine into a state machine -*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file declares the pass that builds the coroutine frame and outlines
// the resume and destroy parts of the coroutine into separate functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_COROSPLIT_H
#define LLVM_TRANSFORMS_COROUTINES_COROSPLIT_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Coroutines/ABI.h"

namespace llvm {

namespace coro {
class BaseABI;
struct Shape;
} // namespace coro

/// Pass that builds the coroutine frame and outlines resume and destroy
/// functions.
struct CoroSplitPass : RequiredPassInfoMixin<CoroSplitPass> {
  /// Function type that creates a coroutine ABI lowering for a function.
  using BaseABITy =
      std::function<std::unique_ptr<coro::BaseABI>(Function &, coro::Shape &)>;

  /// Construct a pass that uses the default ABI generators.
  /// @param OptimizeFrame Whether to optimize the coroutine frame layout.
  LLVM_ABI CoroSplitPass(bool OptimizeFrame = false);

  /// Construct a pass with additional custom ABI generators.
  /// @param GenCustomABIs Custom ABI generators selected by
  /// coro.begin.custom.abi.
  /// @param OptimizeFrame Whether to optimize the coroutine frame layout.
  LLVM_ABI CoroSplitPass(SmallVector<BaseABITy> GenCustomABIs,
                         bool OptimizeFrame = false);

  /// Construct a pass with a custom rematerializability callback.
  /// @param MaterializableCallback Predicate that decides which instructions
  /// may be rematerialized across suspend points.
  /// @param OptimizeFrame Whether to optimize the coroutine frame layout.
  LLVM_ABI
  CoroSplitPass(std::function<bool(Instruction &)> MaterializableCallback,
                bool OptimizeFrame = false);

  /// Construct a pass with a rematerializability callback and custom ABIs.
  /// @param MaterializableCallback Predicate that decides which instructions
  /// may be rematerialized across suspend points.
  /// @param GenCustomABIs Custom ABI generators selected by
  /// coro.begin.custom.abi.
  /// @param OptimizeFrame Whether to optimize the coroutine frame layout.
  LLVM_ABI
  CoroSplitPass(std::function<bool(Instruction &)> MaterializableCallback,
                SmallVector<BaseABITy> GenCustomABIs,
                bool OptimizeFrame = false);

  /// Split coroutine functions in SCC \p C into ramp, resume, and destroy
  /// functions.
  /// @param C SCC whose functions are processed.
  /// @param AM CGSCC analysis manager.
  /// @param CG Lazy call graph containing \p C.
  /// @param UR Structure for communicating call graph updates.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(LazyCallGraph::SCC &C,
                                 CGSCCAnalysisManager &AM, LazyCallGraph &CG,
                                 CGSCCUpdateResult &UR);

  /// Generator that creates and initializes an ABI transformer.
  BaseABITy CreateAndInitABI;

  /// Whether coroutine frame optimization is enabled (not O0).
  bool OptimizeFrame;
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_COROUTINES_COROSPLIT_H
