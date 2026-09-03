//===- InstSimplifyFolder.h - InstSimplify folding helper --------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the InstSimplifyFolder class, a helper for IRBuilder.
// It provides IRBuilder with a set of methods for folding operations to
// existing values using InstructionSimplify. At the moment, only a subset of
// the implementation uses InstructionSimplify. The rest of the implementation
// only folds constants.
//
// The folder also applies target-specific constant folding.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_INSTSIMPLIFYFOLDER_H
#define LLVM_ANALYSIS_INSTSIMPLIFYFOLDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/IR/CmpPredicate.h"
#include "llvm/IR/IRBuilderFolder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Constant;

/// InstSimplifyFolder - Fold IRBuilder operations via InstructionSimplify.
///
/// Also applies target-specific constant folding when not using
/// InstructionSimplify.
class LLVM_ABI InstSimplifyFolder final : public IRBuilderFolder {
  TargetFolder ConstFolder;
  SimplifyQuery SQ;

  LLVM_DECLARE_VIRTUAL_ANCHOR_FUNCTION();

public:
  /// Construct a folder that simplifies with the given data layout.
  /// \param DL Data layout used for target folding and simplify queries.
  explicit InstSimplifyFolder(const DataLayout &DL) : ConstFolder(DL), SQ(DL) {}

  //===--------------------------------------------------------------------===//
  // Value-based folders.
  //
  // Return an existing value or a constant if the operation can be simplified.
  // Otherwise return nullptr.
  //===--------------------------------------------------------------------===//

