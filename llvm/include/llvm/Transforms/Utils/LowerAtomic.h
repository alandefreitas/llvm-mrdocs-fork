//===- LowerAtomic.h - Lower atomic intrinsics ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
// This pass lowers atomic intrinsics to non-atomic form for use in a known
// non-preemptible environment.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOWERATOMIC_H
#define LLVM_TRANSFORMS_UTILS_LOWERATOMIC_H

#include "llvm/IR/Instructions.h"

namespace llvm {

class IRBuilderBase;

/// Convert the given Cmpxchg into primitive load and compare.
/// \param CXI The atomic compare-exchange instruction to lower.
/// \return True if the compare-exchange was lowered.
LLVM_ABI bool lowerAtomicCmpXchgInst(AtomicCmpXchgInst *CXI);

/// Emit IR to implement the given cmpxchg operation on values in registers,
/// returning the new value.
/// \param Builder IR builder used to emit the non-atomic cmpxchg sequence.
/// \param Ptr Pointer to the memory location being compared and exchanged.
/// \param Cmp Expected value compared against the loaded contents of \p Ptr.
/// \param Val New value stored to \p Ptr when the comparison succeeds.
/// \param Alignment Alignment of the load and store to \p Ptr.
/// \return A pair of the original loaded value and whether it equaled \p Cmp.
LLVM_ABI std::pair<Value *, Value *> buildCmpXchgValue(IRBuilderBase &Builder,
                                                       Value *Ptr, Value *Cmp,
                                                       Value *Val,
                                                       Align Alignment);

/// Convert the given RMWI into primitive load and stores,
/// assuming that doing so is legal. Return true if the lowering
/// succeeds.
/// \param RMWI The atomic read-modify-write instruction to lower.
/// \return True if the RMW was lowered.
LLVM_ABI bool lowerAtomicRMWInst(AtomicRMWInst *RMWI);

/// Emit IR to implement the given atomicrmw operation on values in registers,
/// returning the new value.
/// \param Op The atomic RMW binary operation to perform.
/// \param Builder IR builder used to emit the operation.
/// \param Loaded Value previously loaded from the atomic location.
/// \param Val Operand combined with \p Loaded according to \p Op.
/// \return The new value computed from \p Loaded and \p Val under \p Op.
LLVM_ABI Value *buildAtomicRMWValue(AtomicRMWInst::BinOp Op,
                                    IRBuilderBase &Builder, Value *Loaded,
                                    Value *Val);
}

#endif // LLVM_TRANSFORMS_UTILS_LOWERATOMIC_H
