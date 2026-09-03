//====- TargetFolder.h - Constant folding helper ---------------*- C++ -*-====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TargetFolder class, a helper for IRBuilder.
// It provides IRBuilder with a set of methods for creating constants with
// target dependent folding, in addition to the same target-independent
// folding that the ConstantFolder class provides.  For general constant
// creation and folding, use ConstantExpr and the routines in
// llvm/Analysis/ConstantFolding.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TARGETFOLDER_H
#define LLVM_ANALYSIS_TARGETFOLDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/ConstantFold.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilderFolder.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Constant;
class DataLayout;
class Type;

/// TargetFolder - Create constants with target dependent folding.
class LLVM_ABI TargetFolder final : public IRBuilderFolder {
  const DataLayout &DL;

  /// Fold - Fold the constant using target specific information.
  Constant *Fold(Constant *C) const {
    return ConstantFoldConstant(C, DL);
  }

  LLVM_DECLARE_VIRTUAL_ANCHOR_FUNCTION();

public:
  /// Construct a folder that creates constants with target-dependent folding.
  /// \param DL Data layout used for target-specific constant folding.
  explicit TargetFolder(const DataLayout &DL) : DL(DL) {}

  //===--------------------------------------------------------------------===//
  // Value-based folders.
  //
  // Return an existing value or a constant if the operation can be simplified.
  // Otherwise return nullptr.
  //===--------------------------------------------------------------------===//