  /// Fold a binary operator using InstructionSimplify.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldBinOp(Instruction::BinaryOps Opc, Value *LHS,
                   Value *RHS) const override {
    return simplifyBinOp(Opc, LHS, RHS, SQ);
  }

  /// Fold an exact binary operator using InstructionSimplify.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param IsExact Whether the operation is exact.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldExactBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                        bool IsExact) const override {
    return simplifyBinOp(Opc, LHS, RHS, SQ);
  }

  /// Fold a no-wrap binary operator using InstructionSimplify.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param HasNUW Whether the operation has the nuw flag.
  /// \param HasNSW Whether the operation has the nsw flag.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldNoWrapBinOp(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                         bool HasNUW, bool HasNSW) const override {
    return simplifyBinOp(Opc, LHS, RHS, SQ);
  }

  /// Fold a binary operator with fast-math flags using InstructionSimplify.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param FMF Fast-math flags for the operation.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldBinOpFMF(Instruction::BinaryOps Opc, Value *LHS, Value *RHS,
                      FastMathFlags FMF) const override {
    return simplifyBinOp(Opc, LHS, RHS, FMF, SQ);
  }

  /// Fold a unary operator with fast-math flags using InstructionSimplify.
  /// \param Opc The unary opcode to fold.
  /// \param V The operand value.
  /// \param FMF Fast-math flags for the operation.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldUnOpFMF(Instruction::UnaryOps Opc, Value *V,
                      FastMathFlags FMF) const override {
    return simplifyUnOp(Opc, V, FMF, SQ);
  }

  /// Fold a compare using InstructionSimplify.
  /// \param P The compare predicate.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldCmp(CmpInst::Predicate P, Value *LHS, Value *RHS) const override {
    return simplifyCmpInst(P, LHS, RHS, SQ);
  }

  /// Fold a GEP using InstructionSimplify.
  /// \param Ty The GEP source element type.
  /// \param Ptr The base pointer value.
  /// \param IdxList The GEP index values.
  /// \param NW No-wrap flags for the GEP.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldGEP(Type *Ty, Value *Ptr, ArrayRef<Value *> IdxList,
                 GEPNoWrapFlags NW) const override {
    return simplifyGEPInst(Ty, Ptr, IdxList, NW, SQ);
  }

  /// Fold a select using InstructionSimplify.
  /// \param C The select condition.
  /// \param True The value selected when the condition is true.
  /// \param False The value selected when the condition is false.
  /// \param FMF Fast-math flags for the select.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldSelect(Value *C, Value *True, Value *False,
                    FastMathFlags FMF = FastMathFlags()) const override {
    return simplifySelectInst(C, True, False, FMF, SQ);
  }

  /// Fold an extractvalue using InstructionSimplify.
  /// \param Agg The aggregate value to extract from.
  /// \param IdxList Indices identifying the extracted element.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldExtractValue(Value *Agg,
                          ArrayRef<unsigned> IdxList) const override {
    return simplifyExtractValueInst(Agg, IdxList, SQ);
  };

  /// Fold an insertvalue using InstructionSimplify.
  /// \param Agg The aggregate value to insert into.
  /// \param Val The value to insert.
  /// \param IdxList Indices identifying the insertion position.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldInsertValue(Value *Agg, Value *Val,
                         ArrayRef<unsigned> IdxList) const override {
    return simplifyInsertValueInst(Agg, Val, IdxList, SQ);
  }

  /// Fold an extractelement using InstructionSimplify.
  /// \param Vec The vector value to extract from.
  /// \param Idx The element index.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldExtractElement(Value *Vec, Value *Idx) const override {
    return simplifyExtractElementInst(Vec, Idx, SQ);
  }

  /// Fold an insertelement using InstructionSimplify.
  /// \param Vec The vector value to insert into.
  /// \param NewElt The element value to insert.
  /// \param Idx The element index.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldInsertElement(Value *Vec, Value *NewElt,
                           Value *Idx) const override {
    return simplifyInsertElementInst(Vec, NewElt, Idx, SQ);
  }

  /// Fold a shufflevector using InstructionSimplify.
  /// \param V1 The first vector operand.
  /// \param V2 The second vector operand.
  /// \param Mask The shuffle mask.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldShuffleVector(Value *V1, Value *V2,
                           ArrayRef<int> Mask) const override {
    Type *RetTy = VectorType::get(
        cast<VectorType>(V1->getType())->getElementType(), Mask.size(),
        isa<ScalableVectorType>(V1->getType()));
    return simplifyShuffleVectorInst(V1, V2, Mask, RetTy, SQ);
  }

  /// Fold a cast using InstructionSimplify.
  /// \param Op The cast opcode.
  /// \param V The value to cast.
  /// \param DestTy The destination type.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldCast(Instruction::CastOps Op, Value *V,
                  Type *DestTy) const override {
    return simplifyCastInst(Op, V, DestTy, SQ);
  }

  /// Fold an intrinsic call using InstructionSimplify.
  /// \param ID The intrinsic identifier.
  /// \param Ops The intrinsic operands.
  /// \param Ty The result type of the intrinsic.
  /// \param FMF Fast-math flags for the call.
  /// \param CtxF Optional context function for the intrinsic.
  /// @return The simplified value, or nullptr if the operation cannot be folded.
  Value *FoldIntrinsic(Intrinsic::ID ID, ArrayRef<Value *> Ops, Type *Ty,
                       FastMathFlags FMF = {},
                       Function *CtxF = nullptr) const override {
    return simplifyIntrinsic(ID, Ty, Ops, FMF, SQ, CtxF);
  }

  //===--------------------------------------------------------------------===//
  // Cast/Conversion Operators
  //===--------------------------------------------------------------------===//

  /// Create a pointer cast of constant \p C to type \p DestTy.
  /// \param C The constant pointer to cast.
  /// \param DestTy The destination type.
  /// @return The pointer cast of \p C to \p DestTy, or \p C if already that type.
  Value *CreatePointerCast(Constant *C, Type *DestTy) const override {
    if (C->getType() == DestTy)
      return C; // avoid calling Fold
    return ConstFolder.CreatePointerCast(C, DestTy);
  }

  /// Create a bitcast or addrspacecast of constant pointer \p C to \p DestTy.
  /// \param C The constant pointer to cast.
  /// \param DestTy The destination type.
  /// @return The cast of \p C to \p DestTy, or \p C if already that type.
  Value *CreatePointerBitCastOrAddrSpaceCast(Constant *C,
                                             Type *DestTy) const override {
    if (C->getType() == DestTy)
      return C; // avoid calling Fold
    return ConstFolder.CreatePointerBitCastOrAddrSpaceCast(C, DestTy);
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_INSTSIMPLIFYFOLDER_H
