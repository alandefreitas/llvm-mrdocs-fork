//===- NoFolder.h - Constant folding helper ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the NoFolder class, a helper for IRBuilder.  It provides
// IRBuilder with a set of methods for creating unfolded constants.  This is
// useful for learners trying to understand how LLVM IR works, and who don't
// want details to be hidden by the constant folder.  For general constant
// creation and folding, use ConstantExpr and the routines in
// llvm/Analysis/ConstantFolding.h.
//
// Note: since it is not actually possible to create unfolded constants, this
// class returns instructions rather than constants.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_NOFOLDER_H
#define LLVM_IR_NOFOLDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/IRBuilderFolder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// NoFolder - Create "constants" (actually, instructions) with no folding.
class LLVM_ABI NoFolder final : public IRBuilderFolder {
  LLVM_DECLARE_VIRTUAL_ANCHOR_FUNCTION();

public:
  /// Construct a folder that never folds constants.
  explicit NoFolder() = default;

  //===--------------------------------------------------------------------===//
  // Value-based folders.
  //
  // Return an existing value or a constant if the operation can be simplified.
  // Otherwise return nullptr.
  //===--------------------------------------------------------------------===//

  /// Decline to fold a binary operator; always returns nullptr.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \return Always nullptr.
  Value *FoldBinOp(Instruction::BinaryOps Opc, Value *LHS,
                   Value *RHS) const override {
    return nullptr;
  }

  /// Decline to fold an exact binary operator; always returns nullptr.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param IsExact Whether the operation is exact.
  /// \return Always nullptr.
  Value *FoldExactBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                        bool IsExact) const override {
    return nullptr;
  }

  /// Decline to fold a no-wrap binary operator; always returns nullptr.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param HasNUW Whether the operation has the nuw flag.
  /// \param HasNSW Whether the operation has the nsw flag.
  /// \return Always nullptr.
  Value *FoldNoWrapBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                         bool HasNUW, bool HasNSW) const override {
    return nullptr;
  }

  /// Decline to fold a binary operator with fast-math flags; always returns
  /// nullptr.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param FMF Fast-math flags for the operation.
  /// \return Always nullptr.
  Value *FoldBinOpFMF(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                      FastMathFlags FMF) const override {
    return nullptr;
  }

  /// Decline to fold a unary operator with fast-math flags; always returns
  /// nullptr.
  /// \param Opc The unary opcode to fold.
  /// \param V The operand value.
  /// \param FMF Fast-math flags for the operation.
  /// \return Always nullptr.
  Value *FoldUnOpFMF(Instruction::UnaryOps Opc, Value *V,
                     FastMathFlags FMF) const override {
    return nullptr;
  }

  /// Decline to fold a compare; always returns nullptr.
  /// \param P The compare predicate.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \return Always nullptr.
  Value *FoldCmp(CmpInst::Predicate P, Value *LHS, Value *RHS) const override {
    return nullptr;
  }

  /// Decline to fold a GEP; always returns nullptr.
  /// \param Ty The GEP source element type.
  /// \param Ptr The base pointer value.
  /// \param IdxList The GEP index values.
  /// \param NW No-wrap flags for the GEP.
  /// \return Always nullptr.
  Value *FoldGEP(Type *Ty, Value *Ptr, ArrayRef<Value *> IdxList,
                 GEPNoWrapFlags NW) const override {
    return nullptr;
  }

  /// Decline to fold a select; always returns nullptr.
  /// \param C The select condition.
  /// \param True The value selected when the condition is true.
  /// \param False The value selected when the condition is false.
  /// \param FMF Fast-math flags for the select.
  /// \return Always nullptr.
  Value *FoldSelect(Value *C, Value *True, Value *False,
                    FastMathFlags FMF) const override {
    return nullptr;
  }

  /// Decline to fold an extractvalue; always returns nullptr.
  /// \param Agg The aggregate value to extract from.
  /// \param IdxList Indices identifying the extracted element.
  /// \return Always nullptr.
  Value *FoldExtractValue(Value *Agg,
                          ArrayRef<unsigned> IdxList) const override {
    return nullptr;
  }

  /// Decline to fold an insertvalue; always returns nullptr.
  /// \param Agg The aggregate value to insert into.
  /// \param Val The value to insert.
  /// \param IdxList Indices identifying the insertion position.
  /// \return Always nullptr.
  Value *FoldInsertValue(Value *Agg, Value *Val,
                         ArrayRef<unsigned> IdxList) const override {
    return nullptr;
  }

  /// Decline to fold an extractelement; always returns nullptr.
  /// \param Vec The vector value to extract from.
  /// \param Idx The element index.
  /// \return Always nullptr.
  Value *FoldExtractElement(Value *Vec, Value *Idx) const override {
    return nullptr;
  }

  /// Decline to fold an insertelement; always returns nullptr.
  /// \param Vec The vector value to insert into.
  /// \param NewElt The element value to insert.
  /// \param Idx The element index.
  /// \return Always nullptr.
  Value *FoldInsertElement(Value *Vec, Value *NewElt,
                           Value *Idx) const override {
    return nullptr;
  }

  /// Decline to fold a shufflevector; always returns nullptr.
  /// \param V1 The first vector operand.
  /// \param V2 The second vector operand.
  /// \param Mask The shuffle mask.
  /// \return Always nullptr.
  Value *FoldShuffleVector(Value *V1, Value *V2,
                           ArrayRef<int> Mask) const override {
    return nullptr;
  }

  /// Decline to fold a cast; always returns nullptr.
  /// \param Op The cast opcode.
  /// \param V The value to cast.
  /// \param DestTy The destination type.
  /// \return Always nullptr.
  Value *FoldCast(Instruction::CastOps Op, Value *V,
                  Type *DestTy) const override {
    return nullptr;
  }

  /// Decline to fold an intrinsic call; always returns nullptr.
  /// \param ID The intrinsic identifier.
  /// \param Ops The intrinsic operands.
  /// \param Ty The result type of the intrinsic.
  /// \param FMF Fast-math flags for the call.
  /// \param CtxF Optional context function for the intrinsic.
  /// \return Always nullptr.
  Value *FoldIntrinsic(Intrinsic::ID ID, ArrayRef<Value *> Ops, Type *Ty,
                       FastMathFlags FMF, Function *CtxF) const override {
    return nullptr;
  }

  //===--------------------------------------------------------------------===//
  // Cast/Conversion Operators
  //===--------------------------------------------------------------------===//

  /// Create a pointer cast of constant \p C to type \p DestTy as an instruction.
  /// \param C The constant pointer to cast.
  /// \param DestTy The destination type.
  /// \return The cast instruction of \p C to \p DestTy.
  Instruction *CreatePointerCast(Constant *C, Type *DestTy) const override {
    return CastInst::CreatePointerCast(C, DestTy);
  }

  /// Create a bitcast or addrspacecast of constant pointer \p C to \p DestTy as
  /// an instruction.
  /// \param C The constant pointer to cast.
  /// \param DestTy The destination type.
  /// \return The bitcast or addrspacecast instruction of \p C to \p DestTy.
  Instruction *CreatePointerBitCastOrAddrSpaceCast(
      Constant *C, Type *DestTy) const override {
    return CastInst::CreatePointerBitCastOrAddrSpaceCast(C, DestTy);
  }
};

} // end namespace llvm

#endif // LLVM_IR_NOFOLDER_H
