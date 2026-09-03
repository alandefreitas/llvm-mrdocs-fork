//==-- ConstantFold.h - DL-independent Constant Folding Interface -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the DataLayout-independent constant folding interface.
// When possible, the DataLayout-aware constant folding interface in
// Analysis/ConstantFolding.h should be preferred.
//
// These interfaces are used by the ConstantExpr::get* methods to automatically
// fold constants when possible.
//
// These operators may return a null object if they don't know how to perform
// the specified operation on the specified constant types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CONSTANTFOLD_H
#define LLVM_IR_CONSTANTFOLD_H

#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {
template <typename T> class ArrayRef;
class Value;
class Constant;
class Type;

// Constant fold various types of instruction...

/// Attempt to constant fold a cast instruction with the specified operand.
///
/// The constant result is returned if successful; if not, null is returned.
/// \param opcode The opcode of the cast.
/// \param V The source constant.
/// \param DestTy The destination type.
/// \return The folded constant, or null if the cast cannot be folded.
LLVM_ABI Constant *ConstantFoldCastInstruction(unsigned opcode, Constant *V,
                                               Type *DestTy);

/// Attempt to constant fold a select instruction with the specified
/// operands. The constant result is returned if successful; if not, null is
/// returned.
/// \param Cond The select condition.
/// \param V1 The value selected when \p Cond is true.
/// \param V2 The value selected when \p Cond is false.
/// \return The folded constant, or null if the select cannot be folded.
LLVM_ABI Constant *ConstantFoldSelectInstruction(Constant *Cond, Constant *V1,
                                                 Constant *V2);

/// Attempt to constant fold an extractelement instruction with the
/// specified operands and indices.
///
/// The constant result is returned if successful; if not, null is returned.
/// \param Val The vector constant to extract from.
/// \param Idx The element index.
/// \return The folded constant, or null if the extract cannot be folded.
LLVM_ABI Constant *ConstantFoldExtractElementInstruction(Constant *Val,
                                                         Constant *Idx);

/// Attempt to constant fold an insertelement instruction with the
/// specified operands and indices.
///
/// The constant result is returned if successful; if not, null is returned.
/// \param Val The vector constant to insert into.
/// \param Elt The element value to insert.
/// \param Idx The element index.
/// \return The folded constant, or null if the insert cannot be folded.
LLVM_ABI Constant *ConstantFoldInsertElementInstruction(Constant *Val,
                                                        Constant *Elt,
                                                        Constant *Idx);

/// Attempt to constant fold a shufflevector instruction with the
/// specified operands and mask.
///
/// See class ShuffleVectorInst for a description of the mask representation.
/// The constant result is returned if successful; if not, null is returned.
/// \param V1 The first vector operand.
/// \param V2 The second vector operand.
/// \param Mask The shuffle mask.
/// \return The folded constant, or null if the shuffle cannot be folded.
LLVM_ABI Constant *ConstantFoldShuffleVectorInstruction(Constant *V1,
                                                        Constant *V2,
                                                        ArrayRef<int> Mask);

/// Attempt to constant fold an extractvalue instruction with the
/// specified operands and indices.
///
/// The constant result is returned if successful; if not, null is returned.
/// \param Agg The aggregate constant to extract from.
/// \param Idxs The indices identifying the extracted element.
/// \return The folded constant, or null if the extract cannot be folded.
LLVM_ABI Constant *ConstantFoldExtractValueInstruction(Constant *Agg,
                                                       ArrayRef<unsigned> Idxs);

/// Attempt to constant fold an insertvalue instruction with the specified
/// operands and indices.
///
/// The constant result is returned if successful; if not, null is returned.
/// \param Agg The aggregate constant to insert into.
/// \param Val The value to insert.
/// \param Idxs The indices identifying the insertion position.
/// \return The folded constant, or null if the insert cannot be folded.
LLVM_ABI Constant *ConstantFoldInsertValueInstruction(Constant *Agg,
                                                      Constant *Val,
                                                      ArrayRef<unsigned> Idxs);

/// Attempt to constant fold a unary instruction with the specified operand.
///
/// The constant result is returned if successful; if not, null is returned.
/// \param Opcode The unary opcode to fold.
/// \param V The constant operand.
/// \return The folded constant, or null if the unary op cannot be folded.
LLVM_ABI Constant *ConstantFoldUnaryInstruction(unsigned Opcode, Constant *V);

/// Attempt to constant fold a binary instruction with the specified
/// operands.
///
/// The constant result is returned if successful; if not, null is returned.
/// \param Opcode The binary opcode to fold.
/// \param V1 The left-hand constant operand.
/// \param V2 The right-hand constant operand.
/// \return The folded constant, or null if the binary op cannot be folded.
LLVM_ABI Constant *ConstantFoldBinaryInstruction(unsigned Opcode, Constant *V1,
                                                 Constant *V2);

/// Attempt to constant fold a compare instruction with the specified
/// operands.
///
/// The constant result is returned if successful; if not, null is returned.
/// \param Predicate The icmp or fcmp predicate to evaluate.
/// \param C1 The left-hand constant operand.
/// \param C2 The right-hand constant operand.
/// \return The folded constant, or null if the compare cannot be folded.
LLVM_ABI Constant *ConstantFoldCompareInstruction(CmpInst::Predicate Predicate,
                                                  Constant *C1, Constant *C2);

/// Attempt to constant fold a getelementptr instruction with the specified
/// operands.
///
/// The constant result is returned if successful; if not, null is returned.
/// \param Ty The source element type for the GEP.
/// \param C The pointer operand.
/// \param InRange The inrange range if present, or std::nullopt.
/// \param Idxs The index operands.
/// \return The folded constant, or null if the GEP cannot be folded.
LLVM_ABI Constant *
ConstantFoldGetElementPtr(Type *Ty, Constant *C,
                          std::optional<ConstantRange> InRange,
                          ArrayRef<Value *> Idxs);
} // namespace llvm

#endif
