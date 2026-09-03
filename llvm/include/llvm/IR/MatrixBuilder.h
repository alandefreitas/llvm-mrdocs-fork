//===- llvm/MatrixBuilder.h - Builder to lower matrix ops -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MatrixBuilder class, which is used as a convenient way
// to lower matrix operations to LLVM IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MATRIXBUILDER_H
#define LLVM_IR_MATRIXBUILDER_H

#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Alignment.h"

namespace llvm {

class Function;
class Twine;
class Module;

/// Helper for lowering matrix operations to LLVM IR.
class MatrixBuilder {
  IRBuilderBase &B;
  Module *getModule() { return B.GetInsertBlock()->getParent()->getParent(); }

  std::pair<Value *, Value *> splatScalarOperandIfNeeded(Value *LHS,
                                                         Value *RHS) {
    assert((LHS->getType()->isVectorTy() || RHS->getType()->isVectorTy()) &&
           "One of the operands must be a matrix (embedded in a vector)");
    if (LHS->getType()->isVectorTy() && !RHS->getType()->isVectorTy()) {
      assert(!isa<ScalableVectorType>(LHS->getType()) &&
             "LHS Assumed to be fixed width");
      RHS = B.CreateVectorSplat(
          cast<VectorType>(LHS->getType())->getElementCount(), RHS,
          "scalar.splat");
    } else if (!LHS->getType()->isVectorTy() && RHS->getType()->isVectorTy()) {
      assert(!isa<ScalableVectorType>(RHS->getType()) &&
             "RHS Assumed to be fixed width");
      LHS = B.CreateVectorSplat(
          cast<VectorType>(RHS->getType())->getElementCount(), LHS,
          "scalar.splat");
    }
    return {LHS, RHS};
  }

public:
  /// Create a MatrixBuilder that emits IR with the given builder.
  /// \param Builder The IR builder used to emit matrix operations.
  MatrixBuilder(IRBuilderBase &Builder) : B(Builder) {}

  /// Create a column-major, strided matrix load.
  /// \param EltTy Matrix element type.
  /// \param DataPtr Start address of the matrix read.
  /// \param Alignment Alignment of the load.
  /// \param Stride Space between columns.
  /// \param IsVolatile Whether the load is volatile.
  /// \param Rows Number of rows in the matrix (must be a constant).
  /// \param Columns Number of columns in the matrix (must be a constant).
  /// \param Name Name for the created instruction.
  /// \return A call to the column-major matrix load intrinsic.
  CallInst *CreateColumnMajorLoad(Type *EltTy, Value *DataPtr, Align Alignment,
                                  Value *Stride, bool IsVolatile, unsigned Rows,
                                  unsigned Columns, const Twine &Name = "") {
    auto *RetType = FixedVectorType::get(EltTy, Rows * Columns);

    Value *Ops[] = {DataPtr, Stride, B.getInt1(IsVolatile), B.getInt32(Rows),
                    B.getInt32(Columns)};
    Type *OverloadedTypes[] = {RetType, Stride->getType()};

    Function *TheFn = Intrinsic::getOrInsertDeclaration(
        getModule(), Intrinsic::matrix_column_major_load, OverloadedTypes);

    CallInst *Call = B.CreateCall(TheFn->getFunctionType(), TheFn, Ops, Name);
    Attribute AlignAttr =
        Attribute::getWithAlignment(Call->getContext(), Alignment);
    Call->addParamAttr(0, AlignAttr);
    return Call;
  }

  /// Create a column-major, strided matrix store.
  /// \param Matrix Matrix to store.
  /// \param Ptr Pointer to write back to.
  /// \param Alignment Alignment of the store.
  /// \param Stride Space between columns.
  /// \param IsVolatile Whether the store is volatile.
  /// \param Rows Number of rows in the matrix.
  /// \param Columns Number of columns in the matrix.
  /// \param Name Name for the created instruction.
  /// \return A call to the column-major matrix store intrinsic.
  CallInst *CreateColumnMajorStore(Value *Matrix, Value *Ptr, Align Alignment,
                                   Value *Stride, bool IsVolatile,
                                   unsigned Rows, unsigned Columns,
                                   const Twine &Name = "") {
    Value *Ops[] = {Matrix,           Ptr,
                    Stride,           B.getInt1(IsVolatile),
                    B.getInt32(Rows), B.getInt32(Columns)};
    Type *OverloadedTypes[] = {Matrix->getType(), Stride->getType()};

    Function *TheFn = Intrinsic::getOrInsertDeclaration(
        getModule(), Intrinsic::matrix_column_major_store, OverloadedTypes);

    CallInst *Call = B.CreateCall(TheFn->getFunctionType(), TheFn, Ops, Name);
    Attribute AlignAttr =
        Attribute::getWithAlignment(Call->getContext(), Alignment);
    Call->addParamAttr(1, AlignAttr);
    return Call;
  }