  /// Fold a binary operator with target-dependent constant folding.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldBinOp(Instruction::BinaryOps Opc, Value *LHS,
                   Value *RHS) const override {
    auto *LC = dyn_cast<Constant>(LHS);
    auto *RC = dyn_cast<Constant>(RHS);
    if (LC && RC) {
      if (ConstantExpr::isDesirableBinOp(Opc))
        return Fold(ConstantExpr::get(Opc, LC, RC));
      return ConstantFoldBinaryOpOperands(Opc, LC, RC, DL);
    }
    return nullptr;
  }

  /// Fold an exact binary operator with target-dependent constant folding.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param IsExact Whether the operation is exact.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldExactBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                        bool IsExact) const override {
    auto *LC = dyn_cast<Constant>(LHS);
    auto *RC = dyn_cast<Constant>(RHS);
    if (LC && RC) {
      if (ConstantExpr::isDesirableBinOp(Opc))
        return Fold(ConstantExpr::get(
            Opc, LC, RC, IsExact ? PossiblyExactOperator::IsExact : 0));
      return ConstantFoldBinaryOpOperands(Opc, LC, RC, DL);
    }
    return nullptr;
  }

  /// Fold a no-wrap binary operator with target-dependent constant folding.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param HasNUW Whether the operation has the nuw flag.
  /// \param HasNSW Whether the operation has the nsw flag.
  /// @return The folded value, or nullptr if the operation cannot be folded.
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
        return Fold(ConstantExpr::get(Opc, LC, RC, Flags));
      }
      return ConstantFoldBinaryOpOperands(Opc, LC, RC, DL);
    }
    return nullptr;
  }

  /// Fold a binary operator with fast-math flags via target-dependent folding.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param FMF Fast-math flags for the operation.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldBinOpFMF(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                      FastMathFlags FMF) const override {
    return FoldBinOp(Opc, LHS, RHS);
  }

  /// Fold a compare with target-dependent constant folding.
  /// \param P The compare predicate.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldCmp(CmpInst::Predicate P, Value *LHS, Value *RHS) const override {
    auto *LC = dyn_cast<Constant>(LHS);
    auto *RC = dyn_cast<Constant>(RHS);
    if (LC && RC)
      return ConstantFoldCompareInstOperands(P, LC, RC, DL);
    return nullptr;
  }

  /// Fold a unary operator with fast-math flags via target-dependent folding.
  /// \param Opc The unary opcode to fold.
  /// \param V The operand value.
  /// \param FMF Fast-math flags for the operation.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldUnOpFMF(Instruction::UnaryOps Opc, Value *V,
                      FastMathFlags FMF) const override {
    if (Constant *C = dyn_cast<Constant>(V))
      return ConstantFoldUnaryOpOperand(Opc, C, DL);
    return nullptr;
  }

  /// Fold a GEP with target-dependent constant folding.
  /// \param Ty The GEP source element type.
  /// \param Ptr The base pointer value.
  /// \param IdxList The GEP index values.
  /// \param NW No-wrap flags for the GEP.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldGEP(Type *Ty, Value *Ptr, ArrayRef<Value *> IdxList,
                 GEPNoWrapFlags NW) const override {
    if (!ConstantExpr::isSupportedGetElementPtr(Ty))
      return nullptr;

    if (auto *PC = dyn_cast<Constant>(Ptr)) {
      // Every index must be constant.
      if (any_of(IdxList, [](Value *V) { return !isa<Constant>(V); }))
        return nullptr;
      return Fold(ConstantExpr::getGetElementPtr(Ty, PC, IdxList, NW));
    }
    return nullptr;
  }

  /// Fold a select with target-dependent constant folding.
  /// \param C The select condition.
  /// \param True The value selected when the condition is true.
  /// \param False The value selected when the condition is false.
  /// \param FMF Fast-math flags for the select.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldSelect(Value *C, Value *True, Value *False,
                    FastMathFlags FMF) const override {
    auto *CC = dyn_cast<Constant>(C);
    auto *TC = dyn_cast<Constant>(True);
    auto *FC = dyn_cast<Constant>(False);
    if (CC && TC && FC)
      return ConstantFoldSelectInstruction(CC, TC, FC);

    return nullptr;
  }

  /// Fold an extractvalue with target-dependent constant folding.
  /// \param Agg The aggregate value to extract from.
  /// \param IdxList Indices identifying the extracted element.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldExtractValue(Value *Agg,
                          ArrayRef<unsigned> IdxList) const override {
    if (auto *CAgg = dyn_cast<Constant>(Agg))
      return ConstantFoldExtractValueInstruction(CAgg, IdxList);
    return nullptr;
  };

  /// Fold an insertvalue with target-dependent constant folding.
  /// \param Agg The aggregate value to insert into.
  /// \param Val The value to insert.
  /// \param IdxList Indices identifying the insertion position.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldInsertValue(Value *Agg, Value *Val,
                         ArrayRef<unsigned> IdxList) const override {
    auto *CAgg = dyn_cast<Constant>(Agg);
    auto *CVal = dyn_cast<Constant>(Val);
    if (CAgg && CVal)
      return ConstantFoldInsertValueInstruction(CAgg, CVal, IdxList);
    return nullptr;
  }

  /// Fold an extractelement with target-dependent constant folding.
  /// \param Vec The vector value to extract from.
  /// \param Idx The element index.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldExtractElement(Value *Vec, Value *Idx) const override {
    auto *CVec = dyn_cast<Constant>(Vec);
    auto *CIdx = dyn_cast<Constant>(Idx);
    if (CVec && CIdx)
      return Fold(ConstantExpr::getExtractElement(CVec, CIdx));
    return nullptr;
  }

  /// Fold an insertelement with target-dependent constant folding.
  /// \param Vec The vector value to insert into.
  /// \param NewElt The element value to insert.
  /// \param Idx The element index.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldInsertElement(Value *Vec, Value *NewElt,
                           Value *Idx) const override {
    auto *CVec = dyn_cast<Constant>(Vec);
    auto *CNewElt = dyn_cast<Constant>(NewElt);
    auto *CIdx = dyn_cast<Constant>(Idx);
    if (CVec && CNewElt && CIdx)
      return Fold(ConstantExpr::getInsertElement(CVec, CNewElt, CIdx));
    return nullptr;
  }

  /// Fold a shufflevector with target-dependent constant folding.
  /// \param V1 The first vector operand.
  /// \param V2 The second vector operand.
  /// \param Mask The shuffle mask.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldShuffleVector(Value *V1, Value *V2,
                           ArrayRef<int> Mask) const override {
    auto *C1 = dyn_cast<Constant>(V1);
    auto *C2 = dyn_cast<Constant>(V2);
    if (C1 && C2)
      return Fold(ConstantExpr::getShuffleVector(C1, C2, Mask));
    return nullptr;
  }

  /// Fold a cast with target-dependent constant folding.
  /// \param Op The cast opcode.
  /// \param V The value to cast.
  /// \param DestTy The destination type.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldCast(Instruction::CastOps Op, Value *V,
                  Type *DestTy) const override {
    if (auto *C = dyn_cast<Constant>(V))
      return ConstantFoldCastOperand(Op, C, DestTy, DL);
    return nullptr;
  }

  /// Fold an intrinsic call with target-dependent constant folding.
  /// \param ID The intrinsic identifier.
  /// \param Ops The intrinsic operands.
  /// \param Ty The result type of the intrinsic.
  /// \param FMF Fast-math flags for the call.
  /// \param CxtF Optional context function for the intrinsic.
  /// @return The folded value, or nullptr if the operation cannot be folded.
  Value *FoldIntrinsic(Intrinsic::ID ID, ArrayRef<Value *> Ops, Type *Ty,
                       FastMathFlags FMF = {},
                       Function *CxtF = nullptr) const override {
    if (all_of(Ops, IsaPred<Constant>))
      return ConstantFoldIntrinsic(
          ID, ArrayRef((Constant *const *)Ops.data(), Ops.size()), Ty, DL,
          CxtF);
    return nullptr;
  }

  //===--------------------------------------------------------------------===//
  // Cast/Conversion Operators
  //===--------------------------------------------------------------------===//

  /// Create a pointer cast of constant \p C to type \p DestTy.
  /// \param C The constant pointer to cast.
  /// \param DestTy The destination type.
  /// @return The pointer cast of \p C to \p DestTy, or \p C if already that type.
  Constant *CreatePointerCast(Constant *C, Type *DestTy) const override {
    if (C->getType() == DestTy)
      return C; // avoid calling Fold
    return Fold(ConstantExpr::getPointerCast(C, DestTy));
  }

  /// Create a bitcast or addrspacecast of constant pointer \p C to \p DestTy.
  /// \param C The constant pointer to cast.
  /// \param DestTy The destination type.
  /// @return The cast of \p C to \p DestTy, or \p C if already that type.
  Constant *CreatePointerBitCastOrAddrSpaceCast(Constant *C,
                                                Type *DestTy) const override {
    if (C->getType() == DestTy)
      return C; // avoid calling Fold
    return Fold(ConstantExpr::getPointerBitCastOrAddrSpaceCast(C, DestTy));
  }
};
}

#endif
