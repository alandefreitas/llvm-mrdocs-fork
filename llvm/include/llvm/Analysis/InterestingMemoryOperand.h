//===- InterestingMemoryOperand.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines InterestingMemoryOperand class that is used when getting
// the information of a memory reference instruction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_INTERESTINGMEMORYOPERAND_H
#define LLVM_ANALYSIS_INTERESTINGMEMORYOPERAND_H

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/TypeSize.h"

namespace llvm {
/// Describes a memory reference of interest for analysis.
///
/// Collects the pointer use, access kind, type, alignment, and optional
/// mask, EVL, stride, or byte-offset operands from a memory instruction.
class InterestingMemoryOperand {
public:
  /// The use of the pointer operand in the memory instruction.
  Use *PtrUse;
  /// Whether this memory access is a write.
  bool IsWrite;
  /// The type of the memory operand being accessed.
  Type *OpType;
  /// Store size of \p OpType in bits.
  TypeSize TypeStoreSize = TypeSize::getFixed(0);
  /// Alignment of the memory access, if known.
  MaybeAlign Alignment;
  /// The mask value, if looking at a masked load/store.
  Value *MaybeMask;
  /// The EVL value, if looking at a vp intrinsic.
  Value *MaybeEVL;
  /// The stride value, if looking at a strided load/store.
  Value *MaybeStride;
  /// The byte-offset value, if looking at an indexed load/store.
  ///
  /// The offset means a byte offset rather than an array index.
  Value *MaybeByteOffset;

  /// Construct an interesting memory operand from a memory instruction.
  /// @param I Instruction that performs the memory access.
  /// @param OperandNo Operand index of the pointer within \p I.
  /// @param IsWrite Whether the access writes memory.
  /// @param OpType Type of the value being loaded or stored.
  /// @param Alignment Alignment of the access, if known.
  /// @param MaybeMask Optional mask for masked loads/stores.
  /// @param MaybeEVL Optional EVL for vp intrinsics.
  /// @param MaybeStride Optional stride for strided loads/stores.
  /// @param MaybeByteOffset Optional byte offset for indexed loads/stores.
  InterestingMemoryOperand(Instruction *I, unsigned OperandNo, bool IsWrite,
                           class Type *OpType, MaybeAlign Alignment,
                           Value *MaybeMask = nullptr,
                           Value *MaybeEVL = nullptr,
                           Value *MaybeStride = nullptr,
                           Value *MaybeByteOffset = nullptr)
      : IsWrite(IsWrite), OpType(OpType), Alignment(Alignment),
        MaybeMask(MaybeMask), MaybeEVL(MaybeEVL), MaybeStride(MaybeStride),
        MaybeByteOffset(MaybeByteOffset) {
    const DataLayout &DL = I->getDataLayout();
    TypeStoreSize = DL.getTypeStoreSizeInBits(OpType);
    PtrUse = &I->getOperandUse(OperandNo);
  }

  /// Return the instruction that owns this pointer use.
  /// @return The instruction that owns this pointer use.
  Instruction *getInsn() { return cast<Instruction>(PtrUse->getUser()); }

  /// Return the pointer value of this memory operand.
  /// @return The pointer value of this memory operand.
  Value *getPtr() { return PtrUse->get(); }
};

} // namespace llvm

#endif // LLVM_ANALYSIS_INTERESTINGMEMORYOPERAND_H