  /// Create a llvm.matrix.transpose call for the given matrix.
  /// \param Matrix Matrix to transpose.
  /// \param Rows Number of rows in the matrix.
  /// \param Columns Number of columns in the matrix.
  /// \param Name Name for the created instruction.
  /// \return A call to the matrix transpose intrinsic.
  CallInst *CreateMatrixTranspose(Value *Matrix, unsigned Rows,
                                  unsigned Columns, const Twine &Name = "") {
    auto *OpType = cast<VectorType>(Matrix->getType());
    auto *ReturnType =
        FixedVectorType::get(OpType->getElementType(), Rows * Columns);

    Type *OverloadedTypes[] = {ReturnType};
    Value *Ops[] = {Matrix, B.getInt32(Rows), B.getInt32(Columns)};
    Function *TheFn = Intrinsic::getOrInsertDeclaration(
        getModule(), Intrinsic::matrix_transpose, OverloadedTypes);

    return B.CreateCall(TheFn->getFunctionType(), TheFn, Ops, Name);
  }

  /// Create a llvm.matrix.multiply call multiplying two matrices.
  /// \param LHS Left-hand matrix operand.
  /// \param RHS Right-hand matrix operand.
  /// \param LHSRows Number of rows in the left-hand matrix.
  /// \param LHSColumns Number of columns in the left-hand matrix.
  /// \param RHSColumns Number of columns in the right-hand matrix.
  /// \param Name Name for the created instruction.
  /// \return A call to the matrix multiply intrinsic.
  CallInst *CreateMatrixMultiply(Value *LHS, Value *RHS, unsigned LHSRows,
                                 unsigned LHSColumns, unsigned RHSColumns,
                                 const Twine &Name = "") {
    auto *LHSType = cast<VectorType>(LHS->getType());
    auto *RHSType = cast<VectorType>(RHS->getType());

    auto *ReturnType =
        FixedVectorType::get(LHSType->getElementType(), LHSRows * RHSColumns);

    Value *Ops[] = {LHS, RHS, B.getInt32(LHSRows), B.getInt32(LHSColumns),
                    B.getInt32(RHSColumns)};
    Type *OverloadedTypes[] = {ReturnType, LHSType, RHSType};

    Function *TheFn = Intrinsic::getOrInsertDeclaration(
        getModule(), Intrinsic::matrix_multiply, OverloadedTypes);
    return B.CreateCall(TheFn->getFunctionType(), TheFn, Ops, Name);
  }

  /// Create a column-major matrix from a row-major matrix by transposing it.
  ///
  /// Uses the given logical dimensions. Assumes matrix transpose uses
  /// column-major matrix memory layout, which is true for the DirectX and
  /// SPIRV backends, but not necessarily for the LowerMatrixIntrinsics pass.
  /// \param Matrix The row-major matrix value to transform.
  /// \param Rows Number of rows in the logical matrix.
  /// \param Columns Number of columns in the logical matrix.
  /// \param Name Name for the created instruction.
  /// \return A call to the matrix transpose intrinsic producing the column-major form.
  CallInst *CreateRowMajorToColumnMajorTransform(Value *Matrix, unsigned Rows,
                                                 unsigned Columns,
                                                 const Twine &Name = "") {
    return CreateMatrixTranspose(Matrix, Columns, Rows, Name);
  }

