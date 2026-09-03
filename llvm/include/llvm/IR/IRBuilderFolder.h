//===- IRBuilderFolder.h - Const folder interface for IRBuilder -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines for constant folding interface used by IRBuilder.
// It is implemented by ConstantFolder (default), TargetFolder and NoFoler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_IRBUILDERFOLDER_H
#define LLVM_IR_IRBUILDERFOLDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/GEPNoWrapFlags.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// IRBuilderFolder - Interface for constant folding in IRBuilder.
class LLVM_ABI IRBuilderFolder {
public:
  /// Destroy the IRBuilderFolder.
  virtual ~IRBuilderFolder();

  //===--------------------------------------------------------------------===//
  // Value-based folders.
  //
  // Return an existing value or a constant if the operation can be simplified.
  // Otherwise return nullptr.
  //===--------------------------------------------------------------------===//

  /// Fold a binary operator if it can be simplified.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldBinOp(Instruction::BinaryOps Opc, Value *LHS,
                           Value *RHS) const = 0;

  /// Fold an exact binary operator if it can be simplified.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param IsExact Whether the operation is exact.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldExactBinOp(Instruction::BinaryOps Opc, Value *LHS,
                                Value *RHS, bool IsExact) const = 0;

  /// Fold a no-wrap binary operator if it can be simplified.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param HasNUW Whether the operation has the nuw flag.
  /// \param HasNSW Whether the operation has the nsw flag.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldNoWrapBinOp(Instruction::BinaryOps Opc, Value *LHS,
                                 Value *RHS, bool HasNUW,
                                 bool HasNSW) const = 0;

  /// Fold a binary operator with fast-math flags if it can be simplified.
  /// \param Opc The binary opcode to fold.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \param FMF Fast-math flags for the operation.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldBinOpFMF(Instruction::BinaryOps Opc, Value *LHS,
                              Value *RHS, FastMathFlags FMF) const = 0;

  /// Fold a unary operator with fast-math flags if it can be simplified.
  /// \param Opc The unary opcode to fold.
  /// \param V The operand value.
  /// \param FMF Fast-math flags for the operation.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldUnOpFMF(Instruction::UnaryOps Opc, Value *V,
                             FastMathFlags FMF) const = 0;

  /// Fold a compare if it can be simplified.
  /// \param P The compare predicate.
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldCmp(CmpInst::Predicate P, Value *LHS,
                         Value *RHS) const = 0;

  /// Fold a GEP if it can be simplified.
  /// \param Ty The GEP source element type.
  /// \param Ptr The base pointer value.
  /// \param IdxList The GEP index values.
  /// \param NW No-wrap flags for the GEP.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldGEP(Type *Ty, Value *Ptr, ArrayRef<Value *> IdxList,
                         GEPNoWrapFlags NW) const = 0;

  /// Fold a select if it can be simplified.
  /// \param C The select condition.
  /// \param True The value selected when the condition is true.
  /// \param False The value selected when the condition is false.
  /// \param FMF Fast-math flags for the select.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldSelect(Value *C, Value *True, Value *False,
                            FastMathFlags FMF = FastMathFlags()) const = 0;

  /// Fold an extractvalue if it can be simplified.
  /// \param Agg The aggregate value to extract from.
  /// \param IdxList Indices identifying the extracted element.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldExtractValue(Value *Agg,
                                  ArrayRef<unsigned> IdxList) const = 0;

  /// Fold an insertvalue if it can be simplified.
  /// \param Agg The aggregate value to insert into.
  /// \param Val The value to insert.
  /// \param IdxList Indices identifying the insertion position.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldInsertValue(Value *Agg, Value *Val,
                                 ArrayRef<unsigned> IdxList) const = 0;

  /// Fold an extractelement if it can be simplified.
  /// \param Vec The vector value to extract from.
  /// \param Idx The element index.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldExtractElement(Value *Vec, Value *Idx) const = 0;

  /// Fold an insertelement if it can be simplified.
  /// \param Vec The vector value to insert into.
  /// \param NewElt The element value to insert.
  /// \param Idx The element index.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldInsertElement(Value *Vec, Value *NewElt,
                                   Value *Idx) const = 0;

  /// Fold a shufflevector if it can be simplified.
  /// \param V1 The first vector operand.
  /// \param V2 The second vector operand.
  /// \param Mask The shuffle mask.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldShuffleVector(Value *V1, Value *V2,
                                   ArrayRef<int> Mask) const = 0;

  /// Fold a cast if it can be simplified.
  /// \param Op The cast opcode.
  /// \param V The value to cast.
  /// \param DestTy The destination type.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldCast(Instruction::CastOps Op, Value *V,
                          Type *DestTy) const = 0;

  /// Fold an intrinsic call if it can be simplified.
  /// \param ID The intrinsic identifier.
  /// \param Ops The intrinsic operands.
  /// \param Ty The result type of the intrinsic.
  /// \param FMF Fast-math flags for the call.
  /// \param CtxF Optional context function for the intrinsic.
  /// \return The folded value, or nullptr if the operation cannot be simplified.
  virtual Value *FoldIntrinsic(Intrinsic::ID ID, ArrayRef<Value *> Ops,
                               Type *Ty, FastMathFlags FMF = {},
                               Function *CtxF = nullptr) const = 0;

  //===--------------------------------------------------------------------===//
  // Cast/Conversion Operators
  //===--------------------------------------------------------------------===//

  /// Create a pointer cast of constant \p C to type \p DestTy.
  /// \param C The constant pointer to cast.
  /// \param DestTy The destination type.
  /// \return The constant pointer cast to \p DestTy.
  virtual Value *CreatePointerCast(Constant *C, Type *DestTy) const = 0;

  /// Create a bitcast or addrspacecast of constant pointer \p C to \p DestTy.
  /// \param C The constant pointer to cast.
  /// \param DestTy The destination type.
  /// \return The constant after a bitcast or addrspacecast to \p DestTy.
  virtual Value *CreatePointerBitCastOrAddrSpaceCast(Constant *C,
                                                     Type *DestTy) const = 0;
};

} // end namespace llvm

#endif // LLVM_IR_IRBUILDERFOLDER_H
