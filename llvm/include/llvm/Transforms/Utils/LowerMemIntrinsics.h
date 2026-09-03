//===- llvm/Transforms/Utils/LowerMemIntrinsics.h ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Lower memset, memcpy, memmov intrinsics to loops (e.g. for targets without
// library support).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOWERMEMINTRINSICS_H
#define LLVM_TRANSFORMS_UTILS_LOWERMEMINTRINSICS_H

#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <optional>

namespace llvm {

class AnyMemCpyInst;
class ConstantInt;
class Instruction;
class MemCpyInst;
class MemMoveInst;
class MemSetInst;
class MemSetPatternInst;
class ScalarEvolution;
class TargetTransformInfo;
class Value;
struct Align;

/// Emit a loop implementing llvm.memcpy when the size is not a constant.
///
/// The loop is inserted at \p InsertBefore.
///
/// \param InsertBefore Instruction before which the loop is inserted.
/// \param SrcAddr Source address of the copy.
/// \param DstAddr Destination address of the copy.
/// \param CopyLen Runtime number of bytes to copy.
/// \param SrcAlign Alignment of the source.
/// \param DestAlign Alignment of the destination.
/// \param SrcIsVolatile Whether the source load is volatile.
/// \param DstIsVolatile Whether the destination store is volatile.
/// \param CanOverlap Whether source and destination may overlap.
/// \param TTI Target info used to choose loop operand types.
/// \param AtomicSize Optional atomic element size in bytes.
/// \param AverageTripCount Optional expected loop trip count for profiling.
LLVM_ABI void createMemCpyLoopUnknownSize(
    Instruction *InsertBefore, Value *SrcAddr, Value *DstAddr, Value *CopyLen,
    Align SrcAlign, Align DestAlign, bool SrcIsVolatile, bool DstIsVolatile,
    bool CanOverlap, const TargetTransformInfo &TTI,
    std::optional<unsigned> AtomicSize = std::nullopt,
    std::optional<uint64_t> AverageTripCount = std::nullopt);

/// Emit a loop implementing llvm.memcpy when the size is a constant.
///
/// The loop is inserted at \p InsertBefore.
///
/// \param InsertBefore Instruction before which the loop is inserted.
/// \param SrcAddr Source address of the copy.
/// \param DstAddr Destination address of the copy.
/// \param CopyLen Constant number of bytes to copy.
/// \param SrcAlign Alignment of the source.
/// \param DestAlign Alignment of the destination.
/// \param SrcIsVolatile Whether the source load is volatile.
/// \param DstIsVolatile Whether the destination store is volatile.
/// \param CanOverlap Whether source and destination may overlap.
/// \param TTI Target info used to choose loop operand types.
/// \param AtomicCpySize Optional atomic element size in bytes.
/// \param AverageTripCount Optional expected loop trip count for profiling.
LLVM_ABI void createMemCpyLoopKnownSize(
    Instruction *InsertBefore, Value *SrcAddr, Value *DstAddr,
    ConstantInt *CopyLen, Align SrcAlign, Align DestAlign, bool SrcIsVolatile,
    bool DstIsVolatile, bool CanOverlap, const TargetTransformInfo &TTI,
    std::optional<uint32_t> AtomicCpySize = std::nullopt,
    std::optional<uint64_t> AverageTripCount = std::nullopt);

/// Expand \p MemCpy as a loop without deleting the intrinsic.
///
/// \param MemCpy The memcpy intrinsic to expand.
/// \param TTI Target info used to choose loop operand types.
/// \param SE Optional scalar evolution used to prove non-overlap.
LLVM_ABI void expandMemCpyAsLoop(MemCpyInst *MemCpy,
                                 const TargetTransformInfo &TTI,
                                 ScalarEvolution *SE = nullptr);

/// Expand \p MemMove as a loop without deleting the intrinsic.
///
/// \param MemMove The memmove intrinsic to expand.
/// \param TTI Target info used to choose the lowering strategy.
/// \return True if the memmove was lowered.
LLVM_ABI bool expandMemMoveAsLoop(MemMoveInst *MemMove,
                                  const TargetTransformInfo &TTI);

/// Expand \p MemSet as a loop without deleting the intrinsic.
///
/// If \p TTI is provided, the memset is expanded according to the target's
/// preferences. Otherwise, it is expanded as a byte-wise loop.
///
/// \param MemSet The memset intrinsic to expand.
/// \param TTI Optional target info controlling the expansion strategy.
LLVM_ABI void expandMemSetAsLoop(MemSetInst *MemSet,
                                 const TargetTransformInfo *TTI = nullptr);

/// Expand \p MemSet as a loop using the target's preferences.
///
/// \p MemSet is not deleted.
///
/// \param MemSet The memset intrinsic to expand.
/// \param TTI Target info controlling the expansion strategy.
LLVM_ABI void expandMemSetAsLoop(MemSetInst *MemSet,
                                 const TargetTransformInfo &TTI);

/// Expand \p MemSetPattern as a loop without deleting the intrinsic.
///
/// If \p TTI is provided, the memset.pattern is expanded according to the
/// target's preferences. Otherwise, it is expanded as an element-wise loop.
///
/// \param MemSet The memset.pattern intrinsic to expand.
/// \param TTI Optional target info controlling the expansion strategy.
LLVM_ABI void
expandMemSetPatternAsLoop(MemSetPatternInst *MemSet,
                          const TargetTransformInfo *TTI = nullptr);

/// Expand \p MemSetPattern as a loop without deleting the intrinsic.
///
/// \param MemSet The memset.pattern intrinsic to expand.
/// \param TTI Target info controlling the expansion strategy.
LLVM_ABI void expandMemSetPatternAsLoop(MemSetPatternInst *MemSet,
                                        const TargetTransformInfo &TTI);

/// Expand \p AtomicMemCpy as a loop without deleting the intrinsic.
///
/// \param AtomicMemCpy The atomic memcpy intrinsic to expand.
/// \param TTI Target info used to choose loop operand types.
/// \param SE Optional scalar evolution used for analysis.
LLVM_ABI void expandAtomicMemCpyAsLoop(AnyMemCpyInst *AtomicMemCpy,
                                       const TargetTransformInfo &TTI,
                                       ScalarEvolution *SE);

} // namespace llvm

#endif