  /// Create a row-major matrix from a column-major matrix by transposing it.
  ///
  /// Uses the given logical dimensions. Assumes matrix transpose uses
  /// column-major matrix memory layout, which is true for the DirectX and
  /// SPIRV backends, but not necessarily for the LowerMatrixIntrinsics pass.
  /// \param Matrix The column-major matrix value to transform.
  /// \param Rows Number of rows in the logical matrix.
  /// \param Columns Number of columns in the logical matrix.
  /// \param Name Name for the created instruction.
  /// \return A call to the matrix transpose intrinsic producing the row-major form.
  CallInst *CreateColumnMajorToRowMajorTransform(Value *Matrix, unsigned Rows,
                                                 unsigned Columns,
                                                 const Twine &Name = "") {
    return CreateMatrixTranspose(Matrix, Rows, Columns, Name);
  }

  /// Insert a single element into a matrix at the given indices.
  /// \param Matrix Matrix to insert into.
  /// \param NewVal Element value to insert.
  /// \param RowIdx Row index of the insertion.
  /// \param ColumnIdx Column index of the insertion.
  /// \param NumRows Number of rows in the matrix.
  /// \return The matrix with \p NewVal inserted at the given indices.
  Value *CreateMatrixInsert(Value *Matrix, Value *NewVal, Value *RowIdx,
                            Value *ColumnIdx, unsigned NumRows) {
    return B.CreateInsertElement(
        Matrix, NewVal,
        B.CreateAdd(B.CreateMul(ColumnIdx, ConstantInt::get(
                                               ColumnIdx->getType(), NumRows)),
                    RowIdx));
  }

  /// Add two matrices, supporting integer and floating-point element types.
  /// \param LHS Left-hand matrix or scalar operand.
  /// \param RHS Right-hand matrix or scalar operand.
  /// \return The matrix sum of \p LHS and \p RHS.
  Value *CreateAdd(Value *LHS, Value *RHS) {
    assert(LHS->getType()->isVectorTy() || RHS->getType()->isVectorTy());
    if (LHS->getType()->isVectorTy() && !RHS->getType()->isVectorTy()) {
      assert(!isa<ScalableVectorType>(LHS->getType()) &&
             "LHS Assumed to be fixed width");
      RHS = B.CreateVectorSplat(
          cast<VectorType>(LHS->getType())->getElementCount(), RHS,
          "scalar.splat");
    } else if (!LHS->getType()->isVectorTy() && RHS->getType()->isVectorTy()) {
      assert(!isa<ScalableVectorType>(RHS->getType()) &&
             "RHS Assumed to be fixed width");
      LHS = B.CreateVectorSplat(
          cast<VectorType>(RHS->getType())->getElementCount(), LHS,
          "scalar.splat");
    }

    return cast<VectorType>(LHS->getType())
                   ->getElementType()
                   ->isFloatingPointTy()
               ? B.CreateFAdd(LHS, RHS)
               : B.CreateAdd(LHS, RHS);
  }

  /// Subtract two matrices, supporting integer and floating-point element types.
  /// \param LHS Left-hand matrix or scalar operand.
  /// \param RHS Right-hand matrix or scalar operand.
  /// \return The matrix difference of \p LHS and \p RHS.
  Value *CreateSub(Value *LHS, Value *RHS) {
    assert(LHS->getType()->isVectorTy() || RHS->getType()->isVectorTy());
    if (LHS->getType()->isVectorTy() && !RHS->getType()->isVectorTy()) {
      assert(!isa<ScalableVectorType>(LHS->getType()) &&
             "LHS Assumed to be fixed width");
      RHS = B.CreateVectorSplat(
          cast<VectorType>(LHS->getType())->getElementCount(), RHS,
          "scalar.splat");
    } else if (!LHS->getType()->isVectorTy() && RHS->getType()->isVectorTy()) {
      assert(!isa<ScalableVectorType>(RHS->getType()) &&
             "RHS Assumed to be fixed width");
      LHS = B.CreateVectorSplat(
          cast<VectorType>(RHS->getType())->getElementCount(), LHS,
          "scalar.splat");
    }

    return cast<VectorType>(LHS->getType())
                   ->getElementType()
                   ->isFloatingPointTy()
               ? B.CreateFSub(LHS, RHS)
               : B.CreateSub(LHS, RHS);
  }

  /// Multiply a matrix by a scalar, or a scalar by a matrix.
  /// \param LHS Left-hand matrix or scalar operand.
  /// \param RHS Right-hand matrix or scalar operand.
  /// \return The matrix product after multiplying by the scalar.
  Value *CreateScalarMultiply(Value *LHS, Value *RHS) {
    std::tie(LHS, RHS) = splatScalarOperandIfNeeded(LHS, RHS);
    if (LHS->getType()->getScalarType()->isFloatingPointTy())
      return B.CreateFMul(LHS, RHS);
    return B.CreateMul(LHS, RHS);
  }

