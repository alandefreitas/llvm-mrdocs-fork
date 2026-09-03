//===- ConstantFolder.h - Constant folding helper ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ConstantFolder class, a helper for IRBuilder.
// It provides IRBuilder with a set of methods for creating constants
// with minimal folding.  For general constant creation and folding,
// use ConstantExpr and the routines in llvm/Analysis/ConstantFolding.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CONSTANTFOLDER_H
#define LLVM_IR_CONSTANTFOLDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/ConstantFold.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilderFolder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// ConstantFolder - Create constants with minimum, target independent, folding.
class LLVM_ABI ConstantFolder final : public IRBuilderFolder {
  LLVM_DECLARE_VIRTUAL_ANCHOR_FUNCTION();

public:
  /// Construct a folder that performs minimal target-independent constant folding.
  explicit ConstantFolder() = default;

  //===--------------------------------------------------------------------===//
  // Value-based folders.
  //
  // Return an existing value or a constant if the operation can be simplified.
  // Otherwise return nullptr.
  //===--------------------------------------------------------------------===//

  /// Fold a binary operator when both operands are constants.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldBinOp(Instruction::BinaryOps Opc, Value *LHS,
                   Value *RHS) const override {
    auto *LC = dyn_cast<Constant>(LHS);
    auto *RC = dyn_cast<Constant>(RHS);
    if (LC && RC) {
      if (ConstantExpr::isDesirableBinOp(Opc))
        return ConstantExpr::get(Opc, LC, RC);
      return ConstantFoldBinaryInstruction(Opc, LC, RC);
    }
    return nullptr;
  }

  /// Fold an exact binary operator when both operands are constants.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param IsExact Whether the operation is exact.
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldExactBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                        bool IsExact) const override {
    auto *LC = dyn_cast<Constant>(LHS);
    auto *RC = dyn_cast<Constant>(RHS);
    if (LC && RC) {
      if (ConstantExpr::isDesirableBinOp(Opc))
        return ConstantExpr::get(Opc, LC, RC,
                                 IsExact ? PossiblyExactOperator::IsExact : 0);
      return ConstantFoldBinaryInstruction(Opc, LC, RC);
    }
    return nullptr;
  }

  /// Fold a no-wrap binary operator when both operands are constants.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param HasNUW Whether the operation has the nuw flag.
  /// \param HasNSW Whether the operation has the nsw flag.
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldNoWrapBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                         bool HasNUW, bool HasNSW) const override {
    auto *LC = dyn_cast<Constant>(LHS);
    auto *RC = dyn_cast<Constant>(RHS);
    if (LC && RC) {
      if (ConstantExpr::isDesirableBinOp(Opc)) {
        unsigned Flags = 0;
        if (HasNUW)
          Flags |= OverflowingBinaryOperator::NoUnsignedWrap;
        if (HasNSW)
          Flags |= OverflowingBinaryOperator::NoSignedWrap;
        return ConstantExpr::get(Opc, LC, RC, Flags);
      }
      return ConstantFoldBinaryInstruction(Opc, LC, RC);
    }
    return nullptr;
  }

  /// Fold a binary operator with fast-math flags when both operands are constants.
  ///
  /// Fast-math flags are ignored; folding is delegated to \c FoldBinOp.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param FMF Fast-math flags for the operation (unused).
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldBinOpFMF(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                      FastMathFlags FMF) const override {
    return FoldBinOp(Opc, LHS, RHS);
  }

  /// Fold a unary operator with fast-math flags when the operand is a constant.
  ///
  /// Fast-math flags are ignored for constant folding.
  /// \param Opc The unary opcode to fold.
  /// \param V The operand value.
  /// \param FMF Fast-math flags for the operation (unused).
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldUnOpFMF(Instruction::UnaryOps Opc, Value *V,
                     FastMathFlags FMF) const override {
    if (Constant *C = dyn_cast<Constant>(V))
      return ConstantFoldUnaryInstruction(Opc, C);
    return nullptr;
  }

  /// Fold a compare when both operands are constants.
  /// \param P The compare predicate.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldCmp(CmpInst::Predicate P, Value *LHS, Value *RHS) const override {
    auto *LC = dyn_cast<Constant>(LHS);
    auto *RC = dyn_cast<Constant>(RHS);
    if (LC && RC)
      return ConstantFoldCompareInstruction(P, LC, RC);
    return nullptr;
  }

  /// Fold a GEP when the pointer and all indices are constants.
  /// \param Ty The GEP source element type.
  /// \param Ptr The base pointer value.
  /// \param IdxList The GEP index values.
  /// \param NW No-wrap flags for the GEP.
  /// \return The folded constant GEP, or nullptr if the operation cannot be folded.
  Value *FoldGEP(Type *Ty, Value *Ptr, ArrayRef<Value *> IdxList,
                 GEPNoWrapFlags NW) const override {
    if (!ConstantExpr::isSupportedGetElementPtr(Ty))
      return nullptr;

    if (auto *PC = dyn_cast<Constant>(Ptr)) {
      // Every index must be constant.
      if (any_of(IdxList, [](Value *V) { return !isa<Constant>(V); }))
        return nullptr;

      return ConstantExpr::getGetElementPtr(Ty, PC, IdxList, NW);
    }
    return nullptr;
  }

  /// Fold a select when the condition and both arms are constants.
  ///
  /// Fast-math flags are ignored for constant folding.
  /// \param C The select condition.
  /// \param True The value selected when the condition is true.
  /// \param False The value selected when the condition is false.
  /// \param FMF Fast-math flags for the select (unused).
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldSelect(Value *C, Value *True, Value *False,
                    FastMathFlags FMF) const override {
    auto *CC = dyn_cast<Constant>(C);
    auto *TC = dyn_cast<Constant>(True);
    auto *FC = dyn_cast<Constant>(False);
    if (CC && TC && FC)
      return ConstantFoldSelectInstruction(CC, TC, FC);
    return nullptr;
  }

  /// Fold an extractvalue when the aggregate operand is a constant.
  /// \param Agg The aggregate value to extract from.
  /// \param IdxList Indices identifying the extracted element.
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldExtractValue(Value *Agg,
                          ArrayRef<unsigned> IdxList) const override {
    if (auto *CAgg = dyn_cast<Constant>(Agg))
      return ConstantFoldExtractValueInstruction(CAgg, IdxList);
    return nullptr;
  };

  /// Fold an insertvalue when the aggregate and inserted value are constants.
  /// \param Agg The aggregate value to insert into.
  /// \param Val The value to insert.
  /// \param IdxList Indices identifying the insertion position.
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldInsertValue(Value *Agg, Value *Val,
                         ArrayRef<unsigned> IdxList) const override {
    auto *CAgg = dyn_cast<Constant>(Agg);
    auto *CVal = dyn_cast<Constant>(Val);
    if (CAgg && CVal)
      return ConstantFoldInsertValueInstruction(CAgg, CVal, IdxList);
    return nullptr;
  }

  /// Fold an extractelement when the vector and index are constants.
  /// \param Vec The vector value to extract from.
  /// \param Idx The element index.
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldExtractElement(Value *Vec, Value *Idx) const override {
    auto *CVec = dyn_cast<Constant>(Vec);
    auto *CIdx = dyn_cast<Constant>(Idx);
    if (CVec && CIdx)
      return ConstantExpr::getExtractElement(CVec, CIdx);
    return nullptr;
  }

  /// Fold an insertelement when the vector, element, and index are constants.
  /// \param Vec The vector value to insert into.
  /// \param NewElt The element value to insert.
  /// \param Idx The element index.
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldInsertElement(Value *Vec, Value *NewElt,
                           Value *Idx) const override {
    auto *CVec = dyn_cast<Constant>(Vec);
    auto *CNewElt = dyn_cast<Constant>(NewElt);
    auto *CIdx = dyn_cast<Constant>(Idx);
    if (CVec && CNewElt && CIdx)
      return ConstantExpr::getInsertElement(CVec, CNewElt, CIdx);
    return nullptr;
  }

  /// Fold a shufflevector when both vector operands are constants.
  /// \param V1 The first vector operand.
  /// \param V2 The second vector operand.
  /// \param Mask The shuffle mask.
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldShuffleVector(Value *V1, Value *V2,
                           ArrayRef<int> Mask) const override {
    auto *C1 = dyn_cast<Constant>(V1);
    auto *C2 = dyn_cast<Constant>(V2);
    if (C1 && C2)
      return ConstantExpr::getShuffleVector(C1, C2, Mask);
    return nullptr;
  }

  /// Fold a cast when the operand is a constant.
  /// \param Op The cast opcode.
  /// \param V The value to cast.
  /// \param DestTy The destination type.
  /// \return The folded constant, or nullptr if the operation cannot be folded.
  Value *FoldCast(Instruction::CastOps Op, Value *V,
                  Type *DestTy) const override {
    if (auto *C = dyn_cast<Constant>(V)) {
      if (ConstantExpr::isDesirableCastOp(Op))
        return ConstantExpr::getCast(Op, C, DestTy);
      return ConstantFoldCastInstruction(Op, C, DestTy);
    }
    return nullptr;
  }

  /// Attempt to fold an intrinsic call; always returns nullptr.
  ///
  /// Use TargetFolder or InstSimplifyFolder instead.
  /// \param ID The intrinsic identifier.
  /// \param Ops The intrinsic operands.
  /// \param Ty The result type of the intrinsic.
  /// \param FMF Fast-math flags for the call.
  /// \param CtxF Optional context function for the intrinsic.
  /// \return Always nullptr; this folder does not fold intrinsics.
  Value *FoldIntrinsic(Intrinsic::ID ID, ArrayRef<Value *> Ops, Type *Ty,
                       FastMathFlags FMF, Function *CtxF) const override {
    // Use TargetFolder or InstSimplifyFolder instead.
    return nullptr;
  }

  //===--------------------------------------------------------------------===//
  // Cast/Conversion Operators
  //===--------------------------------------------------------------------===//

  /// Create a pointer cast of constant \p C to type \p DestTy.
  /// \param C The constant pointer to cast.
  /// \param DestTy The destination type.
  /// \return The constant pointer cast to \p DestTy.
  Constant *CreatePointerCast(Constant *C, Type *DestTy) const override {
    return ConstantExpr::getPointerCast(C, DestTy);
  }

  /// Create a bitcast or addrspacecast of constant pointer \p C to \p DestTy.
  /// \param C The constant pointer to cast.
  /// \param DestTy The destination type.
  /// \return The constant after a bitcast or addrspacecast to \p DestTy.
  Constant *CreatePointerBitCastOrAddrSpaceCast(Constant *C,
                                                Type *DestTy) const override {
    return ConstantExpr::getPointerBitCastOrAddrSpaceCast(C, DestTy);
  }
};

} // end namespace llvm

#endif // LLVM_IR_CONSTANTFOLDER_H
