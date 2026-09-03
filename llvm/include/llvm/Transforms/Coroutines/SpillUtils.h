//===- SpillUtils.h - Utilities for han dling for spills ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Dominators.h"
#include "llvm/Transforms/Coroutines/CoroShape.h"
#include "llvm/Transforms/Coroutines/SuspendCrossingInfo.h"

#ifndef LLVM_TRANSFORMS_COROUTINES_SPILLINGINFO_H
#define LLVM_TRANSFORMS_COROUTINES_SPILLINGINFO_H

namespace llvm::coro {

/// Map from a spilled value to the instructions that use it across suspends.
using SpillInfo = SmallMapVector<Value *, SmallVector<Instruction *, 2>, 8>;

/// Alloca that may need to live on the coroutine frame, with alias metadata.
struct AllocaInfo {
  /// The alloca instruction being considered for the frame.
  AllocaInst *Alloca;
  /// Map from aliasing instructions to an optional constant GEP offset.
  DenseMap<Instruction *, std::optional<APInt>> Aliases;
  /// Whether the alloca may be written before \c coro.begin.
  bool MayWriteBeforeCoroBegin;
  /// Construct alloca spill metadata.
  ///
  /// \param Alloca The alloca instruction.
  /// \param Aliases Map of aliasing instructions to optional GEP offsets.
  /// \param MayWriteBeforeCoroBegin Whether a write may occur before
  ///        \c coro.begin.
  AllocaInfo(AllocaInst *Alloca,
             DenseMap<Instruction *, std::optional<APInt>> Aliases,
             bool MayWriteBeforeCoroBegin)
      : Alloca(Alloca), Aliases(std::move(Aliases)),
        MayWriteBeforeCoroBegin(MayWriteBeforeCoroBegin) {}
};

/// Collect function arguments whose uses cross a suspend point into \p Spills.
///
/// \param Spills Output map from spilled values to their crossing uses.
/// \param F The coroutine function whose arguments are examined.
/// \param Checker Suspend-crossing analysis for \p F.
LLVM_ABI void collectSpillsFromArgs(SpillInfo &Spills, Function &F,
                                    const SuspendCrossingInfo &Checker);
/// Collect instruction spills and frame allocas that cross suspend points.
///
/// Walks instructions in \p F, recording values that must be spilled into the
/// frame, allocas that should live on the frame, non-local \c coro.alloca.alloc
/// rewrites, and instructions made dead by those rewrites.
///
/// \param Spills Output map from spilled values to their crossing uses.
/// \param Allocas Output list of allocas that may need to live on the frame.
/// \param DeadInstructions Output list of instructions to erase later.
/// \param LocalAllocas Output list of \c coro.alloca.alloc whose lifetime does
///        not cross a suspend.
/// \param F The coroutine function being analyzed.
/// \param Checker Suspend-crossing analysis for \p F.
/// \param DT Dominator tree for \p F.
/// \param Shape Structural shape of the coroutine (intrinsics and ABI).
LLVM_ABI void collectSpillsAndAllocasFromInsts(
    SpillInfo &Spills, SmallVector<AllocaInfo, 8> &Allocas,
    SmallVector<Instruction *, 4> &DeadInstructions,
    SmallVector<CoroAllocaAllocInst *, 4> &LocalAllocas, Function &F,
    const SuspendCrossingInfo &Checker, const DominatorTree &DT,
    const coro::Shape &Shape);

/// Add debug-info uses of already-spilled values that cross a suspend.
///
/// Only salvages \c dbg.value uses for values already present in \p Spills so
/// that debug information does not change the coroutine frame layout.
///
/// \param Spills Spill map to update with debug-info use instructions.
/// \param F The coroutine function being analyzed.
/// \param Checker Suspend-crossing analysis for \p F.
LLVM_ABI void collectSpillsFromDbgInfo(SpillInfo &Spills, Function &F,
                                       const SuspendCrossingInfo &Checker);

/// Async and Retcon{Once} conventions assume that all spill uses can be sunk
/// after the coro.begin intrinsic.
///
/// \param DT Dominator tree for the coroutine function.
/// \param CoroBegin The \c coro.begin intrinsic after which uses are sunk.
/// \param Spills Spill map whose definition uses may precede \p CoroBegin.
/// \param Allocas Frame allocas whose uses may precede \p CoroBegin.
LLVM_ABI void
sinkSpillUsesAfterCoroBegin(const DominatorTree &DT, CoroBeginInst *CoroBegin,
                            coro::SpillInfo &Spills,
                            SmallVectorImpl<coro::AllocaInfo> &Allocas);

/// Return the insertion point for a spill store of definition \p Def.
///
/// \param Shape Structural shape of the coroutine (frame pointer and ABI).
/// \param Def The value being spilled (argument or instruction).
/// \param DT Dominator tree used to place spills relative to \c coro.begin.
/// \return Iterator at which the spill store should be inserted.
LLVM_ABI BasicBlock::iterator
getSpillInsertionPt(const coro::Shape &Shape, Value *Def,
                    const DominatorTree &DT);

} // namespace llvm::coro

#endif // LLVM_TRANSFORMS_COROUTINES_SPILLINGINFO_H