  /// Divide a matrix by a scalar.
  /// \param LHS Matrix dividend.
  /// \param RHS Scalar divisor.
  /// \param IsUnsigned For integer operands, whether to use unsigned division.
  /// \return The matrix quotient after dividing each element by the scalar.
  Value *CreateScalarDiv(Value *LHS, Value *RHS, bool IsUnsigned) {
    assert(LHS->getType()->isVectorTy() && !RHS->getType()->isVectorTy());
    assert(!isa<ScalableVectorType>(LHS->getType()) &&
           "LHS Assumed to be fixed width");
    RHS =
        B.CreateVectorSplat(cast<VectorType>(LHS->getType())->getElementCount(),
                            RHS, "scalar.splat");
    return cast<VectorType>(LHS->getType())
                   ->getElementType()
                   ->isFloatingPointTy()
               ? B.CreateFDiv(LHS, RHS)
               : (IsUnsigned ? B.CreateUDiv(LHS, RHS) : B.CreateSDiv(LHS, RHS));
  }

  /// Create an assumption that an index is less than the element count.
  /// \param Idx Index value that must be in range.
  /// \param NumElements Exclusive upper bound on the index.
  /// \param Name Name for the created comparison.
  void CreateIndexAssumption(Value *Idx, unsigned NumElements,
                             Twine const &Name = "") {
    Value *NumElts =
        B.getIntN(Idx->getType()->getScalarSizeInBits(), NumElements);
    auto *Cmp = B.CreateICmpULT(Idx, NumElts);
    if (isa<ConstantInt>(Cmp))
      assert(cast<ConstantInt>(Cmp)->isOne() && "Index must be valid!");
    else
      B.CreateAssumption(Cmp);
  }

  /// Compute the vector index of a matrix element at the given coordinates.
  /// \param RowIdx Row index of the element.
  /// \param ColumnIdx Column index of the element.
  /// \param NumRows Number of rows in the matrix.
  /// \param NumCols Number of columns in the matrix.
  /// \param IsMatrixRowMajor Whether the matrix uses row-major ordering.
  /// \param Name Name for the created instruction.
  /// \return The linear index of the element in the flattened matrix vector.
  Value *CreateIndex(Value *RowIdx, Value *ColumnIdx, unsigned NumRows,
                     unsigned NumCols, bool IsMatrixRowMajor = false,
                     Twine const &Name = "") {
    unsigned MaxWidth = std::max(RowIdx->getType()->getScalarSizeInBits(),
                                 ColumnIdx->getType()->getScalarSizeInBits());
    Type *IntTy = IntegerType::get(RowIdx->getType()->getContext(), MaxWidth);
    RowIdx = B.CreateZExt(RowIdx, IntTy);
    ColumnIdx = B.CreateZExt(ColumnIdx, IntTy);
    if (IsMatrixRowMajor) {
      Value *NumColsV = B.getIntN(MaxWidth, NumCols);
      return CreateRowMajorIndex(RowIdx, ColumnIdx, NumColsV, Name);
    }
    Value *NumRowsV = B.getIntN(MaxWidth, NumRows);
    return CreateColumnMajorIndex(RowIdx, ColumnIdx, NumRowsV, Name);
  }

private:
  /// Compute the index to access the element at (\p RowIdx, \p ColumnIdx) from
  /// a matrix with \p NumRows embedded in a vector.
  Value *CreateColumnMajorIndex(Value *RowIdx, Value *ColumnIdx,
                                Value *NumRowsV, Twine const &Name) {
    return B.CreateAdd(B.CreateMul(ColumnIdx, NumRowsV), RowIdx);
  }

  /// Compute the index to access the element at (\p RowIdx, \p ColumnIdx) from
  /// a matrix with \p NumCols embedded in a vector.
  Value *CreateRowMajorIndex(Value *RowIdx, Value *ColumnIdx, Value *NumColsV,
                             Twine const &Name) {
    return B.CreateAdd(B.CreateMul(RowIdx, NumColsV), ColumnIdx);
  }
};

} // end namespace llvm

#endif // LLVM_IR_MATRIXBUILDER_H
