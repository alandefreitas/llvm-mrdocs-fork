//===- llvm/InstrTypes.h - Important Instruction subclasses -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines various meta classes of instructions that exist in the VM
// representation.  Specific concrete subclasses of these may be found in the
// i*.h files...
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INSTRTYPES_H
#define LLVM_IR_INSTRTYPES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/User.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class StringRef;
class Type;
class Value;
class ConstantRange;

namespace Intrinsic {
/// Unique identifier for an LLVM intrinsic.
typedef unsigned ID;
}

/// Provide fast-math flags storage, instructions that support fast-math flags
/// should inherit from this class.
class FastMathFlagsStorage {
  friend class FPMathOperator;

protected:
  /// Fast-math flags stored by the instruction.
  FastMathFlags FMF;
};

//===----------------------------------------------------------------------===//
//                          UnaryInstruction Class
//===----------------------------------------------------------------------===//

/// Base class for instructions with exactly one operand.
class UnaryInstruction : public Instruction {
  constexpr static IntrusiveOperandsAllocMarker AllocMarker{1};

protected:
  /// Construct a unary instruction with result type \p Ty, opcode \p iType,
  /// and operand \p V, optionally inserting before \p InsertBefore.
  /// \param Ty The result type of the instruction.
  /// \param iType The instruction opcode.
  /// \param V The single operand value.
  /// \param InsertBefore Optional insertion point for the new instruction.
  UnaryInstruction(Type *Ty, unsigned iType, Value *V,
                   InsertPosition InsertBefore = nullptr)
      : Instruction(Ty, iType, AllocMarker, InsertBefore) {
    Op<0>() = V;
  }

public:
  /// Allocate a unary instruction with space for its single operand.
  /// \param S Allocation size in bytes.
  /// @return Pointer to the allocated storage.
  void *operator new(size_t S) { return User::operator new(S, AllocMarker); }
  /// Deallocate a unary instruction created with the fixed-size allocator.
  /// \param Ptr Pointer returned by the fixed-size \c operator new.
  void operator delete(void *Ptr) { User::operator delete(Ptr, AllocMarker); }

  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// @return The operand value at that index.
  inline Value *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Value *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return Iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return Const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return Iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return Const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at a compile-time index.
  /// @return Reference to the operand Use.
  template <int> inline Use &Op();
  /// Return a const reference to the operand at a compile-time index.
  /// @return Const reference to the operand Use.
  template <int> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I is a unary instruction or related single-operand
  /// instruction.
  static bool classof(const Instruction *I) {
    return I->isUnaryOp() || I->getOpcode() == Instruction::Alloca ||
           I->getOpcode() == Instruction::Load ||
           I->getOpcode() == Instruction::VAArg ||
           I->getOpcode() == Instruction::ExtractValue ||
           I->getOpcode() == Instruction::Freeze ||
           (I->getOpcode() >= CastOpsBegin && I->getOpcode() < CastOpsEnd);
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a UnaryInstruction.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

/// Operand layout traits for UnaryInstruction.
template <>
struct OperandTraits<UnaryInstruction> :
  public FixedNumOperandTraits<UnaryInstruction, 1> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(UnaryInstruction, Value)

//===----------------------------------------------------------------------===//
//                                UnaryOperator Class
//===----------------------------------------------------------------------===//

/// Instruction representing a standard unary operator.
class UnaryOperator : public UnaryInstruction {
  void AssertOK();

protected:
  /// Construct a unary operator with opcode \p iType and operand \p S.
  /// \param iType The unary opcode.
  /// \param S The operand value.
  /// \param Ty The result type.
  /// \param Name The instruction name.
  /// \param InsertBefore Optional insertion point for the new instruction.
  LLVM_ABI UnaryOperator(UnaryOps iType, Value *S, Type *Ty, const Twine &Name,
                         InsertPosition InsertBefore);

  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Create a copy of this unary operator without inserting it into a block.
  /// @return A clone of this instruction.
  LLVM_ABI UnaryOperator *cloneImpl() const;

public:
  /// Create a unary instruction with the given opcode and operand.
  ///
  /// Optionally (if InstBefore is specified) insert the instruction
  /// into a BasicBlock right before the specified instruction.  The specified
  /// Instruction is allowed to be a dereferenced end iterator.
  /// \param Op The unary opcode.
  /// \param S The operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Optional insertion point for the new instruction.
  /// @return The newly created unary operator.
  LLVM_ABI static UnaryOperator *Create(UnaryOps Op, Value *S,
                                        const Twine &Name = Twine(),
                                        InsertPosition InsertBefore = nullptr);

  /// Declare a Create helper that forwards to Create for one unary opcode.
  ///
  /// These methods just forward to Create, and are useful when you
  /// statically know what type of instruction you're going to create.  These
  /// helpers just save some typing.
  /// \param N The opcode enumeration value.
  /// \param OPC The opcode name token used to form Create##OPC.
  /// \param CLASS The instruction class for this opcode.
#define HANDLE_UNARY_INST(N, OPC, CLASS)                                       \
  static UnaryOperator *Create##OPC(Value *V, const Twine &Name = "") {        \
    return Create(Instruction::OPC, V, Name);                                  \
  }
#include "llvm/IR/Instruction.def"
  /// Declare a Create helper with an explicit insertion point for one unary
  /// opcode.
  ///
  /// These methods just forward to Create, and are useful when you
  /// statically know what type of instruction you're going to create.  These
  /// helpers just save some typing.
  /// \param N The opcode enumeration value.
  /// \param OPC The opcode name token used to form Create##OPC.
  /// \param CLASS The instruction class for this opcode.
#define HANDLE_UNARY_INST(N, OPC, CLASS)                                       \
  static UnaryOperator *Create##OPC(Value *V, const Twine &Name,               \
                                    InsertPosition InsertBefore = nullptr) {   \
    return Create(Instruction::OPC, V, Name, InsertBefore);                    \
  }
#include "llvm/IR/Instruction.def"

  /// Create a unary instruction and copy IR flags from \p CopyO.
  ///
  /// Copies overflow, fast-math, and other IR flags from the source
  /// instruction.
  /// \param Opc The unary opcode.
  /// \param V The operand value.
  /// \param CopyO Instruction whose IR flags are copied.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Optional insertion point for the new instruction.
  /// @return The newly created unary operator.
  static UnaryOperator *
  CreateWithCopiedFlags(UnaryOps Opc, Value *V, Instruction *CopyO,
                        const Twine &Name = "",
                        InsertPosition InsertBefore = nullptr) {
    UnaryOperator *UO = Create(Opc, V, Name, InsertBefore);
    UO->copyIRFlags(CopyO);
    return UO;
  }

  /// Create an \c fneg instruction with fast-math flags copied from \p FMFSource.
  /// \param Op The operand value.
  /// \param FMFSource Instruction whose fast-math flags are copied.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Optional insertion point for the new instruction.
  /// @return The newly created \c fneg operator.
  static UnaryOperator *CreateFNegFMF(Value *Op, Instruction *FMFSource,
                                      const Twine &Name = "",
                                      InsertPosition InsertBefore = nullptr) {
    return CreateWithCopiedFlags(Instruction::FNeg, Op, FMFSource, Name,
                                 InsertBefore);
  }

  /// Return the unary opcode of this instruction.
  /// @return The instruction's unary opcode.
  UnaryOps getOpcode() const {
    return static_cast<UnaryOps>(Instruction::getOpcode());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I is a UnaryOperator.
  static bool classof(const Instruction *I) {
    return I->isUnaryOp();
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a UnaryOperator.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

/// Unary operator with fast-math flags.
///
/// Users should not use this class directly. UnaryOperator creates
/// instructions with the correct type automatically.
class FPUnaryOperator : public UnaryOperator, public FastMathFlagsStorage {
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;
  friend class UnaryOperator;
  using UnaryOperator::UnaryOperator;

  /// Create a copy of this FP unary operator without inserting it into a block.
  /// @return A clone of this instruction.
  LLVM_ABI FPUnaryOperator *cloneImpl() const;

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I is an FPUnaryOperator.
  static bool classof(const Instruction *I) {
    switch (I->getOpcode()) {
    case Instruction::FNeg:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an FPUnaryOperator.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                           BinaryOperator Class
//===----------------------------------------------------------------------===//

/// Instruction representing a standard binary operator.
class BinaryOperator : public Instruction {
  constexpr static IntrusiveOperandsAllocMarker AllocMarker{2};

  void AssertOK();

protected:
  /// Construct a binary operator with opcode \p iType and operands \p S1 and
  /// \p S2.
  /// \param iType The binary opcode.
  /// \param S1 The first operand value.
  /// \param S2 The second operand value.
  /// \param Ty The result type.
  /// \param Name The instruction name.
  /// \param InsertBefore Optional insertion point for the new instruction.
  LLVM_ABI BinaryOperator(BinaryOps iType, Value *S1, Value *S2, Type *Ty,
                          const Twine &Name, InsertPosition InsertBefore);

  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Create a copy of this binary operator without inserting it into a block.
  /// @return A clone of this instruction.
  LLVM_ABI BinaryOperator *cloneImpl() const;

public:
  /// Allocate a binary operator with space for its two fixed operands.
  /// \param S Allocation size in bytes.
  /// @return Pointer to the allocated storage.
  void *operator new(size_t S) { return User::operator new(S, AllocMarker); }
  /// Deallocate a binary operator created with the fixed-size allocator.
  /// \param Ptr Pointer returned by the fixed-size \c operator new.
  void operator delete(void *Ptr) { User::operator delete(Ptr, AllocMarker); }

  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// @return The operand value at that index.
  inline Value *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Value *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return Iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return Const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return Iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return Const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at a compile-time index.
  /// @return Reference to the operand Use.
  template <int> inline Use &Op();
  /// Return a const reference to the operand at a compile-time index.
  /// @return Const reference to the operand Use.
  template <int> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Create a binary instruction with the given opcode and operands.
  ///
  /// Optionally (if InstBefore is specified) insert the instruction
  /// into a BasicBlock right before the specified instruction.  The specified
  /// Instruction is allowed to be a dereferenced end iterator.
  /// \param Op The binary opcode.
  /// \param S1 The first operand value.
  /// \param S2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Optional insertion point for the new instruction.
  /// @return The newly created binary operator.
  LLVM_ABI static BinaryOperator *Create(BinaryOps Op, Value *S1, Value *S2,
                                         const Twine &Name = Twine(),
                                         InsertPosition InsertBefore = nullptr);

  /// Declare a Create helper that forwards to Create for one binary opcode.
  ///
  /// These methods just forward to Create, and are useful when you
  /// statically know what type of instruction you're going to create.  These
  /// helpers just save some typing.
  /// \param N The opcode enumeration value.
  /// \param OPC The opcode name token used to form Create##OPC.
  /// \param CLASS The instruction class for this opcode.
#define HANDLE_BINARY_INST(N, OPC, CLASS)                                      \
  static BinaryOperator *Create##OPC(Value *V1, Value *V2,                     \
                                     const Twine &Name = "") {                 \
    return Create(Instruction::OPC, V1, V2, Name);                             \
  }
#include "llvm/IR/Instruction.def"
  /// Declare a Create helper with an explicit insertion point for one binary
  /// opcode.
  ///
  /// These methods just forward to Create, and are useful when you
  /// statically know what type of instruction you're going to create.  These
  /// helpers just save some typing.
  /// \param N The opcode enumeration value.
  /// \param OPC The opcode name token used to form Create##OPC.
  /// \param CLASS The instruction class for this opcode.
#define HANDLE_BINARY_INST(N, OPC, CLASS)                                      \
  static BinaryOperator *Create##OPC(Value *V1, Value *V2, const Twine &Name,  \
                                     InsertPosition InsertBefore) {            \
    return Create(Instruction::OPC, V1, V2, Name, InsertBefore);               \
  }
#include "llvm/IR/Instruction.def"

  /// Create a binary instruction and copy IR flags from \p CopyO.
  ///
  /// Copies overflow, fast-math, and other IR flags from the source
  /// instruction.
  /// \param Opc The binary opcode.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param CopyO Instruction whose IR flags are copied.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Optional insertion point for the new instruction.
  /// @return The newly created binary operator.
  static BinaryOperator *
  CreateWithCopiedFlags(BinaryOps Opc, Value *V1, Value *V2, Value *CopyO,
                        const Twine &Name = "",
                        InsertPosition InsertBefore = nullptr) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, InsertBefore);
    BO->copyIRFlags(CopyO);
    return BO;
  }

  /// Create a binary instruction with opcode \p Opc and fast-math flags \p FMF.
  /// \param Opc The binary opcode.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param FMF Fast-math flags for the new instruction.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Optional insertion point for the new instruction.
  /// @return The newly created binary operator.
  static BinaryOperator *CreateWithFMF(BinaryOps Opc, Value *V1, Value *V2,
                                       FastMathFlags FMF,
                                       const Twine &Name = "",
                                       InsertPosition InsertBefore = nullptr) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, InsertBefore);
    BO->setFastMathFlags(FMF);
    return BO;
  }

  /// Create an \c fadd with fast-math flags \p FMF.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param FMF Fast-math flags for the new instruction.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created \c fadd operator.
  static BinaryOperator *CreateFAddFMF(Value *V1, Value *V2, FastMathFlags FMF,
                                       const Twine &Name = "") {
    return CreateWithFMF(Instruction::FAdd, V1, V2, FMF, Name);
  }
  /// Create an \c fsub with fast-math flags \p FMF.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param FMF Fast-math flags for the new instruction.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created \c fsub operator.
  static BinaryOperator *CreateFSubFMF(Value *V1, Value *V2, FastMathFlags FMF,
                                       const Twine &Name = "") {
    return CreateWithFMF(Instruction::FSub, V1, V2, FMF, Name);
  }
  /// Create an \c fmul with fast-math flags \p FMF.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param FMF Fast-math flags for the new instruction.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created \c fmul operator.
  static BinaryOperator *CreateFMulFMF(Value *V1, Value *V2, FastMathFlags FMF,
                                       const Twine &Name = "") {
    return CreateWithFMF(Instruction::FMul, V1, V2, FMF, Name);
  }
  /// Create an \c fdiv with fast-math flags \p FMF.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param FMF Fast-math flags for the new instruction.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created \c fdiv operator.
  static BinaryOperator *CreateFDivFMF(Value *V1, Value *V2, FastMathFlags FMF,
                                       const Twine &Name = "") {
    return CreateWithFMF(Instruction::FDiv, V1, V2, FMF, Name);
  }

  /// Create an \c fadd with fast-math flags copied from \p FMFSource.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param FMFSource Instruction whose fast-math flags are copied.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created \c fadd operator.
  static BinaryOperator *CreateFAddFMF(Value *V1, Value *V2,
                                       Instruction *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FAdd, V1, V2, FMFSource, Name);
  }
  /// Create an \c fsub with fast-math flags copied from \p FMFSource.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param FMFSource Instruction whose fast-math flags are copied.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created \c fsub operator.
  static BinaryOperator *CreateFSubFMF(Value *V1, Value *V2,
                                       Instruction *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FSub, V1, V2, FMFSource, Name);
  }
  /// Create an \c fmul with fast-math flags copied from \p FMFSource.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param FMFSource Instruction whose fast-math flags are copied.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created \c fmul operator.
  static BinaryOperator *CreateFMulFMF(Value *V1, Value *V2,
                                       Instruction *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FMul, V1, V2, FMFSource, Name);
  }
  /// Create an \c fdiv with fast-math flags copied from \p FMFSource.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param FMFSource Instruction whose fast-math flags are copied.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created \c fdiv operator.
  static BinaryOperator *CreateFDivFMF(Value *V1, Value *V2,
                                       Instruction *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FDiv, V1, V2, FMFSource, Name);
  }
  /// Create an \c frem with fast-math flags copied from \p FMFSource.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param FMFSource Instruction whose fast-math flags are copied.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created \c frem operator.
  static BinaryOperator *CreateFRemFMF(Value *V1, Value *V2,
                                       Instruction *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FRem, V1, V2, FMFSource, Name);
  }

  /// Create a binary instruction with opcode \p Opc and the nsw flag set.
  /// \param Opc The binary opcode.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created binary operator with no-signed-wrap set.
  static BinaryOperator *CreateNSW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name = "") {
    BinaryOperator *BO = Create(Opc, V1, V2, Name);
    BO->setHasNoSignedWrap(true);
    return BO;
  }

  /// Create an nsw binary instruction and insert before \p InsertBefore.
  /// \param Opc The binary opcode.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created binary operator with no-signed-wrap set.
  static BinaryOperator *CreateNSW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name,
                                   InsertPosition InsertBefore) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, InsertBefore);
    BO->setHasNoSignedWrap(true);
    return BO;
  }

  /// Create a binary instruction with opcode \p Opc and the nuw flag set.
  /// \param Opc The binary opcode.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created binary operator with no-unsigned-wrap set.
  static BinaryOperator *CreateNUW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name = "") {
    BinaryOperator *BO = Create(Opc, V1, V2, Name);
    BO->setHasNoUnsignedWrap(true);
    return BO;
  }

  /// Create a nuw binary instruction and insert before \p InsertBefore.
  /// \param Opc The binary opcode.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created binary operator with no-unsigned-wrap set.
  static BinaryOperator *CreateNUW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name,
                                   InsertPosition InsertBefore) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, InsertBefore);
    BO->setHasNoUnsignedWrap(true);
    return BO;
  }

  /// Create a binary instruction with opcode \p Opc and the exact flag set.
  /// \param Opc The binary opcode.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created binary operator with the exact flag set.
  static BinaryOperator *CreateExact(BinaryOps Opc, Value *V1, Value *V2,
                                     const Twine &Name = "") {
    BinaryOperator *BO = Create(Opc, V1, V2, Name);
    BO->setIsExact(true);
    return BO;
  }

  /// Create an exact binary instruction and insert before \p InsertBefore.
  /// \param Opc The binary opcode.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created binary operator with the exact flag set.
  static BinaryOperator *CreateExact(BinaryOps Opc, Value *V1, Value *V2,
                                     const Twine &Name,
                                     InsertPosition InsertBefore) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, InsertBefore);
    BO->setIsExact(true);
    return BO;
  }

  /// Create a disjoint binary instruction with opcode \p Opc.
  /// \param Opc The binary opcode.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created binary operator with the disjoint flag set.
  static inline BinaryOperator *
  CreateDisjoint(BinaryOps Opc, Value *V1, Value *V2, const Twine &Name = "");
  /// Create a disjoint binary instruction and insert before \p InsertBefore.
  /// \param Opc The binary opcode.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created binary operator with the disjoint flag set.
  static inline BinaryOperator *CreateDisjoint(BinaryOps Opc, Value *V1,
                                               Value *V2, const Twine &Name,
                                               InsertPosition InsertBefore);

  /// Create an integer add with the nsw (no signed wrap) flag set.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created nsw add operator.
  static BinaryOperator *CreateNSWAdd(Value *V1, Value *V2,
                                      const Twine &Name = "") {
    return CreateNSW(Instruction::Add, V1, V2, Name);
  }
  /// Create an nsw integer add and insert at \p InsertBefore.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created nsw add operator.
  static BinaryOperator *CreateNSWAdd(Value *V1, Value *V2, const Twine &Name,
                                      InsertPosition InsertBefore) {
    return CreateNSW(Instruction::Add, V1, V2, Name, InsertBefore);
  }
  /// Create an integer add with the nuw (no unsigned wrap) flag set.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created nuw add operator.
  static BinaryOperator *CreateNUWAdd(Value *V1, Value *V2,
                                      const Twine &Name = "") {
    return CreateNUW(Instruction::Add, V1, V2, Name);
  }
  /// Create a nuw integer add and insert at \p InsertBefore.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created nuw add operator.
  static BinaryOperator *CreateNUWAdd(Value *V1, Value *V2, const Twine &Name,
                                      InsertPosition InsertBefore) {
    return CreateNUW(Instruction::Add, V1, V2, Name, InsertBefore);
  }
  /// Create an integer subtract with the nsw (no signed wrap) flag set.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created nsw subtract operator.
  static BinaryOperator *CreateNSWSub(Value *V1, Value *V2,
                                      const Twine &Name = "") {
    return CreateNSW(Instruction::Sub, V1, V2, Name);
  }
  /// Create an nsw integer subtract and insert at \p InsertBefore.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created nsw subtract operator.
  static BinaryOperator *CreateNSWSub(Value *V1, Value *V2, const Twine &Name,
                                      InsertPosition InsertBefore) {
    return CreateNSW(Instruction::Sub, V1, V2, Name, InsertBefore);
  }
  /// Create an integer subtract with the nuw (no unsigned wrap) flag set.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created nuw subtract operator.
  static BinaryOperator *CreateNUWSub(Value *V1, Value *V2,
                                      const Twine &Name = "") {
    return CreateNUW(Instruction::Sub, V1, V2, Name);
  }
  /// Create a nuw integer subtract and insert at \p InsertBefore.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created nuw subtract operator.
  static BinaryOperator *CreateNUWSub(Value *V1, Value *V2, const Twine &Name,
                                      InsertPosition InsertBefore) {
    return CreateNUW(Instruction::Sub, V1, V2, Name, InsertBefore);
  }
  /// Create an integer multiply with the nsw (no signed wrap) flag set.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created nsw multiply operator.
  static BinaryOperator *CreateNSWMul(Value *V1, Value *V2,
                                      const Twine &Name = "") {
    return CreateNSW(Instruction::Mul, V1, V2, Name);
  }
  /// Create an nsw integer multiply and insert at \p InsertBefore.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created nsw multiply operator.
  static BinaryOperator *CreateNSWMul(Value *V1, Value *V2, const Twine &Name,
                                      InsertPosition InsertBefore) {
    return CreateNSW(Instruction::Mul, V1, V2, Name, InsertBefore);
  }
  /// Create an integer multiply with the nuw (no unsigned wrap) flag set.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created nuw multiply operator.
  static BinaryOperator *CreateNUWMul(Value *V1, Value *V2,
                                      const Twine &Name = "") {
    return CreateNUW(Instruction::Mul, V1, V2, Name);
  }
  /// Create a nuw integer multiply and insert at \p InsertBefore.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created nuw multiply operator.
  static BinaryOperator *CreateNUWMul(Value *V1, Value *V2, const Twine &Name,
                                      InsertPosition InsertBefore) {
    return CreateNUW(Instruction::Mul, V1, V2, Name, InsertBefore);
  }
  /// Create a left-shift instruction with the nsw (no signed wrap) flag set.
  /// \param V1 The value to shift.
  /// \param V2 The shift amount.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created nsw shift-left operator.
  static BinaryOperator *CreateNSWShl(Value *V1, Value *V2,
                                        const Twine &Name = "") {
    return CreateNSW(Instruction::Shl, V1, V2, Name);
  }
  /// Create a left-shift with nsw and insert at \p InsertBefore.
  /// \param V1 The value to shift.
  /// \param V2 The shift amount.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created nsw shift-left operator.
  static BinaryOperator *CreateNSWShl(Value *V1, Value *V2, const Twine &Name,
                                      InsertPosition InsertBefore) {
    return CreateNSW(Instruction::Shl, V1, V2, Name, InsertBefore);
  }
  /// Create a left-shift instruction with the nuw (no unsigned wrap) flag set.
  /// \param V1 The value to shift.
  /// \param V2 The shift amount.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created nuw shift-left operator.
  static BinaryOperator *CreateNUWShl(Value *V1, Value *V2,
                                        const Twine &Name = "") {
    return CreateNUW(Instruction::Shl, V1, V2, Name);
  }
  /// Create a left-shift with nuw and insert at \p InsertBefore.
  /// \param V1 The value to shift.
  /// \param V2 The shift amount.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created nuw shift-left operator.
  static BinaryOperator *CreateNUWShl(Value *V1, Value *V2, const Twine &Name,
                                      InsertPosition InsertBefore) {
    return CreateNUW(Instruction::Shl, V1, V2, Name, InsertBefore);
  }

  /// Create a signed division instruction with the exact flag set.
  /// \param V1 The dividend.
  /// \param V2 The divisor.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created exact signed division operator.
  static BinaryOperator *CreateExactSDiv(Value *V1, Value *V2,
                                         const Twine &Name = "") {
    return CreateExact(Instruction::SDiv, V1, V2, Name);
  }
  /// Create an exact signed division and insert at \p InsertBefore.
  /// \param V1 The dividend.
  /// \param V2 The divisor.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created exact signed division operator.
  static BinaryOperator *CreateExactSDiv(Value *V1, Value *V2, const Twine &Name,
                                         InsertPosition InsertBefore) {
    return CreateExact(Instruction::SDiv, V1, V2, Name, InsertBefore);
  }
  /// Create an unsigned division instruction with the exact flag set.
  /// \param V1 The dividend.
  /// \param V2 The divisor.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created exact unsigned division operator.
  static BinaryOperator *CreateExactUDiv(Value *V1, Value *V2,
                                         const Twine &Name = "") {
    return CreateExact(Instruction::UDiv, V1, V2, Name);
  }
  /// Create an exact unsigned division and insert at \p InsertBefore.
  /// \param V1 The dividend.
  /// \param V2 The divisor.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created exact unsigned division operator.
  static BinaryOperator *CreateExactUDiv(Value *V1, Value *V2, const Twine &Name,
                                         InsertPosition InsertBefore) {
    return CreateExact(Instruction::UDiv, V1, V2, Name, InsertBefore);
  }
  /// Create an arithmetic right-shift instruction with the exact flag set.
  /// \param V1 The value to shift.
  /// \param V2 The shift amount.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created exact arithmetic shift-right operator.
  static BinaryOperator *CreateExactAShr(Value *V1, Value *V2,
                                         const Twine &Name = "") {
    return CreateExact(Instruction::AShr, V1, V2, Name);
  }
  /// Create an exact arithmetic right-shift and insert at \p InsertBefore.
  /// \param V1 The value to shift.
  /// \param V2 The shift amount.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created exact arithmetic shift-right operator.
  static BinaryOperator *CreateExactAShr(Value *V1, Value *V2, const Twine &Name,
                                         InsertPosition InsertBefore) {
    return CreateExact(Instruction::AShr, V1, V2, Name, InsertBefore);
  }
  /// Create a logical right-shift instruction with the exact flag set.
  /// \param V1 The value to shift.
  /// \param V2 The shift amount.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created exact logical shift-right operator.
  static BinaryOperator *CreateExactLShr(Value *V1, Value *V2,
                                         const Twine &Name = "") {
    return CreateExact(Instruction::LShr, V1, V2, Name);
  }
  /// Create a logical right-shift with exact and insert at \p InsertBefore.
  /// \param V1 The value to shift.
  /// \param V2 The shift amount.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created exact logical shift-right operator.
  static BinaryOperator *CreateExactLShr(Value *V1, Value *V2, const Twine &Name,
                                         InsertPosition InsertBefore) {
    return CreateExact(Instruction::LShr, V1, V2, Name, InsertBefore);
  }

  /// Create a disjoint OR instruction.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// @return The newly created disjoint OR operator.
  static BinaryOperator *CreateDisjointOr(Value *V1, Value *V2,
                                          const Twine &Name = "") {
    return CreateDisjoint(Instruction::Or, V1, V2, Name);
  }
  /// Create a disjoint OR and insert at \p InsertBefore.
  /// \param V1 The first operand value.
  /// \param V2 The second operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Insertion point for the new instruction.
  /// @return The newly created disjoint OR operator.
  static BinaryOperator *CreateDisjointOr(Value *V1, Value *V2,
                                          const Twine &Name,
                                          InsertPosition InsertBefore) {
    return CreateDisjoint(Instruction::Or, V1, V2, Name, InsertBefore);
  }

  /// Create a negation or bitwise-not via subtraction or XOR.
  ///
  /// Helper functions to construct and inspect unary operations (NEG and NOT)
  /// via binary operators SUB and XOR. Create the NEG and NOT instructions out
  /// of SUB and XOR instructions.
  /// \param Op The operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Optional insertion point for the new instruction.
  /// @return The newly created negation operator.
  LLVM_ABI static BinaryOperator *
  CreateNeg(Value *Op, const Twine &Name = "",
            InsertPosition InsertBefore = nullptr);
  /// Create an nsw negation via subtraction from zero.
  /// \param Op The operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Optional insertion point for the new instruction.
  /// @return The newly created nsw negation operator.
  LLVM_ABI static BinaryOperator *
  CreateNSWNeg(Value *Op, const Twine &Name = "",
               InsertPosition InsertBefore = nullptr);
  /// Create a bitwise-not via XOR with all-ones.
  /// \param Op The operand value.
  /// \param Name Optional name for the new instruction.
  /// \param InsertBefore Optional insertion point for the new instruction.
  /// @return The newly created bitwise-not operator.
  LLVM_ABI static BinaryOperator *
  CreateNot(Value *Op, const Twine &Name = "",
            InsertPosition InsertBefore = nullptr);

  /// Return the binary opcode of this instruction.
  /// @return The instruction's binary opcode.
  BinaryOps getOpcode() const {
    return static_cast<BinaryOps>(Instruction::getOpcode());
  }

  /// Exchange the two operands of this binary instruction.
  ///
  /// This instruction is safe to use on any binary instruction and
  /// does not modify the semantics of the instruction.  If the instruction
  /// cannot be reversed (ie, it's a Div), then return true.
  /// @return True if the operands could not be swapped.
  LLVM_ABI bool swapOperands();

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I is a BinaryOperator.
  static bool classof(const Instruction *I) {
    return I->isBinaryOp();
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a BinaryOperator.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

/// Operand layout traits for BinaryOperator.
template <>
struct OperandTraits<BinaryOperator> :
  public FixedNumOperandTraits<BinaryOperator, 2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(BinaryOperator, Value)

/// Binary OR instruction that may be marked disjoint.
///
/// An or instruction, which can be marked as "disjoint", indicating that the
/// inputs don't have a 1 in the same bit position. Meaning this instruction
/// can also be treated as an add.
class PossiblyDisjointInst : public BinaryOperator {
public:
  /// Disjoint-OR flag stored in SubclassOptionalData.
  enum {
    /// Subclass optional-data bit marking a disjoint OR instruction.
    IsDisjoint = (1 << 0)
  };

  /// Set whether this or is marked disjoint (inputs share no set bits).
  /// \param B True if the OR is disjoint.
  void setIsDisjoint(bool B) {
    SubclassOptionalData =
        (SubclassOptionalData & ~IsDisjoint) | (B * IsDisjoint);
  }

  /// Return whether this OR is marked disjoint.
  /// @return True if the OR is disjoint.
  bool isDisjoint() const { return SubclassOptionalData & IsDisjoint; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I is a PossiblyDisjointInst.
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Or;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a PossiblyDisjointInst.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

/// Create a disjoint binary instruction with opcode \p Opc.
/// \param Opc The binary opcode.
/// \param V1 The first operand value.
/// \param V2 The second operand value.
/// \param Name Optional name for the new instruction.
/// @return The newly created binary operator with the disjoint flag set.
BinaryOperator *BinaryOperator::CreateDisjoint(BinaryOps Opc, Value *V1,
                                               Value *V2, const Twine &Name) {
  BinaryOperator *BO = Create(Opc, V1, V2, Name);
  cast<PossiblyDisjointInst>(BO)->setIsDisjoint(true);
  return BO;
}
/// Create a disjoint binary instruction and insert before \p InsertBefore.
/// \param Opc The binary opcode.
/// \param V1 The first operand value.
/// \param V2 The second operand value.
/// \param Name Optional name for the new instruction.
/// \param InsertBefore Insertion point for the new instruction.
/// @return The newly created binary operator with the disjoint flag set.
BinaryOperator *BinaryOperator::CreateDisjoint(BinaryOps Opc, Value *V1,
                                               Value *V2, const Twine &Name,
                                               InsertPosition InsertBefore) {
  BinaryOperator *BO = Create(Opc, V1, V2, Name, InsertBefore);
  cast<PossiblyDisjointInst>(BO)->setIsDisjoint(true);
  return BO;
}

/// Binary operator with fast-math flags.
///
/// Users should not use this class directly. BinaryOperator creates
/// instructions with the correct type automatically.
class FPBinaryOperator : public BinaryOperator, public FastMathFlagsStorage {
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;
  friend class BinaryOperator;
  LLVM_ABI FPBinaryOperator *cloneImpl() const;
  using BinaryOperator::BinaryOperator;

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I is an FPBinaryOperator.
  static bool classof(const Instruction *I) {
    switch (I->getOpcode()) {
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FDiv:
    case Instruction::FRem:
      return true;
    default:
      return false;
    }
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an FPBinaryOperator.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                               CastInst Class
//===----------------------------------------------------------------------===//

/// Base class of casting instructions.
///
/// This is the base class for all instructions that perform data casts. It is
/// simply provided so that instruction category testing can be performed with
/// code like:
///
/// if (isa<CastInst>(Instr)) { ... }
class CastInst : public UnaryInstruction {
protected:
  /// Construct a cast instruction for subclasses.
  /// \param Ty The result type of the cast.
  /// \param iType The cast opcode.
  /// \param S The value to cast (operand 0).
  /// \param NameStr Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  CastInst(Type *Ty, unsigned iType, Value *S, const Twine &NameStr = "",
           InsertPosition InsertBefore = nullptr)
      : UnaryInstruction(Ty, iType, S, InsertBefore) {
    setName(NameStr);
  }

public:
  /// Construct any CastInst subclass from an opcode.
  ///
  /// Provides a way to construct any of the CastInst subclasses using an
  /// opcode instead of the subclass's constructor. The opcode must be in the
  /// CastOps category (Instruction::isCast(opcode) returns true). This
  /// constructor has insert-before-instruction semantics to automatically
  /// insert the new CastInst before InsertBefore (if it is non-null).
  /// \param Op The cast opcode.
  /// \param S The value to cast (operand 0).
  /// \param Ty The destination type of the cast.
  /// \param Name Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  /// @return The newly created cast instruction.
  LLVM_ABI static CastInst *Create(
      Instruction::CastOps Op, Value *S, Type *Ty, const Twine &Name = "",
      InsertPosition InsertBefore = nullptr);

  /// Create a zero-extension or bitcast cast instruction.
  /// \param S The value to cast (operand 0).
  /// \param Ty The destination type of the cast.
  /// \param Name Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  /// @return The newly created cast instruction.
  LLVM_ABI static CastInst *CreateZExtOrBitCast(
      Value *S, Type *Ty, const Twine &Name = "",
      InsertPosition InsertBefore = nullptr);

  /// Create a sign-extension or bitcast cast instruction.
  /// \param S The value to cast (operand 0).
  /// \param Ty The destination type of the cast.
  /// \param Name Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  /// @return The newly created cast instruction.
  LLVM_ABI static CastInst *CreateSExtOrBitCast(
      Value *S, Type *Ty, const Twine &Name = "",
      InsertPosition InsertBefore = nullptr);

  /// Create a bitcast, address-space cast, or pointer-to-integer cast.
  /// \param S The pointer value to cast (operand 0).
  /// \param Ty The destination type of the cast.
  /// \param Name Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  /// @return The newly created cast instruction.
  LLVM_ABI static CastInst *CreatePointerCast(
      Value *S, Type *Ty, const Twine &Name = "",
      InsertPosition InsertBefore = nullptr);

  /// Create a bitcast or address-space cast instruction.
  /// \param S The pointer value to cast (operand 0).
  /// \param Ty The destination type of the cast.
  /// \param Name Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  /// @return The newly created cast instruction.
  LLVM_ABI static CastInst *CreatePointerBitCastOrAddrSpaceCast(
      Value *S, Type *Ty, const Twine &Name = "",
      InsertPosition InsertBefore = nullptr);

  /// Create a bitcast, pointer-to-integer, or integer-to-pointer cast.
  ///
  /// If the value is a pointer type and the destination an integer type,
  /// creates a PtrToInt cast. If the value is an integer type and the
  /// destination a pointer type, creates an IntToPtr cast. Otherwise, creates
  /// a bitcast.
  /// \param S The value to cast (operand 0).
  /// \param Ty The destination type of the cast.
  /// \param Name Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  /// @return The newly created cast instruction.
  LLVM_ABI static CastInst *CreateBitOrPointerCast(
      Value *S, Type *Ty, const Twine &Name = "",
      InsertPosition InsertBefore = nullptr);

  /// Create a zero-extension, bitcast, or truncation for integer casts.
  /// \param S The integer value to cast (operand 0).
  /// \param Ty The destination type of the cast.
  /// \param isSigned Whether to treat \p S as signed.
  /// \param Name Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  /// @return The newly created cast instruction.
  LLVM_ABI static CastInst *CreateIntegerCast(
      Value *S, Type *Ty, bool isSigned, const Twine &Name = "",
      InsertPosition InsertBefore = nullptr);

  /// Create a floating-point extension, bitcast, or truncation.
  /// \param S The floating-point value to cast.
  /// \param Ty The destination floating-point type.
  /// \param Name Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  /// @return The newly created cast instruction.
  LLVM_ABI static CastInst *CreateFPCast(
      Value *S, Type *Ty, const Twine &Name = "",
      InsertPosition InsertBefore = nullptr);

  /// Create a truncation or bitcast cast instruction.
  /// \param S The value to cast (operand 0).
  /// \param Ty The destination type of the cast.
  /// \param Name Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  /// @return The newly created cast instruction.
  LLVM_ABI static CastInst *CreateTruncOrBitCast(
      Value *S, Type *Ty, const Twine &Name = "",
      InsertPosition InsertBefore = nullptr);

  /// Check whether a bitcast between two types is valid.
  /// \param SrcTy The source type.
  /// \param DestTy The destination type.
  /// @return True if a bitcast from \p SrcTy to \p DestTy is valid.
  LLVM_ABI static bool isBitCastable(Type *SrcTy, Type *DestTy);

  /// Check whether a no-op bitcast or pointer cast is valid.
  ///
  /// This ensures that any pointer<->integer cast has enough bits in the
  /// integer and any other cast is a bitcast.
  /// \param SrcTy The source type.
  /// \param DestTy The destination type.
  /// \param DL The data layout used to size pointer types.
  /// @return True if the cast is valid and changes no bits.
  LLVM_ABI static bool isBitOrNoopPointerCastable(Type *SrcTy, Type *DestTy,
                                                  const DataLayout &DL);

  /// Infer the cast opcode for a value and destination type.
  ///
  /// Returns the opcode necessary to cast Val into Ty using usual casting
  /// rules.
  /// \param Val The value to cast.
  /// \param SrcIsSigned Whether to treat the source as signed.
  /// \param Ty The destination type.
  /// \param DstIsSigned Whether to treat the destination as signed.
  /// @return The cast opcode for the conversion.
  LLVM_ABI static Instruction::CastOps
  getCastOpcode(const Value *Val, bool SrcIsSigned, Type *Ty,
                bool DstIsSigned);

  /// Return true if this cast uses only integer types.
  ///
  /// There are several places where we need to know if a cast instruction
  /// only deals with integer source and destination types. To simplify that
  /// logic, this method is provided.
  /// @return True if both the operand and result types are integral.
  LLVM_ABI bool isIntegerCast() const;

  /// Return true if a cast changes no bits.
  ///
  /// A no-op cast is one that can be effected without changing any bits.
  /// It implies that the source and destination types are the same size. The
  /// DataLayout argument is to determine the pointer size when examining casts
  /// involving Integer and Pointer types. They are no-op casts if the integer
  /// is the same size as the pointer. However, pointer size varies with
  /// platform. Note that a precondition of this method is that the cast is
  /// legal - i.e. the instruction formed with these operands would verify.
  /// \param Opcode The cast opcode.
  /// \param SrcTy The source type.
  /// \param DstTy The destination type.
  /// \param DL The data layout used to size pointer types.
  /// @return True if the cast changes no bits.
  LLVM_ABI static bool isNoopCast(Instruction::CastOps Opcode, Type *SrcTy,
                                Type *DstTy, const DataLayout &DL);

  /// Return true if this cast changes no bits.
  /// \param DL The data layout used to size pointer types.
  /// @return True if this cast changes no bits.
  LLVM_ABI bool isNoopCast(const DataLayout &DL) const;

  /// Return the opcode that replaces an eliminable cast pair.
  ///
  /// Determine how a pair of casts can be eliminated, if they can be at all.
  /// This is a helper function for both CastInst and ConstantExpr.
  /// \param firstOpcode The opcode of the first cast.
  /// \param secondOpcode The opcode of the second cast.
  /// \param SrcTy The source type of the first cast.
  /// \param MidTy The intermediate type shared by both casts.
  /// \param DstTy The destination type of the second cast.
  /// \param DL Optional data layout for pointer sizing.
  /// @return Zero if the pair cannot be eliminated; otherwise the replacing
  /// cast opcode from \p SrcTy to \p DstTy.
  LLVM_ABI static unsigned isEliminableCastPair(
      Instruction::CastOps firstOpcode, Instruction::CastOps secondOpcode,
      Type *SrcTy, Type *MidTy, Type *DstTy, const DataLayout *DL);

  /// Return the cast opcode of this instruction.
  /// @return This instruction's cast opcode.
  Instruction::CastOps getOpcode() const {
    return Instruction::CastOps(Instruction::getOpcode());
  }

  /// Return the source type of this cast.
  /// @return The type of operand 0.
  Type* getSrcTy() const { return getOperand(0)->getType(); }
  /// Return the destination type of this cast.
  /// @return The result type of this instruction.
  Type* getDestTy() const { return getType(); }

  /// Return true if a cast from \p SrcTy to \p DstTy is valid.
  ///
  /// This method can be used to determine if a cast from SrcTy to DstTy using
  /// opcode \p op is valid without creating an instruction.
  /// \param op The proposed cast opcode.
  /// \param SrcTy The source type.
  /// \param DstTy The destination type.
  /// @return True if the proposed cast is valid.
  LLVM_ABI static bool castIsValid(Instruction::CastOps op, Type *SrcTy,
                                   Type *DstTy);
  /// Return true if a cast of \p S to \p DstTy is valid.
  /// \param op The proposed cast opcode.
  /// \param S The value whose type is the source type.
  /// \param DstTy The destination type.
  /// @return True if the proposed cast is valid.
  static bool castIsValid(Instruction::CastOps op, Value *S, Type *DstTy) {
    return castIsValid(op, S->getType(), DstTy);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I is a cast instruction.
  static bool classof(const Instruction *I) {
    return I->isCast();
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a cast instruction.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

/// Cast instruction that may carry a non-negative flag.
///
/// Applies to zero-extension and unsigned integer-to-floating-point casts.
class PossiblyNonNegInst : public CastInst {
public:
  /// Non-negative cast flag stored in SubclassOptionalData.
  enum {
    /// Subclass-data flag indicating a non-negative cast.
    NonNeg = (1 << 0)
  };

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I is a zext or uitofp instruction.
  static bool classof(const Instruction *I) {
    switch (I->getOpcode()) {
    case Instruction::ZExt:
    case Instruction::UIToFP:
      return true;
    default:
      return false;
    }
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a zext or uitofp instruction.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                               CmpInst Class
//===----------------------------------------------------------------------===//

/// Abstract base class of comparison instructions.
///
/// This class is the base class for the comparison instructions.
class CmpInst : public Instruction {
  constexpr static IntrusiveOperandsAllocMarker AllocMarker{2};

public:
  /// Comparison predicates for CmpInst subclasses.
  ///
  /// This enumeration lists the possible predicates for CmpInst subclasses.
  /// Values in the range 0-31 are reserved for FCmpInst, while values in the
  /// range 32-64 are reserved for ICmpInst. This is necessary to ensure the
  /// predicate values are not overlapping between the classes.
  ///
  /// Some passes (e.g. InstCombine) depend on the bit-wise characteristics of
  /// FCMP_* values. Changing the bit patterns requires a potential change to
  /// those passes.
  enum Predicate : unsigned {
    // Opcode            U L G E    Intuitive operation
    FCMP_FALSE = 0, ///< 0 0 0 0    Always false (always folded)
    FCMP_OEQ = 1,   ///< 0 0 0 1    True if ordered and equal
    FCMP_OGT = 2,   ///< 0 0 1 0    True if ordered and greater than
    FCMP_OGE = 3,   ///< 0 0 1 1    True if ordered and greater than or equal
    FCMP_OLT = 4,   ///< 0 1 0 0    True if ordered and less than
    FCMP_OLE = 5,   ///< 0 1 0 1    True if ordered and less than or equal
    FCMP_ONE = 6,   ///< 0 1 1 0    True if ordered and operands are unequal
    FCMP_ORD = 7,   ///< 0 1 1 1    True if ordered (no nans)
    FCMP_UNO = 8,   ///< 1 0 0 0    True if unordered: isnan(X) | isnan(Y)
    FCMP_UEQ = 9,   ///< 1 0 0 1    True if unordered or equal
    FCMP_UGT = 10,  ///< 1 0 1 0    True if unordered or greater than
    FCMP_UGE = 11,  ///< 1 0 1 1    True if unordered, greater than, or equal
    FCMP_ULT = 12,  ///< 1 1 0 0    True if unordered or less than
    FCMP_ULE = 13,  ///< 1 1 0 1    True if unordered, less than, or equal
    FCMP_UNE = 14,  ///< 1 1 1 0    True if unordered or not equal
    FCMP_TRUE = 15, ///< 1 1 1 1    Always true (always folded)
    /// First valid floating-point comparison predicate.
    FIRST_FCMP_PREDICATE = FCMP_FALSE,
    /// Last valid floating-point comparison predicate.
    LAST_FCMP_PREDICATE = FCMP_TRUE,
    /// One past the last valid floating-point comparison predicate.
    BAD_FCMP_PREDICATE = FCMP_TRUE + 1,
    ICMP_EQ = 32,  ///< equal
    ICMP_NE = 33,  ///< not equal
    ICMP_UGT = 34, ///< unsigned greater than
    ICMP_UGE = 35, ///< unsigned greater or equal
    ICMP_ULT = 36, ///< unsigned less than
    ICMP_ULE = 37, ///< unsigned less or equal
    ICMP_SGT = 38, ///< signed greater than
    ICMP_SGE = 39, ///< signed greater or equal
    ICMP_SLT = 40, ///< signed less than
    ICMP_SLE = 41, ///< signed less or equal
    /// First valid integer comparison predicate.
    FIRST_ICMP_PREDICATE = ICMP_EQ,
    /// Last valid integer comparison predicate.
    LAST_ICMP_PREDICATE = ICMP_SLE,
    /// One past the last valid integer comparison predicate.
    BAD_ICMP_PREDICATE = ICMP_SLE + 1
  };
  /// Bitfield element storing the comparison predicate in subclass data.
  using PredicateField =
      Bitfield::Element<Predicate, 0, 6, LAST_ICMP_PREDICATE>;

  /// Return the sequence of all floating-point comparison predicates.
  /// @return An inclusive sequence from FIRST_FCMP_PREDICATE through
  /// LAST_FCMP_PREDICATE.
  static auto FCmpPredicates() {
    return enum_seq_inclusive(Predicate::FIRST_FCMP_PREDICATE,
                              Predicate::LAST_FCMP_PREDICATE,
                              force_iteration_on_noniterable_enum);
  }

  /// Return the sequence of all integer comparison predicates.
  /// @return An inclusive sequence from FIRST_ICMP_PREDICATE through
  /// LAST_ICMP_PREDICATE.
  static auto ICmpPredicates() {
    return enum_seq_inclusive(Predicate::FIRST_ICMP_PREDICATE,
                              Predicate::LAST_ICMP_PREDICATE,
                              force_iteration_on_noniterable_enum);
  }

protected:
  /// Construct a comparison instruction for subclasses.
  /// \param ty The result type of the comparison.
  /// \param op The comparison opcode (ICmp or FCmp).
  /// \param pred The comparison predicate.
  /// \param LHS The left-hand operand.
  /// \param RHS The right-hand operand.
  /// \param Name Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  LLVM_ABI CmpInst(Type *ty, Instruction::OtherOps op, Predicate pred,
                   Value *LHS, Value *RHS, const Twine &Name = "",
                   InsertPosition InsertBefore = nullptr);

public:
  /// Allocate a CmpInst with space for its two fixed operands.
  /// \param S Allocation size in bytes.
  /// @return Pointer to the allocated storage.
  void *operator new(size_t S) { return User::operator new(S, AllocMarker); }
  /// Deallocate a CmpInst created with the fixed-size allocator.
  /// \param Ptr Pointer returned by the fixed-size \c operator new.
  void operator delete(void *Ptr) { User::operator delete(Ptr, AllocMarker); }

  /// Create a comparison instruction.
  ///
  /// Construct a compare instruction, given the opcode, the predicate and
  /// the two operands. Optionally (if InstBefore is specified) insert the
  /// instruction into a BasicBlock right before the specified instruction.
  /// The specified Instruction is allowed to be a dereferenced end iterator.
  /// \param Op The comparison opcode (ICmp or FCmp).
  /// \param Pred The comparison predicate.
  /// \param S1 The first operand.
  /// \param S2 The second operand.
  /// \param Name Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  /// @return The newly created comparison instruction.
  LLVM_ABI static CmpInst *Create(OtherOps Op, Predicate Pred, Value *S1,
                                  Value *S2, const Twine &Name = "",
                                  InsertPosition InsertBefore = nullptr);

  /// Create a comparison instruction with copied flags.
  ///
  /// Construct a compare instruction, given the opcode, the predicate,
  /// the two operands and the instruction to copy the flags from. Optionally
  /// (if InstBefore is specified) insert the instruction into a BasicBlock
  /// right before the specified instruction. The specified Instruction is
  /// allowed to be a dereferenced end iterator.
  /// \param Op The comparison opcode (ICmp or FCmp).
  /// \param Pred The comparison predicate.
  /// \param S1 The first operand.
  /// \param S2 The second operand.
  /// \param FlagsSource The instruction whose flags are copied.
  /// \param Name Optional name for the instruction.
  /// \param InsertBefore Optional insertion position.
  /// @return The newly created comparison instruction.
  LLVM_ABI static CmpInst *
  CreateWithCopiedFlags(OtherOps Op, Predicate Pred, Value *S1, Value *S2,
                        const Instruction *FlagsSource, const Twine &Name = "",
                        InsertPosition InsertBefore = nullptr);

  /// Return the comparison opcode of this instruction.
  /// @return This instruction's ICmp or FCmp opcode.
  OtherOps getOpcode() const {
    return static_cast<OtherOps>(Instruction::getOpcode());
  }

  /// Return the predicate of this instruction.
  /// @return The current comparison predicate.
  Predicate getPredicate() const { return getSubclassData<PredicateField>(); }

  /// Set the predicate of this instruction.
  /// \param P The new comparison predicate.
  void setPredicate(Predicate P) { setSubclassData<PredicateField>(P); }

  /// Return true if \p P is a floating-point comparison predicate.
  /// \param P The predicate to test.
  /// @return True if \p P is an FCmp predicate.
  static bool isFPPredicate(Predicate P) {
    static_assert(FIRST_FCMP_PREDICATE == 0,
                  "FIRST_FCMP_PREDICATE is required to be 0");
    return P <= LAST_FCMP_PREDICATE;
  }

  /// Return true if \p P is an integer comparison predicate.
  /// \param P The predicate to test.
  /// @return True if \p P is an ICmp predicate.
  static bool isIntPredicate(Predicate P) {
    return P >= FIRST_ICMP_PREDICATE && P <= LAST_ICMP_PREDICATE;
  }

  /// Return the name of comparison predicate \p P.
  /// \param P The predicate to name.
  /// @return The predicate's symbolic name.
  LLVM_ABI static StringRef getPredicateName(Predicate P);

  /// Return true if this instruction uses a floating-point predicate.
  /// @return True if the current predicate is an FCmp predicate.
  bool isFPPredicate() const { return isFPPredicate(getPredicate()); }
  /// Return true if this instruction uses an integer predicate.
  /// @return True if the current predicate is an ICmp predicate.
  bool isIntPredicate() const { return isIntPredicate(getPredicate()); }

  /// Return the inverse of this instruction's predicate.
  ///
  /// For example, EQ -> NE, UGT -> ULE, SLT -> SGE,
  /// OEQ -> UNE, UGT -> OLE, OLT -> UGE, etc.
  /// @return The inverse comparison predicate.
  Predicate getInversePredicate() const {
    return getInversePredicate(getPredicate());
  }

  /// Return the ordered variant of a floating-point predicate.
  ///
  /// For example, UEQ -> OEQ, ULT -> OLT, OEQ -> OEQ.
  /// \param Pred The predicate to convert.
  /// @return The ordered variant of \p Pred.
  static Predicate getOrderedPredicate(Predicate Pred) {
    return static_cast<Predicate>(Pred & FCMP_ORD);
  }

  /// Return the ordered variant of this instruction's predicate.
  /// @return The ordered variant of the current predicate.
  Predicate getOrderedPredicate() const {
    return getOrderedPredicate(getPredicate());
  }

  /// Return the unordered variant of a floating-point predicate.
  ///
  /// For example, OEQ -> UEQ, OLT -> ULT, UEQ -> UEQ.
  /// \param Pred The predicate to convert.
  /// @return The unordered variant of \p Pred.
  static Predicate getUnorderedPredicate(Predicate Pred) {
    return static_cast<Predicate>(Pred | FCMP_UNO);
  }

  /// Return the unordered variant of this instruction's predicate.
  /// @return The unordered variant of the current predicate.
  Predicate getUnorderedPredicate() const {
    return getUnorderedPredicate(getPredicate());
  }

  /// Return the inverse of comparison predicate \p pred.
  ///
  /// For example, EQ -> NE, UGT -> ULE, SLT -> SGE,
  /// OEQ -> UNE, UGT -> OLE, OLT -> UGE, etc.
  /// \param pred The predicate to invert.
  /// @return The inverse of \p pred.
  LLVM_ABI static Predicate getInversePredicate(Predicate pred);

  /// Return the predicate that results from swapping operands.
  ///
  /// For example, EQ->EQ, SLE->SGE, ULT->UGT,
  /// OEQ->OEQ, ULE->UGE, OLT->OGT, etc.
  /// @return The predicate that would produce the same result if the two
  /// operands were exchanged.
  Predicate getSwappedPredicate() const {
    return getSwappedPredicate(getPredicate());
  }

  /// Return the predicate that results from swapping operands.
  ///
  /// For example, EQ->EQ, SLE->SGE, ULT->UGT,
  /// OEQ->OEQ, ULE->UGE, OLT->OGT, etc.
  /// \param pred The predicate to transform.
  /// @return The predicate that would produce the same result if the two
  /// operands were exchanged.
  LLVM_ABI static Predicate getSwappedPredicate(Predicate pred);

  /// Return true if comparison predicate \p predicate is strict.
  /// \param predicate The predicate to test.
  /// @return True if \p predicate is a strict comparison.
  LLVM_ABI static bool isStrictPredicate(Predicate predicate);

  /// Return true if this instruction uses a strict comparison predicate.
  /// @return True if the current predicate is strict.
  bool isStrictPredicate() const { return isStrictPredicate(getPredicate()); }

  /// Return true if comparison predicate \p predicate is non-strict.
  /// \param predicate The predicate to test.
  /// @return True if \p predicate is a non-strict comparison.
  LLVM_ABI static bool isNonStrictPredicate(Predicate predicate);

  /// Return true if this instruction uses a non-strict comparison predicate.
  /// @return True if the current predicate is non-strict.
  bool isNonStrictPredicate() const {
    return isNonStrictPredicate(getPredicate());
  }

  /// Return the strict version of this instruction's predicate.
  ///
  /// For example, SGE -> SGT, SLE -> SLT, ULE -> ULT, UGE -> UGT.
  /// @return The strict variant of the current predicate.
  Predicate getStrictPredicate() const {
    return getStrictPredicate(getPredicate());
  }

  /// Return the strict version of comparison predicate \p pred.
  ///
  /// For example, SGE -> SGT, SLE -> SLT, ULE -> ULT, UGE -> UGT.
  /// If \p pred is not a non-strict comparison predicate, returns \p pred.
  /// \param pred The predicate to convert.
  /// @return The strict variant of \p pred.
  LLVM_ABI static Predicate getStrictPredicate(Predicate pred);

  /// Return the non-strict version of this instruction's predicate.
  ///
  /// For example, SGT -> SGE, SLT -> SLE, ULT -> ULE, UGT -> UGE.
  /// @return The non-strict variant of the current predicate.
  Predicate getNonStrictPredicate() const {
    return getNonStrictPredicate(getPredicate());
  }

  /// Return the non-strict version of comparison predicate \p pred.
  ///
  /// For example, SGT -> SGE, SLT -> SLE, ULT -> ULE, UGT -> UGE.
  /// If \p pred is not a strict comparison predicate, returns \p pred.
  /// \param pred The predicate to convert.
  /// @return The non-strict variant of \p pred.
  LLVM_ABI static Predicate getNonStrictPredicate(Predicate pred);

  /// Return the predicate with flipped strictness.
  ///
  /// For a predicate of the form "is X or equal to 0", returns "is X".
  /// For a predicate of the form "is X", returns "is X or equal to 0".
  /// Does not support other kinds of predicates.
  /// \param pred The predicate to convert.
  /// @return The predicate with strictness flipped, or \p pred if unsupported.
  LLVM_ABI static Predicate getFlippedStrictnessPredicate(Predicate pred);

  /// Return the predicate with flipped strictness.
  ///
  /// For a predicate of the form "is X or equal to 0", returns "is X".
  /// For a predicate of the form "is X", returns "is X or equal to 0".
  /// Does not support other kinds of predicates.
  /// @return The predicate with strictness flipped.
  Predicate getFlippedStrictnessPredicate() const {
    return getFlippedStrictnessPredicate(getPredicate());
  }

  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// @return The operand value at \p i_nocapture.
  inline Value *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Value *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return An iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return A const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return An iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return A const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at a compile-time index.
  /// @return Reference to the operand Use.
  template <int> inline Use &Op();
  /// Return a const reference to the operand at a compile-time index.
  /// @return Const reference to the operand Use.
  template <int> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The number of operands.
  inline unsigned getNumOperands() const;

  /// Swap the operands and adjust the predicate to retain the comparison.
  LLVM_ABI void swapOperands();

  /// Return true if this comparison is commutative.
  /// @return True if operand order may be exchanged without changing the result.
  LLVM_ABI bool isCommutative() const;

  /// Return true if \p pred is an equality or inequality predicate.
  /// \param pred The predicate to test.
  /// @return True if \p pred is EQ, NE, or an FCmp equality predicate.
  LLVM_ABI static bool isEquality(Predicate pred);

  /// Return true if this instruction uses an equality predicate.
  /// @return True if the current predicate tests equality or inequality.
  bool isEquality() const { return isEquality(getPredicate()); }

  /// Return true if operands are interchangeable ignoring provenance.
  ///
  /// Determine if one operand of this compare can always be replaced by the
  /// other operand, ignoring provenance considerations. If \p Invert, check for
  /// equivalence with the inverse predicate.
  /// \param Invert If true, test equivalence with the inverse predicate.
  /// @return True if either operand can replace the other.
  LLVM_ABI bool isEquivalence(bool Invert = false) const;

  /// Return true if \p P is a relational comparison predicate.
  /// \param P The predicate to test.
  /// @return True if \p P is not an equality or inequality predicate.
  static bool isRelational(Predicate P) { return !isEquality(P); }

  /// Return true if this instruction uses a relational predicate.
  /// @return True if the current predicate is not equality or inequality.
  bool isRelational() const { return !isEquality(); }

  /// Return true if this instruction uses a signed integer predicate.
  /// @return True if the current predicate is a signed ICmp predicate.
  bool isSigned() const {
    return isSigned(getPredicate());
  }

  /// Return true if this instruction uses an unsigned integer predicate.
  /// @return True if the current predicate is an unsigned ICmp predicate.
  bool isUnsigned() const {
    return isUnsigned(getPredicate());
  }

  /// Return true if this instruction is true when both operands are equal.
  /// @return True if equal operands make the comparison true.
  bool isTrueWhenEqual() const {
    return isTrueWhenEqual(getPredicate());
  }

  /// Return true if this instruction is false when both operands are equal.
  /// @return True if equal operands make the comparison false.
  bool isFalseWhenEqual() const {
    return isFalseWhenEqual(getPredicate());
  }

  /// Return true if \p Pred is an unsigned integer comparison predicate.
  /// \param Pred The predicate to test.
  /// @return True if \p Pred is ICMP_UGT through ICMP_ULE.
  static bool isUnsigned(Predicate Pred) {
    return Pred >= ICMP_UGT && Pred <= ICMP_ULE;
  }

  /// Return true if \p Pred is a signed integer comparison predicate.
  /// \param Pred The predicate to test.
  /// @return True if \p Pred is ICMP_SGT through ICMP_SLE.
  static bool isSigned(Predicate Pred) {
    return Pred >= ICMP_SGT && Pred <= ICMP_SLE;
  }

  /// Return true if \p predicate is an ordered floating-point predicate.
  /// \param predicate The predicate to test.
  /// @return True if \p predicate requires ordered operands.
  LLVM_ABI static bool isOrdered(Predicate predicate);

  /// Return true if \p predicate is an unordered floating-point predicate.
  /// \param predicate The predicate to test.
  /// @return True if \p predicate allows unordered operands.
  LLVM_ABI static bool isUnordered(Predicate predicate);

  /// Return true if \p predicate is true when both operands are equal.
  /// \param predicate The predicate to test.
  /// @return True if equal operands make \p predicate true.
  LLVM_ABI static bool isTrueWhenEqual(Predicate predicate);

  /// Return true if \p predicate is false when both operands are equal.
  /// \param predicate The predicate to test.
  /// @return True if equal operands make \p predicate false.
  LLVM_ABI static bool isFalseWhenEqual(Predicate predicate);

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I is an ICmp or FCmp instruction.
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::ICmp ||
           I->getOpcode() == Instruction::FCmp;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an ICmp or FCmp instruction.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

  /// Return the result type for an fcmp or icmp on \p opnd_type.
  /// \param opnd_type The operand type being compared.
  /// @return An i1 type, or a vector of i1 for vector operands.
  static Type* makeCmpResultType(Type* opnd_type) {
    if (VectorType* vt = dyn_cast<VectorType>(opnd_type)) {
      return VectorType::get(Type::getInt1Ty(opnd_type->getContext()),
                             vt->getElementCount());
    }
    return Type::getInt1Ty(opnd_type->getContext());
  }

private:
  // Shadow Value::setValueSubclassData with a private forwarding method so that
  // subclasses cannot accidentally use it.
  void setValueSubclassData(unsigned short D) {
    Value::setValueSubclassData(D);
  }
};

// FIXME: these are redundant if CmpInst < BinaryOperator
/// Operand layout traits for CmpInst.
template <>
struct OperandTraits<CmpInst> : public FixedNumOperandTraits<CmpInst, 2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CmpInst, Value)

/// Print comparison predicate \p Pred to \p OS.
/// \param OS The output stream.
/// \param Pred The predicate to print.
/// @return The output stream.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, CmpInst::Predicate Pred);

/// Lightweight view of an operand bundle passed by value.
struct OperandBundleUse {
  /// The bundle's input uses.
  ArrayRef<Use> Inputs;

  /// Construct an empty operand bundle with no tag or inputs.
  OperandBundleUse() = default;
  /// Construct an operand bundle view from a tag and input uses.
  /// \param Tag The interned bundle tag entry.
  /// \param Inputs The bundle's input uses.
  explicit OperandBundleUse(StringMapEntry<uint32_t> *Tag, ArrayRef<Use> Inputs)
      : Inputs(Inputs), Tag(Tag) {}

  /// Return true if input \p Idx has attribute \p A.
  /// \param Idx The zero-based input index.
  /// \param A The attribute kind to test.
  /// @return True if the specified input has attribute \p A.
  bool operandHasAttr(unsigned Idx, Attribute::AttrKind A) const {
    if (isDeoptOperandBundle())
      if (A == Attribute::ReadOnly)
        return Inputs[Idx]->getType()->isPointerTy();

    // Conservative answer:  no operands have any attributes.
    return false;
  }

  /// Return the bundle tag as a string.
  /// @return The tag name of this operand bundle.
  StringRef getTagName() const {
    return Tag->getKey();
  }

  /// Return the bundle tag as an interned integer ID.
  ///
  /// Operand bundle tags are interned by LLVMContextImpl::getOrInsertBundleTag,
  /// and this function returns the unique integer getOrInsertBundleTag
  /// associated the tag of this operand bundle to.
  /// @return The interned tag ID of this operand bundle.
  uint32_t getTagID() const {
    return Tag->getValue();
  }

  /// Return true if this is a "deopt" operand bundle.
  /// @return True if the bundle tag is \c deopt.
  bool isDeoptOperandBundle() const {
    return getTagID() == LLVMContext::OB_deopt;
  }

  /// Return true if this is a "funclet" operand bundle.
  /// @return True if the bundle tag is \c funclet.
  bool isFuncletOperandBundle() const {
    return getTagID() == LLVMContext::OB_funclet;
  }

  /// Return true if this is a "cfguardtarget" operand bundle.
  /// @return True if the bundle tag is \c cfguardtarget.
  bool isCFGuardTargetOperandBundle() const {
    return getTagID() == LLVMContext::OB_cfguardtarget;
  }

private:
  /// Pointer to an entry in LLVMContextImpl::getOrInsertBundleTag.
  StringMapEntry<uint32_t> *Tag;
};

/// Owned operand bundle definition built from values rather than uses.
///
/// Unlike OperandBundleUse, OperandBundleDefT owns the memory it carries, and
/// so it is possible to create and pass around "self-contained" instances of
/// OperandBundleDef and ConstOperandBundleDef.
template <typename InputTy> class OperandBundleDefT {
  std::string Tag;
  std::vector<InputTy> Inputs;

public:
  /// Construct an owned operand bundle from a tag and input vector.
  /// \param Tag The bundle tag name.
  /// \param Inputs The bundle's input values.
  explicit OperandBundleDefT(std::string Tag, std::vector<InputTy> Inputs)
      : Tag(std::move(Tag)), Inputs(std::move(Inputs)) {}
  /// Construct an owned operand bundle from a tag and input array.
  /// \param Tag The bundle tag name.
  /// \param Inputs The bundle's input values.
  explicit OperandBundleDefT(std::string Tag, ArrayRef<InputTy> Inputs)
      : Tag(std::move(Tag)), Inputs(Inputs) {}

  /// Construct an owned operand bundle from an operand bundle view.
  /// \param OBU The operand bundle view to copy.
  explicit OperandBundleDefT(const OperandBundleUse &OBU) {
    Tag = std::string(OBU.getTagName());
    llvm::append_range(Inputs, OBU.Inputs);
  }

  /// Return the bundle's input values.
  /// @return An array reference to the owned input values.
  ArrayRef<InputTy> inputs() const { return Inputs; }

  /// Const iterator over the bundle's input values.
  using input_iterator = typename std::vector<InputTy>::const_iterator;

  /// Return the number of inputs in this bundle.
  /// @return The number of input values.
  size_t input_size() const { return Inputs.size(); }
  /// Return an iterator to the first input value.
  /// @return A const iterator to the first input.
  input_iterator input_begin() const { return Inputs.begin(); }
  /// Return an iterator past the last input value.
  /// @return A const iterator past the last input.
  input_iterator input_end() const { return Inputs.end(); }

  /// Return the tag name of this operand bundle.
  /// @return The bundle tag as a string reference.
  StringRef getTag() const { return Tag; }
};

/// Operand bundle definition with mutable \c Value* inputs.
using OperandBundleDef = OperandBundleDefT<Value *>;
/// Operand bundle definition with const \c Value* inputs.
using ConstOperandBundleDef = OperandBundleDefT<const Value *>;

//===----------------------------------------------------------------------===//
//                               CallBase Class
//===----------------------------------------------------------------------===//

/// Base class for all callable instructions (InvokeInst and CallInst)
/// Holds everything related to calling a function.
///
/// All call-like instructions are required to use a common operand layout:
/// - Zero or more arguments to the call,
/// - Zero or more operand bundles with zero or more operand inputs each
///   bundle,
/// - Zero or more subclass controlled operands
/// - The called function.
///
/// This allows this base class to easily access the called function and the
/// start of the arguments without knowing how many other operands a particular
/// subclass requires. Note that accessing the end of the argument list isn't
/// as cheap as most other operations on the base class.
class CallBase : public Instruction {
protected:
  // The first two bits are reserved by CallInst for fast retrieval,
  /// Bitfield element reserving bits used by \c CallInst subclass data.
  using CallInstReservedField = Bitfield::Element<unsigned, 0, 2>;
  /// Bitfield element storing the call's calling convention.
  using CallingConvField =
      Bitfield::Element<CallingConv::ID, CallInstReservedField::NextBit, 10,
                        CallingConv::MaxID>;
  static_assert(
      Bitfield::areContiguous<CallInstReservedField, CallingConvField>(),
      "Bitfields must be contiguous");

  /// The last operand is the called operand.
  static constexpr int CalledOperandOpEndIdx = -1;

  AttributeList Attrs; ///< parameter attributes for callable
  /// Function type of the call (return type, parameter types, varargs).
  FunctionType *FTy;

  /// Construct a call-like instruction with attributes and function type.
  /// \param A Attribute list for the call.
  /// \param FT Function type of the called function.
  /// \param Args Remaining arguments forwarded to \c Instruction.
  template <class... ArgsTy>
  CallBase(AttributeList const &A, FunctionType *FT, ArgsTy &&... Args)
      : Instruction(std::forward<ArgsTy>(Args)...), Attrs(A), FTy(FT) {}

  /// Inherit \c Instruction constructors for call-like subclasses.
  using Instruction::Instruction;

  /// Return true if this call has an attached descriptor (operand-bundle info).
  /// @return True if operand-bundle descriptor storage is present.
  bool hasDescriptor() const { return Value::HasDescriptor; }

  /// Return how many extra operands this call subclass stores after the callee.
  /// @return The number of subclass-specific trailing operands.
  unsigned getNumSubclassExtraOperands() const {
    switch (getOpcode()) {
    case Instruction::Call:
      return 0;
    case Instruction::Invoke:
      return 2;
    case Instruction::CallBr:
      return getNumSubclassExtraOperandsDynamic();
    }
    llvm_unreachable("Invalid opcode!");
  }

  /// Get the number of extra operands for instructions that don't have a fixed
  /// number of extra operands.
  /// @return The number of subclass-specific trailing operands for \c CallBr.
  LLVM_ABI unsigned getNumSubclassExtraOperandsDynamic() const;

public:
  /// Bring Instruction::getContext into scope for CallBase users.
  using Instruction::getContext;

  /// Create a clone of \p CB with a different set of operand bundles.
  ///
  /// The returned call instruction is identical \p CB in every way except that
  /// the operand bundles for the new instruction are set to the operand bundles
  /// in \p Bundles.
  /// \param CB Call to clone.
  /// \param Bundles Operand bundles for the new instruction.
  /// \param InsertPt Optional insertion point for the new instruction.
  /// @return The cloned call with the specified operand bundles.
  LLVM_ABI static CallBase *Create(CallBase *CB,
                                   ArrayRef<OperandBundleDef> Bundles,
                                   InsertPosition InsertPt = nullptr);

  /// Create a clone of \p CB replacing one operand bundle by tag.
  ///
  /// The returned call instruction is identical \p CB in every way except that
  /// the specified operand bundle has been replaced.
  /// \param CB Call to clone.
  /// \param Bundle Replacement operand bundle (matched by tag).
  /// \param InsertPt Optional insertion point for the new instruction.
  /// @return The cloned call with the replaced operand bundle.
  LLVM_ABI static CallBase *Create(CallBase *CB, OperandBundleDef Bundle,
                                   InsertPosition InsertPt = nullptr);

  /// Create a clone of \p CB with operand bundle \p OB added.
  /// \param CB Call to clone.
  /// \param ID Operand bundle tag identifier.
  /// \param OB Operand bundle to add.
  /// \param InsertPt Optional insertion point for the new instruction.
  /// @return The cloned call with the added operand bundle.
  LLVM_ABI static CallBase *addOperandBundle(CallBase *CB, uint32_t ID,
                                             OperandBundleDef OB,
                                             InsertPosition InsertPt = nullptr);

  /// Create a clone of \p CB with operand bundle \p ID removed.
  /// \param CB Call to clone.
  /// \param ID Tag of the operand bundle to remove.
  /// \param InsertPt Optional insertion point for the new instruction.
  /// @return The cloned call without the specified operand bundle.
  LLVM_ABI static CallBase *
  removeOperandBundle(CallBase *CB, uint32_t ID,
                      InsertPosition InsertPt = nullptr);

  /// Create a clone of \p CB with the operand bundle at \p Offset removed.
  /// \param CB Call to clone.
  /// \param Offset Zero-based index of the operand bundle to remove.
  /// \param InsertPtr Optional insertion point for the new instruction.
  /// @return The cloned call without the operand bundle at \p Offset.
  LLVM_ABI static CallBase *
  removeOperandBundleAt(CallBase *CB, size_t Offset,
                        InsertPosition InsertPtr = nullptr);

  /// Return the convergence control token for this call, if it exists.
  /// @return The convergence control token, or null if none is attached.
  Value *getConvergenceControlToken() const {
    if (auto Bundle = getOperandBundle(llvm::LLVMContext::OB_convergencectrl)) {
      return Bundle->Inputs[0].get();
    }
    return nullptr;
  }

  /// Return true if \p I is a call-like instruction.
  /// \param I Instruction to test.
  /// @return True if \p I is a \c CallInst, \c InvokeInst, or \c CallBrInst.
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Call ||
           I->getOpcode() == Instruction::Invoke ||
           I->getOpcode() == Instruction::CallBr;
  }
  /// Return true if \p V is a call-like instruction.
  /// \param V Value to test.
  /// @return True if \p V is a \c CallBase instance.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

  /// Return the function type of this call.
  /// @return The called function's type (return, parameters, varargs).
  FunctionType *getFunctionType() const { return FTy; }

  /// Change the function type of this call and update the result type.
  /// \param FTy New function type for the call.
  void mutateFunctionType(FunctionType *FTy) {
    Value::mutateType(FTy->getReturnType());
    this->FTy = FTy;
  }

  // Transparently provide more efficient getOperand methods.
  public:
  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture Zero-based operand index.
  /// @return The operand value at that index.
  inline Value *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture Zero-based operand index.
  /// \param Val_nocapture New operand value.
  inline void setOperand(unsigned i_nocapture, Value *Val_nocapture);
  /// Return an iterator to the first operand \c Use.
  /// @return Iterator to the first operand use.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand \c Use.
  /// @return Const iterator to the first operand use.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand \c Use.
  /// @return Iterator past the last operand use.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand \c Use.
  /// @return Const iterator past the last operand use.
  inline const_op_iterator op_end() const;

protected:
  /// Access operand \c Use by compile-time index (negative indexes from the end).
  /// @return Mutable reference to the operand use at the compile-time index.
  template <int> inline Use &Op();
  /// Access operand \c Use by compile-time index (const overload).
  /// @return Const reference to the operand use at the compile-time index.
  template <int> inline const Use &Op() const;

public:
  /// Return the total number of operands (arguments, called value, and extras).
  /// @return Total operand count for this call-like instruction.
  inline unsigned getNumOperands() const;

  /// Return an iterator to the first call/invoke data operand.
  ///
  /// Data operands include arguments and operand bundles, excluding the callee
  /// and subclass-specific trailing operands.
  /// @return Iterator to the first data operand use.
  User::op_iterator data_operands_begin() { return op_begin(); }
  /// Return a const iterator to the first call/invoke data operand.
  /// @return Const iterator to the first data operand use.
  User::const_op_iterator data_operands_begin() const {
    return const_cast<CallBase *>(this)->data_operands_begin();
  }
  /// Return an iterator past the last call/invoke data operand.
  /// @return Iterator past the last data operand use.
  User::op_iterator data_operands_end() {
    // Walk from the end of the operands over the called operand and any
    // subclass operands.
    return op_end() - getNumSubclassExtraOperands() - 1;
  }
  /// Return a const iterator past the last data operand (args and bundles).
  /// @return Const iterator past the last data operand use.
  User::const_op_iterator data_operands_end() const {
    return const_cast<CallBase *>(this)->data_operands_end();
  }
  /// Return a range over argument and operand-bundle data operands.
  /// @return Iterator range of data operand uses.
  iterator_range<User::op_iterator> data_ops() {
    return make_range(data_operands_begin(), data_operands_end());
  }
  /// Iterate over argument and operand-bundle data operands.
  /// @return Const iterator range of data operand uses.
  iterator_range<User::const_op_iterator> data_ops() const {
    return make_range(data_operands_begin(), data_operands_end());
  }
  /// Return true if this call has no data operands.
  /// @return True if the data operand range is empty.
  bool data_operands_empty() const {
    return data_operands_end() == data_operands_begin();
  }
  /// Return the number of data operands (arguments plus bundle inputs).
  /// @return Count of data operands.
  unsigned data_operands_size() const {
    return std::distance(data_operands_begin(), data_operands_end());
  }

  /// Return true if \p U is a data operand use of this call.
  /// \param U Use belonging to this instruction.
  /// @return True if \p U refers to a data operand.
  bool isDataOperand(const Use *U) const {
    assert(this == U->getUser() &&
           "Only valid to query with a use of this instruction!");
    return data_operands_begin() <= U && U < data_operands_end();
  }
  /// Return true if the use from iterator \p UI is a data operand.
  /// \param UI User iterator whose use is tested.
  /// @return True if \p UI refers to a data operand of this call.
  bool isDataOperand(Value::const_user_iterator UI) const {
    return isDataOperand(&UI.getUse());
  }

  /// Return the data-operand index for the use from iterator \p UI.
  ///
  /// Iterator must actually correspond to a data operand.
  /// \param UI User iterator whose use is a data operand.
  /// @return Zero-based data operand index.
  unsigned getDataOperandNo(Value::const_user_iterator UI) const {
    return getDataOperandNo(&UI.getUse());
  }

  /// Return the data-operand index for use \p U.
  /// \param U Use of a data operand of this call.
  /// @return Zero-based data operand index.
  unsigned getDataOperandNo(const Use *U) const {
    assert(isDataOperand(U) && "Data operand # out of range!");
    return U - data_operands_begin();
  }

  /// Return the iterator pointing to the beginning of the argument list.
  /// @return Iterator to the first call argument operand.
  User::op_iterator arg_begin() { return op_begin(); }
  /// Return a const iterator pointing to the beginning of the argument list.
  /// @return Const iterator to the first call argument operand.
  User::const_op_iterator arg_begin() const {
    return const_cast<CallBase *>(this)->arg_begin();
  }

  /// Return the iterator pointing to the end of the argument list.
  /// @return Iterator past the last call argument operand.
  User::op_iterator arg_end() {
    // From the end of the data operands, walk backwards past the bundle
    // operands.
    return data_operands_end() - getNumTotalBundleOperands();
  }
  /// Return a const iterator pointing to the end of the argument list.
  /// @return Const iterator past the last call argument operand.
  User::const_op_iterator arg_end() const {
    return const_cast<CallBase *>(this)->arg_end();
  }

  /// Return a range over call argument operands.
  /// @return Iterator range of argument operand uses.
  iterator_range<User::op_iterator> args() {
    return make_range(arg_begin(), arg_end());
  }
  /// Return a const range over call argument operands.
  /// @return Const iterator range of argument operand uses.
  iterator_range<User::const_op_iterator> args() const {
    return make_range(arg_begin(), arg_end());
  }
  /// Return true if this call has no arguments.
  /// @return True if the argument range is empty.
  bool arg_empty() const { return arg_end() == arg_begin(); }
  /// Return the number of call arguments.
  /// @return Count of argument operands.
  unsigned arg_size() const { return arg_end() - arg_begin(); }

  /// Return the \p i-th call argument operand.
  /// \param i Zero-based argument index.
  /// @return The argument value at index \p i.
  Value *getArgOperand(unsigned i) const {
    assert(i < arg_size() && "Out of bounds!");
    return getOperand(i);
  }

  /// Set the \p i-th call argument operand.
  /// \param i Zero-based argument index.
  /// \param v New argument value.
  void setArgOperand(unsigned i, Value *v) {
    assert(i < arg_size() && "Out of bounds!");
    setOperand(i, v);
  }

  /// Return the \c Use for call argument \p i.
  /// \param i Zero-based argument index.
  /// @return Mutable reference to the argument's use.
  Use &getArgOperandUse(unsigned i) {
    assert(i < arg_size() && "Out of bounds!");
    return User::getOperandUse(i);
  }
  /// Return the \c Use for call argument \p i.
  /// \param i Zero-based argument index.
  /// @return Const reference to the argument's use.
  const Use &getArgOperandUse(unsigned i) const {
    assert(i < arg_size() && "Out of bounds!");
    return User::getOperandUse(i);
  }

  /// Return true if \p U is an argument operand use of this call.
  /// \param U Use belonging to this instruction.
  /// @return True if \p U refers to a call argument.
  bool isArgOperand(const Use *U) const {
    assert(this == U->getUser() &&
           "Only valid to query with a use of this instruction!");
    return arg_begin() <= U && U < arg_end();
  }
  /// Return true if the use at \p UI is an argument operand of this call.
  /// \param UI User iterator for a use of this instruction.
  /// @return True if the use refers to a call argument.
  bool isArgOperand(Value::const_user_iterator UI) const {
    return isArgOperand(&UI.getUse());
  }

  /// Return the argument index for use \p U.
  ///
  /// \p U must refer to an argument operand.
  /// \param U Use belonging to an argument of this call.
  /// @return Zero-based argument index.
  unsigned getArgOperandNo(const Use *U) const {
    assert(isArgOperand(U) && "Arg operand # out of range!");
    return U - arg_begin();
  }

  /// Return the argument index for the use at \p UI.
  ///
  /// \p UI must refer to an argument operand.
  /// \param UI User iterator for an argument use.
  /// @return Zero-based argument index.
  unsigned getArgOperandNo(Value::const_user_iterator UI) const {
    return getArgOperandNo(&UI.getUse());
  }

  /// Return true if \p V is passed as an argument to the called function.
  /// \param V Value to search for among call arguments.
  /// @return True if \p V appears in the argument list.
  bool hasArgument(const Value *V) const {
    return llvm::is_contained(args(), V);
  }

  /// Return the called function or value operand.
  /// @return The callee operand (direct function, pointer, or inline asm).
  Value *getCalledOperand() const { return Op<CalledOperandOpEndIdx>(); }

  /// Return the \c Use for the called operand.
  /// @return Const reference to the callee operand's use.
  const Use &getCalledOperandUse() const { return Op<CalledOperandOpEndIdx>(); }
  /// Return the \c Use for the called operand.
  /// @return Mutable reference to the callee operand's use.
  Use &getCalledOperandUse() { return Op<CalledOperandOpEndIdx>(); }

  /// Return the directly called \c Function, if known.
  ///
  /// Returns the function called, or null if this is an indirect function
  /// invocation or the function signature does not match the call signature, or
  /// the call target is an alias.
  /// @return The called \c Function, or null if not a direct matching call.
  Function *getCalledFunction() const {
    if (auto *F = dyn_cast_or_null<Function>(getCalledOperand()))
      if (F->getFunctionType() == getFunctionType())
        return F;
    return nullptr;
  }

  /// Return true if the callsite is an indirect call.
  /// @return True if the callee is not a directly referenced \c Function.
  LLVM_ABI bool isIndirectCall() const;

  /// Return true if \p UI points to the callee operand's use.
  /// \param UI User iterator for a use of this instruction.
  /// @return True if the use is the callee operand.
  bool isCallee(Value::const_user_iterator UI) const {
    return isCallee(&UI.getUse());
  }

  /// Return true if \p U is the callee operand's use.
  /// \param U Use belonging to this instruction.
  /// @return True if \p U is the callee operand use.
  bool isCallee(const Use *U) const { return &getCalledOperandUse() == U; }

  /// Return the function containing this call.
  /// @return The parent function of this call-like instruction.
  LLVM_ABI Function *getCaller();
  /// Return the function containing this call.
  /// @return The parent function of this call-like instruction.
  const Function *getCaller() const {
    return const_cast<CallBase *>(this)->getCaller();
  }

  /// Return true if this call site must be tail call optimized.
  ///
  /// Only a \c CallInst can be marked musttail.
  /// @return True if the call has the musttail attribute.
  LLVM_ABI bool isMustTailCall() const;

  /// Return true if this call site is marked as a tail call.
  /// @return True if the call has the tail attribute.
  LLVM_ABI bool isTailCall() const;

  /// Return the intrinsic ID of the called function.
  ///
  /// Returns \c Intrinsic::not_intrinsic if the callee is not an intrinsic, or
  /// if this is an indirect call.
  /// @return Intrinsic identifier for the callee, if known.
  LLVM_ABI Intrinsic::ID getIntrinsicID() const;

  /// Set the called function or value operand.
  /// \param V New callee operand value.
  void setCalledOperand(Value *V) { Op<CalledOperandOpEndIdx>() = V; }

  /// Set the called function and update the function type.
  /// \param Fn Function to call.
  void setCalledFunction(Function *Fn) {
    setCalledFunction(Fn->getFunctionType(), Fn);
  }

  /// Set the called function and update the function type.
  /// \param Fn Callee wrapper providing type and value.
  void setCalledFunction(FunctionCallee Fn) {
    setCalledFunction(Fn.getFunctionType(), Fn.getCallee());
  }

  /// Set the called function with an explicit function type.
  /// \param FTy Function type of the call.
  /// \param Fn Callee operand value.
  void setCalledFunction(FunctionType *FTy, Value *Fn) {
    this->FTy = FTy;
    // This function doesn't mutate the return type, only the function
    // type. Seems broken, but I'm just gonna stick an assert in for now.
    assert(getType() == FTy->getReturnType());
    setCalledOperand(Fn);
  }

  /// Return the calling convention of this call.
  /// @return Calling convention identifier.
  CallingConv::ID getCallingConv() const {
    return getSubclassData<CallingConvField>();
  }

  /// Set the calling convention for this call.
  /// \param CC Calling convention to store on the instruction.
  void setCallingConv(CallingConv::ID CC) {
    setSubclassData<CallingConvField>(CC);
  }

  /// Return true if this call targets inline assembly.
  /// @return True if the callee operand is an \c InlineAsm object.
  bool isInlineAsm() const { return isa<InlineAsm>(getCalledOperand()); }

  /// \name Attribute API
  ///
  /// These methods access and modify attributes on this call (including
  /// looking through to the attributes on the called function when necessary).
  ///@{

  /// Return the attributes for this call.
  /// @return Attribute list attached to this call.
  AttributeList getAttributes() const { return Attrs; }

  /// Set the attributes for this call.
  /// \param A New attribute list for the call.
  void setAttributes(AttributeList A) { Attrs = A; }

  /// Return the return attributes for this call.
  /// @return Attribute set for the return value.
  AttributeSet getRetAttributes() const {
    return getAttributes().getRetAttrs();
  }

  /// Return the param attributes for argument \p ArgNo.
  /// \param ArgNo Zero-based argument index.
  /// @return Attribute set for the specified parameter.
  AttributeSet getParamAttributes(unsigned ArgNo) const {
    return getAttributes().getParamAttrs(ArgNo);
  }

  /// Intersect this call's attributes with \p Other's attributes.
  ///
  /// Try to intersect the attributes from 'this' CallBase and the
  /// 'Other' CallBase. Sets the intersected attributes to 'this' and
  /// return true if successful. Doesn't modify 'this' and returns
  /// false if unsuccessful.
  /// \param Other Other call whose attributes are intersected with this one.
  /// @return True if intersection succeeded and this call was updated.
  bool tryIntersectAttributes(const CallBase *Other) {
    if (this == Other)
      return true;
    AttributeList AL = getAttributes();
    AttributeList ALOther = Other->getAttributes();
    auto Intersected = AL.intersectWith(getContext(), ALOther);
    if (!Intersected)
      return false;
    setAttributes(*Intersected);
    return true;
  }

  /// Return true if this call has function attribute \p Kind.
  ///
  /// Determine whether this call has the given attribute. If it does not
  /// then determine if the called function has the attribute, but only if
  /// the attribute is allowed for the call.
  /// \param Kind Function attribute kind to test.
  /// @return True if the attribute is present on the call or allowed callee.
  bool hasFnAttr(Attribute::AttrKind Kind) const {
    assert(Kind != Attribute::NoBuiltin &&
           "Use CallBase::isNoBuiltin() to check for Attribute::NoBuiltin");
    return hasFnAttrImpl(Kind);
  }

  /// Return true if this call has function attribute \p Kind.
  ///
  /// Determine whether this call has the given attribute. If it does not
  /// then determine if the called function has the attribute, but only if
  /// the attribute is allowed for the call.
  /// \param Kind Function attribute name to test.
  /// @return True if the attribute is present on the call or allowed callee.
  bool hasFnAttr(StringRef Kind) const { return hasFnAttrImpl(Kind); }

  // TODO: remove non-AtIndex versions of these methods.
  /// Add attribute \p Kind at attribute index \p i.
  /// \param i Attribute index (return, function, or parameter slot).
  /// \param Kind Attribute kind to add.
  void addAttributeAtIndex(unsigned i, Attribute::AttrKind Kind) {
    Attrs = Attrs.addAttributeAtIndex(getContext(), i, Kind);
  }

  /// Add attribute \p Attr at attribute index \p i.
  /// \param i Attribute index (return, function, or parameter slot).
  /// \param Attr Attribute to add.
  void addAttributeAtIndex(unsigned i, Attribute Attr) {
    Attrs = Attrs.addAttributeAtIndex(getContext(), i, Attr);
  }

  /// Add function attribute \p Kind to this call.
  /// \param Kind Function attribute kind to add.
  void addFnAttr(Attribute::AttrKind Kind) {
    Attrs = Attrs.addFnAttribute(getContext(), Kind);
  }

  /// Add function attribute \p Attr to this call.
  /// \param Attr Function attribute to add.
  void addFnAttr(Attribute Attr) {
    Attrs = Attrs.addFnAttribute(getContext(), Attr);
  }

  /// Add return attribute \p Kind to this call.
  /// \param Kind Return attribute kind to add.
  void addRetAttr(Attribute::AttrKind Kind) {
    Attrs = Attrs.addRetAttribute(getContext(), Kind);
  }

  /// Add return attribute \p Attr to this call.
  /// \param Attr Return attribute to add.
  void addRetAttr(Attribute Attr) {
    Attrs = Attrs.addRetAttribute(getContext(), Attr);
  }

  /// Add return attributes from builder \p B to this call.
  /// \param B Attributes to add to the return value.
  void addRetAttrs(const AttrBuilder &B) {
    Attrs = Attrs.addRetAttributes(getContext(), B);
  }

  /// Add parameter attribute \p Kind to argument \p ArgNo.
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Parameter attribute kind to add.
  void addParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attrs = Attrs.addParamAttribute(getContext(), ArgNo, Kind);
  }

  /// Add parameter attribute \p Attr to argument \p ArgNo.
  /// \param ArgNo Zero-based argument index.
  /// \param Attr Parameter attribute to add.
  void addParamAttr(unsigned ArgNo, Attribute Attr) {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attrs = Attrs.addParamAttribute(getContext(), ArgNo, Attr);
  }

  /// Add parameter attributes from builder \p B to argument \p ArgNo.
  /// \param ArgNo Zero-based argument index.
  /// \param B Attributes to add to the parameter.
  void addParamAttrs(unsigned ArgNo, const AttrBuilder &B) {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attrs = Attrs.addParamAttributes(getContext(), ArgNo, B);
  }

  /// Remove attribute \p Kind at attribute index \p i.
  /// \param i Attribute index (return, function, or parameter slot).
  /// \param Kind Attribute kind to remove.
  void removeAttributeAtIndex(unsigned i, Attribute::AttrKind Kind) {
    Attrs = Attrs.removeAttributeAtIndex(getContext(), i, Kind);
  }

  /// Remove attribute \p Kind at attribute index \p i.
  /// \param i Attribute index (return, function, or parameter slot).
  /// \param Kind Attribute name to remove.
  void removeAttributeAtIndex(unsigned i, StringRef Kind) {
    Attrs = Attrs.removeAttributeAtIndex(getContext(), i, Kind);
  }

  /// Remove function attributes matching \p AttrsToRemove.
  /// \param AttrsToRemove Mask of function attributes to remove.
  void removeFnAttrs(const AttributeMask &AttrsToRemove) {
    Attrs = Attrs.removeFnAttributes(getContext(), AttrsToRemove);
  }

  /// Remove function attribute \p Kind from this call.
  /// \param Kind Function attribute kind to remove.
  void removeFnAttr(Attribute::AttrKind Kind) {
    Attrs = Attrs.removeFnAttribute(getContext(), Kind);
  }

  /// Remove function attribute \p Kind from this call.
  /// \param Kind Function attribute name to remove.
  void removeFnAttr(StringRef Kind) {
    Attrs = Attrs.removeFnAttribute(getContext(), Kind);
  }

  /// Remove return attribute \p Kind from this call.
  /// \param Kind Return attribute kind to remove.
  void removeRetAttr(Attribute::AttrKind Kind) {
    Attrs = Attrs.removeRetAttribute(getContext(), Kind);
  }

  /// Remove return attributes matching \p AttrsToRemove.
  /// \param AttrsToRemove Mask of return attributes to remove.
  void removeRetAttrs(const AttributeMask &AttrsToRemove) {
    Attrs = Attrs.removeRetAttributes(getContext(), AttrsToRemove);
  }

  /// Remove parameter attribute \p Kind from argument \p ArgNo.
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Parameter attribute kind to remove.
  void removeParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attrs = Attrs.removeParamAttribute(getContext(), ArgNo, Kind);
  }

  /// Remove parameter attribute \p Kind from argument \p ArgNo.
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Parameter attribute name to remove.
  void removeParamAttr(unsigned ArgNo, StringRef Kind) {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attrs = Attrs.removeParamAttribute(getContext(), ArgNo, Kind);
  }

  /// Remove parameter attributes matching \p AttrsToRemove from \p ArgNo.
  /// \param ArgNo Zero-based argument index.
  /// \param AttrsToRemove Mask of parameter attributes to remove.
  void removeParamAttrs(unsigned ArgNo, const AttributeMask &AttrsToRemove) {
    Attrs = Attrs.removeParamAttributes(getContext(), ArgNo, AttrsToRemove);
  }

  /// Add a dereferenceable-bytes attribute to parameter \p i.
  /// \param i Zero-based argument index.
  /// \param Bytes Number of dereferenceable bytes.
  void addDereferenceableParamAttr(unsigned i, uint64_t Bytes) {
    Attrs = Attrs.addDereferenceableParamAttr(getContext(), i, Bytes);
  }

  /// Add a dereferenceable-bytes attribute to the return value.
  /// \param Bytes Number of dereferenceable bytes.
  void addDereferenceableRetAttr(uint64_t Bytes) {
    Attrs = Attrs.addDereferenceableRetAttr(getContext(), Bytes);
  }

  /// Add a range attribute to the return value.
  /// \param CR Constant range for the return value.
  void addRangeRetAttr(const ConstantRange &CR) {
    Attrs = Attrs.addRangeRetAttr(getContext(), CR);
  }

  /// Return true if the return value has attribute \p Kind.
  /// \param Kind Return attribute kind to test.
  /// @return True if the return has \p Kind.
  bool hasRetAttr(Attribute::AttrKind Kind) const {
    return hasRetAttrImpl(Kind);
  }
  /// Return true if the return value has attribute \p Kind.
  /// \param Kind Return attribute name to test.
  /// @return True if the return has \p Kind.
  bool hasRetAttr(StringRef Kind) const { return hasRetAttrImpl(Kind); }

  /// Return return attribute \p Kind, including callee lookup if needed.
  /// \param Kind Return attribute kind to query.
  /// @return The attribute, or an invalid attribute if not present.
  Attribute getRetAttr(Attribute::AttrKind Kind) const {
    Attribute RetAttr = Attrs.getRetAttr(Kind);
    if (RetAttr.isValid())
      return RetAttr;

    // Look at the callee, if available.
    if (const Function *F = getCalledFunction())
      return F->getRetAttribute(Kind);
    return Attribute();
  }

  /// Return true if argument \p ArgNo has attribute \p Kind.
  ///
  /// Checks call attributes and, when allowed, attributes on the callee.
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Parameter attribute kind to test.
  /// @return True if the argument has \p Kind.
  LLVM_ABI bool paramHasAttr(unsigned ArgNo, Attribute::AttrKind Kind) const;

  /// Return true if argument \p ArgNo is known non-null.
  ///
  /// Return true if this argument has the nonnull attribute on either the
  /// CallBase instruction or the called function. Also returns true if at least
  /// one byte is known to be dereferenceable and the pointer is in
  /// addrspace(0). If \p AllowUndefOrPoison is true, respect the semantics of
  /// nonnull attribute and return true even if the argument can be undef or
  /// poison.
  /// \param ArgNo Zero-based argument index.
  /// \param AllowUndefOrPoison If true, treat undef or poison as nonnull.
  /// @return True if the argument is known non-null under the chosen rules.
  LLVM_ABI bool paramHasNonNullAttr(unsigned ArgNo,
                                    bool AllowUndefOrPoison) const;

  /// Return attribute \p Kind at attribute index \p i.
  /// \param i Attribute index (return, function, or parameter slot).
  /// \param Kind Attribute kind to query.
  /// @return The attribute at \p i, or an invalid attribute if absent.
  Attribute getAttributeAtIndex(unsigned i, Attribute::AttrKind Kind) const {
    return getAttributes().getAttributeAtIndex(i, Kind);
  }

  /// Return attribute \p Kind at attribute index \p i.
  /// \param i Attribute index (return, function, or parameter slot).
  /// \param Kind Attribute name to query.
  /// @return The attribute at \p i, or an invalid attribute if absent.
  Attribute getAttributeAtIndex(unsigned i, StringRef Kind) const {
    return getAttributes().getAttributeAtIndex(i, Kind);
  }

  /// Return function attribute \p Kind, including callee lookup if needed.
  /// \param Kind Function attribute name to query.
  /// @return The attribute, or an invalid attribute if not present.
  Attribute getFnAttr(StringRef Kind) const {
    Attribute Attr = getAttributes().getFnAttr(Kind);
    if (Attr.isValid())
      return Attr;
    return getFnAttrOnCalledFunction(Kind);
  }

  /// Return function attribute \p Kind, including callee lookup if needed.
  /// \param Kind Function attribute kind to query.
  /// @return The attribute, or an invalid attribute if not present.
  Attribute getFnAttr(Attribute::AttrKind Kind) const {
    Attribute A = getAttributes().getFnAttr(Kind);
    if (A.isValid())
      return A;
    return getFnAttrOnCalledFunction(Kind);
  }

  /// Return parameter attribute \p Kind for argument \p ArgNo.
  ///
  /// Checks call attributes and, when allowed, attributes on the callee.
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Parameter attribute kind to query.
  /// @return The attribute, or an invalid attribute if not present.
  Attribute getParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) const {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attribute A = getAttributes().getParamAttr(ArgNo, Kind);
    if (A.isValid())
      return A;
    return getParamAttrOnCalledFunction(ArgNo, Kind);
  }

  /// Return parameter attribute \p Kind for argument \p ArgNo.
  ///
  /// Checks call attributes and, when allowed, attributes on the callee.
  /// \param ArgNo Zero-based argument index.
  /// \param Kind Parameter attribute name to query.
  /// @return The attribute, or an invalid attribute if not present.
  Attribute getParamAttr(unsigned ArgNo, StringRef Kind) const {
    assert(ArgNo < arg_size() && "Out of bounds");
    Attribute A = getAttributes().getParamAttr(ArgNo, Kind);
    if (A.isValid())
      return A;
    return getParamAttrOnCalledFunction(ArgNo, Kind);
  }

  /// Return true if data operand \p i has implied attribute \p Kind.
  ///
  /// Data operands include call arguments and values used in operand bundles,
  /// but does not include the callee operand.
  ///
  /// The index \p i is interpreted as
  ///
  ///  \p i in [0, arg_size)  -> argument number (\p i)
  ///  \p i in [arg_size, data_operand_size) -> bundle operand at index
  ///     (\p i) in the operand list.
  /// \param i Zero-based data operand index.
  /// \param Kind Attribute kind to test (direct or bundle-implied).
  /// @return True if the data operand has \p Kind.
  bool dataOperandHasImpliedAttr(unsigned i, Attribute::AttrKind Kind) const {
    // Note that we have to add one because `i` isn't zero-indexed.
    assert(i < arg_size() + getNumTotalBundleOperands() &&
           "Data operand index out of bounds!");

    // The attribute A can either be directly specified, if the operand in
    // question is a call argument; or be indirectly implied by the kind of its
    // containing operand bundle, if the operand is a bundle operand.

    if (i < arg_size())
      return paramHasAttr(i, Kind);

    assert(hasOperandBundles() && i >= getBundleOperandsStartIndex() &&
           "Must be either a call argument or an operand bundle!");
    return bundleOperandHasAttr(i, Kind);
  }

  /// Return which pointer components data operand \p OpNo may capture.
  /// \param OpNo Zero-based data operand index.
  /// @return Capture information for the operand.
  LLVM_ABI CaptureInfo getCaptureInfo(unsigned OpNo) const;

  /// Return true if data operand \p OpNo does not capture memory.
  ///
  /// FIXME: Once this API is no longer duplicated in `CallSite`, rename this to
  /// better indicate that this may return a conservative answer.
  /// \param OpNo Zero-based data operand index.
  /// @return True if the operand is known not to capture.
  bool doesNotCapture(unsigned OpNo) const {
    return capturesNothing(getCaptureInfo(OpNo));
  }

  /// Return true if some argument has extra return capture components.
  ///
  /// Returns whether the call has an argument that has an attribute like
  /// captures(ret: address, provenance), where the return capture components
  /// are not a subset of the other capture components.
  /// @return True if such an argument exists.
  LLVM_ABI bool hasArgumentWithAdditionalReturnCaptureComponents() const;

  /// Return true if argument \p ArgNo is passed by value.
  /// \param ArgNo Zero-based argument index.
  /// @return True if the argument has the byval attribute.
  bool isByValArgument(unsigned ArgNo) const {
    return paramHasAttr(ArgNo, Attribute::ByVal);
  }

  /// Return true if argument \p ArgNo is passed in an alloca.
  /// \param ArgNo Zero-based argument index.
  /// @return True if the argument has the inalloca attribute.
  bool isInAllocaArgument(unsigned ArgNo) const {
    return paramHasAttr(ArgNo, Attribute::InAlloca);
  }

  /// Return true if argument \p ArgNo is passed by value on the stack.
  ///
  /// True for byval, inalloca, or preallocated argument passing.
  /// \param ArgNo Zero-based argument index.
  /// @return True if the pointee is passed by value.
  bool isPassPointeeByValueArgument(unsigned ArgNo) const {
    return paramHasAttr(ArgNo, Attribute::ByVal) ||
           paramHasAttr(ArgNo, Attribute::InAlloca) ||
           paramHasAttr(ArgNo, Attribute::Preallocated);
  }

  /// Return true if passing undef to argument \p ArgNo is undefined behavior.
  ///
  /// Determine whether passing undef to this argument is undefined behavior.
  /// If passing undef to this argument is UB, passing poison is UB as well
  /// because poison is more undefined than undef.
  /// \param ArgNo Zero-based argument index.
  /// @return True if undef or poison at this argument is UB.
  bool isPassingUndefUB(unsigned ArgNo) const {
    return paramHasAttr(ArgNo, Attribute::NoUndef) ||
           // dereferenceable implies noundef.
           paramHasAttr(ArgNo, Attribute::Dereferenceable) ||
           // dereferenceable implies noundef, and null is a well-defined value.
           paramHasAttr(ArgNo, Attribute::DereferenceableOrNull);
  }

  /// Return true if the last argument has the inalloca attribute.
  ///
  /// Determine if there are is an inalloca argument. Only the last argument can
  /// have the inalloca attribute.
  /// @return True if the final argument is inalloca.
  bool hasInAllocaArgument() const {
    return !arg_empty() && paramHasAttr(arg_size() - 1, Attribute::InAlloca);
  }

  // FIXME: Once this API is no longer duplicated in `CallSite`, rename this to
  // better indicate that this may return a conservative answer.
  /// Return true if data operand \p OpNo is known not to access memory.
  /// \param OpNo Zero-based data operand index.
  /// @return True if the operand has readnone semantics.
  bool doesNotAccessMemory(unsigned OpNo) const {
    return dataOperandHasImpliedAttr(OpNo, Attribute::ReadNone);
  }

  // FIXME: Once this API is no longer duplicated in `CallSite`, rename this to
  // better indicate that this may return a conservative answer.
  /// Return true if data operand \p OpNo only reads memory.
  ///
  /// If the argument is passed byval, the callee does not have access to the
  /// original pointer and thus cannot write to it.
  /// \param OpNo Zero-based data operand index.
  /// @return True if the operand is read-only or readnone.
  bool onlyReadsMemory(unsigned OpNo) const {
    // If the argument is passed byval, the callee does not have access to the
    // original pointer and thus cannot write to it.
    if (OpNo < arg_size() && isByValArgument(OpNo))
      return true;

    return dataOperandHasImpliedAttr(OpNo, Attribute::ReadOnly) ||
           dataOperandHasImpliedAttr(OpNo, Attribute::ReadNone);
  }

  // FIXME: Once this API is no longer duplicated in `CallSite`, rename this to
  // better indicate that this may return a conservative answer.
  /// Return true if data operand \p OpNo only writes memory.
  /// \param OpNo Zero-based data operand index.
  /// @return True if the operand is write-only or readnone.
  bool onlyWritesMemory(unsigned OpNo) const {
    return dataOperandHasImpliedAttr(OpNo, Attribute::WriteOnly) ||
           dataOperandHasImpliedAttr(OpNo, Attribute::ReadNone);
  }

  /// Return the alignment of the return value, if known.
  /// @return Return alignment from the call or callee, or empty if unknown.
  MaybeAlign getRetAlign() const {
    if (auto Align = Attrs.getRetAlignment())
      return Align;
    if (const Function *F = getCalledFunction())
      return F->getAttributes().getRetAlignment();
    return std::nullopt;
  }

  /// Return the alignment for argument \p ArgNo, if known.
  /// \param ArgNo Zero-based argument index.
  /// @return Parameter alignment, or empty if unknown.
  MaybeAlign getParamAlign(unsigned ArgNo) const {
    return Attrs.getParamAlignment(ArgNo);
  }

  /// Return the stack alignment for argument \p ArgNo, if specified.
  /// \param ArgNo Zero-based argument index.
  /// @return Requested stack alignment, or empty if none.
  MaybeAlign getParamStackAlign(unsigned ArgNo) const {
    return Attrs.getParamStackAlignment(ArgNo);
  }

  /// Return the byref type for argument \p ArgNo.
  /// \param ArgNo Zero-based argument index.
  /// @return Byref type from the call or callee, or null if absent.
  Type *getParamByRefType(unsigned ArgNo) const {
    if (auto *Ty = Attrs.getParamByRefType(ArgNo))
      return Ty;
    if (const Function *F = getCalledFunction())
      return F->getAttributes().getParamByRefType(ArgNo);
    return nullptr;
  }

  /// Return the byval type for argument \p ArgNo.
  /// \param ArgNo Zero-based argument index.
  /// @return Byval type from the call or callee, or null if absent.
  Type *getParamByValType(unsigned ArgNo) const {
    if (auto *Ty = Attrs.getParamByValType(ArgNo))
      return Ty;
    if (const Function *F = getCalledFunction())
      return F->getAttributes().getParamByValType(ArgNo);
    return nullptr;
  }

  /// Return the preallocated type for argument \p ArgNo.
  /// \param ArgNo Zero-based argument index.
  /// @return Preallocated type from the call or callee, or null if absent.
  Type *getParamPreallocatedType(unsigned ArgNo) const {
    if (auto *Ty = Attrs.getParamPreallocatedType(ArgNo))
      return Ty;
    if (const Function *F = getCalledFunction())
      return F->getAttributes().getParamPreallocatedType(ArgNo);
    return nullptr;
  }

  /// Return the inalloca type for argument \p ArgNo.
  /// \param ArgNo Zero-based argument index.
  /// @return Inalloca type from the call or callee, or null if absent.
  Type *getParamInAllocaType(unsigned ArgNo) const {
    if (auto *Ty = Attrs.getParamInAllocaType(ArgNo))
      return Ty;
    if (const Function *F = getCalledFunction())
      return F->getAttributes().getParamInAllocaType(ArgNo);
    return nullptr;
  }

  /// Return the sret type for argument \p ArgNo.
  /// \param ArgNo Zero-based argument index.
  /// @return Struct-ret type from the call or callee, or null if absent.
  Type *getParamStructRetType(unsigned ArgNo) const {
    if (auto *Ty = Attrs.getParamStructRetType(ArgNo))
      return Ty;
    if (const Function *F = getCalledFunction())
      return F->getAttributes().getParamStructRetType(ArgNo);
    return nullptr;
  }

  /// Return the elementtype for argument \p ArgNo.
  ///
  /// Note that elementtype() can only be applied to call arguments, not
  /// function declaration parameters.
  /// \param ArgNo Zero-based argument index.
  /// @return Element type from the call argument attribute, or null.
  Type *getParamElementType(unsigned ArgNo) const {
    return Attrs.getParamElementType(ArgNo);
  }

  /// Return dereferenceable bytes for the return value.
  ///
  /// Extract the number of dereferenceable bytes for a call or
  /// parameter (0=unknown).
  /// @return Known dereferenceable byte count for the return.
  uint64_t getRetDereferenceableBytes() const {
    uint64_t Bytes = Attrs.getRetDereferenceableBytes();
    if (const Function *F = getCalledFunction())
      Bytes = std::max(Bytes, F->getAttributes().getRetDereferenceableBytes());
    return Bytes;
  }

  /// Return dereferenceable bytes for argument \p i.
  ///
  /// Extract the number of dereferenceable bytes for a call or
  /// parameter (0=unknown).
  /// \param i Zero-based argument index.
  /// @return Known dereferenceable byte count for the argument.
  uint64_t getParamDereferenceableBytes(unsigned i) const {
    return Attrs.getParamDereferenceableBytes(i);
  }

  /// Return dereferenceable_or_null bytes for the return value.
  ///
  /// Extract the number of dereferenceable_or_null bytes for a call
  /// (0=unknown).
  /// @return Known dereferenceable-or-null byte count for the return.
  uint64_t getRetDereferenceableOrNullBytes() const {
    uint64_t Bytes = Attrs.getRetDereferenceableOrNullBytes();
    if (const Function *F = getCalledFunction()) {
      Bytes = std::max(Bytes,
                       F->getAttributes().getRetDereferenceableOrNullBytes());
    }

    return Bytes;
  }

  /// Return dereferenceable_or_null bytes for argument \p i.
  ///
  /// Extract the number of dereferenceable_or_null bytes for a
  /// parameter (0=unknown).
  /// \param i Zero-based argument index.
  /// @return Known dereferenceable-or-null byte count for the argument.
  uint64_t getParamDereferenceableOrNullBytes(unsigned i) const {
    return Attrs.getParamDereferenceableOrNullBytes(i);
  }

  /// Return disallowed FP classes for the return value.
  /// @return FP-class test mask for the return, or \c fcNone if none.
  LLVM_ABI FPClassTest getRetNoFPClass() const;

  /// Return disallowed FP classes for argument \p i.
  /// \param i Zero-based argument index.
  /// @return FP-class test mask for the parameter, or \c fcNone if none.
  LLVM_ABI FPClassTest getParamNoFPClass(unsigned i) const;

  /// Return the constant range of the return value, if any.
  ///
  /// If this return value has a range attribute, return the value range of the
  /// argument. Otherwise, std::nullopt is returned.
  /// @return Optional return-value range from attributes.
  LLVM_ABI std::optional<ConstantRange> getRange() const;

  /// Return true if the return value is known non-null.
  ///
  /// Return true if the return value is known to be not null.
  /// This may be because it has the nonnull attribute, or because at least
  /// one byte is dereferenceable and the pointer is in addrspace(0).
  /// @return True if the return cannot be null.
  LLVM_ABI bool isReturnNonNull() const;

  /// Return true if the return value has the noalias attribute.
  /// @return True if the return is marked noalias.
  bool returnDoesNotAlias() const {
    return Attrs.hasRetAttr(Attribute::NoAlias);
  }

  /// Return the argument marked with the returned attribute.
  ///
  /// If one of the arguments has the 'returned' attribute, returns its
  /// operand value. Otherwise, return nullptr.
  /// @return The returned argument value, or null.
  Value *getReturnedArgOperand() const {
    return getArgOperandWithAttribute(Attribute::Returned);
  }

  /// Return the first argument with attribute \p Kind.
  ///
  /// If one of the arguments has the specified attribute, returns its
  /// operand value. Otherwise, return nullptr.
  /// \param Kind Parameter attribute kind to search for.
  /// @return Matching argument value, or null.
  LLVM_ABI Value *getArgOperandWithAttribute(Attribute::AttrKind Kind) const;

  /// Return true if this call must not be treated as a library builtin.
  ///
  /// Return true if the call should not be treated as a call to a
  /// builtin.
  /// @return True if nobuiltin is set and builtin is not.
  bool isNoBuiltin() const {
    return hasFnAttrImpl(Attribute::NoBuiltin) &&
           !hasFnAttrImpl(Attribute::Builtin);
  }

  /// Return true if this call requires strict floating-point semantics.
  /// @return True if the strictfp attribute is present.
  bool isStrictFP() const { return hasFnAttr(Attribute::StrictFP); }

  /// Return true if this call must not be inlined.
  /// @return True if the noinline attribute is present.
  bool isNoInline() const { return hasFnAttr(Attribute::NoInline); }
  /// Mark this call as must-not-inline.
  void setIsNoInline() { addFnAttr(Attribute::NoInline); }

  /// Return the memory effects of this call.
  /// @return Combined read/write/arg/inaccessible memory effects.
  LLVM_ABI MemoryEffects getMemoryEffects() const;
  /// Set the memory effects of this call.
  /// \param ME Memory effects to store on the call.
  LLVM_ABI void setMemoryEffects(MemoryEffects ME);

  /// Return true if this call does not access memory.
  /// @return True if the call has readnone semantics.
  LLVM_ABI bool doesNotAccessMemory() const;
  /// Mark the call as not accessing memory (sets MemoryEffects::none).
  LLVM_ABI void setDoesNotAccessMemory();

  /// Return true if this call only reads memory (or accesses none).
  /// @return True if the call is readonly or readnone.
  LLVM_ABI bool onlyReadsMemory() const;
  /// Mark the call as only reading memory.
  LLVM_ABI void setOnlyReadsMemory();

  /// Return true if this call only writes memory (or accesses none).
  /// @return True if the call is writeonly or readnone.
  LLVM_ABI bool onlyWritesMemory() const;
  /// Mark the call as only writing memory.
  LLVM_ABI void setOnlyWritesMemory();

  /// Return true if this call may only access argument-pointee memory.
  ///
  /// Determine if the call can access memmory only using pointers based
  /// on its arguments.
  /// @return True if memory access is limited to argument memory.
  LLVM_ABI bool onlyAccessesArgMemory() const;
  /// Mark the call as only accessing argument-pointee memory.
  LLVM_ABI void setOnlyAccessesArgMemory();

  /// Return true if this call may only access inaccessible memory.
  ///
  /// Determine if the function may only access memory that is
  /// inaccessible from the IR.
  /// @return True if only inaccessible memory may be accessed.
  LLVM_ABI bool onlyAccessesInaccessibleMemory() const;
  /// Mark the call as only accessing inaccessible memory.
  LLVM_ABI void setOnlyAccessesInaccessibleMemory();

  /// Return true if this call may only access arg or inaccessible memory.
  ///
  /// Determine if the function may only access memory that is
  /// either inaccessible from the IR or pointed to by its arguments.
  /// @return True if memory access is limited accordingly.
  LLVM_ABI bool onlyAccessesInaccessibleMemOrArgMem() const;
  /// Mark the call as only accessing arg or inaccessible memory.
  LLVM_ABI void setOnlyAccessesInaccessibleMemOrArgMem();

  /// Return true if this call cannot return to its caller.
  /// @return True if the noreturn attribute is present.
  bool doesNotReturn() const { return hasFnAttr(Attribute::NoReturn); }
  /// Mark this call as noreturn.
  void setDoesNotReturn() { addFnAttr(Attribute::NoReturn); }

  /// Return true if this call skips indirect branch tracking.
  /// @return True if the nocfcheck attribute is present.
  bool doesNoCfCheck() const { return hasFnAttr(Attribute::NoCfCheck); }

  /// Return true if this call cannot unwind.
  /// @return True if the nounwind attribute is present.
  bool doesNotThrow() const { return hasFnAttr(Attribute::NoUnwind); }
  /// Mark this call as nounwind.
  void setDoesNotThrow() { addFnAttr(Attribute::NoUnwind); }

  /// Return true if this invoke cannot be duplicated.
  /// @return True if the noduplicate attribute is present.
  bool cannotDuplicate() const { return hasFnAttr(Attribute::NoDuplicate); }
  /// Mark that the invoke cannot be duplicated by the optimizer.
  void setCannotDuplicate() { addFnAttr(Attribute::NoDuplicate); }

  /// Return true if this call cannot be tail merged.
  /// @return True if the nomerge attribute is present.
  bool cannotMerge() const { return hasFnAttr(Attribute::NoMerge); }
  /// Mark that the call cannot be merged with other calls by the optimizer.
  void setCannotMerge() { addFnAttr(Attribute::NoMerge); }

  /// Return true if this call is convergent.
  /// @return True if the convergent attribute is present.
  bool isConvergent() const { return hasFnAttr(Attribute::Convergent); }
  /// Mark this call as convergent.
  void setConvergent() { addFnAttr(Attribute::Convergent); }
  /// Remove the convergent attribute from this call.
  void setNotConvergent() { removeFnAttr(Attribute::Convergent); }

  /// Return true if the call returns a structure via the first pointer arg.
  ///
  /// Determine if the call returns a structure through first
  /// pointer argument.
  /// @return True if argument 0 has the sret attribute.
  bool hasStructRetAttr() const {
    if (arg_empty())
      return false;

    // Be friendly and also check the callee.
    return paramHasAttr(0, Attribute::StructRet);
  }

  /// Return true if any call argument has the byval attribute.
  ///
  /// Determine if any call argument is an aggregate passed by value.
  /// @return True if some parameter carries byval.
  bool hasByValArgument() const {
    return Attrs.hasAttrSomewhere(Attribute::ByVal);
  }

  ///@}
  // End of attribute API.

  /// \name Operand Bundle API
  ///
  /// This group of methods provides the API to access and manipulate operand
  /// bundles on this call.
  /// @{

  /// Return the number of operand bundles on this call.
  /// @return The operand bundle count.
  unsigned getNumOperandBundles() const {
    return std::distance(bundle_op_info_begin(), bundle_op_info_end());
  }

  /// Return whether this call has any operand bundles.
  /// @return True if at least one operand bundle is present.
  bool hasOperandBundles() const { return getNumOperandBundles() != 0; }

  /// Return the index of the first bundle operand in the Use array.
  ///
  /// Requires that the call has operand bundles.
  /// @return The zero-based index of the first bundle operand.
  unsigned getBundleOperandsStartIndex() const {
    assert(hasOperandBundles() && "Don't call otherwise!");
    return bundle_op_info_begin()->Begin;
  }

  /// Return the index one past the last bundle operand in the Use array.
  ///
  /// Requires that the call has operand bundles.
  /// @return The zero-based index past the last bundle operand.
  unsigned getBundleOperandsEndIndex() const {
    assert(hasOperandBundles() && "Don't call otherwise!");
    return bundle_op_info_end()[-1].End;
  }

  /// Return whether the operand at index \p Idx is a bundle operand.
  /// \param Idx The zero-based operand index.
  /// @return True if \p Idx refers to an operand bundle input.
  bool isBundleOperand(unsigned Idx) const {
    return hasOperandBundles() && Idx >= getBundleOperandsStartIndex() &&
           Idx < getBundleOperandsEndIndex();
  }

  /// Return whether the operand at index \p Idx belongs to a bundle with tag
  /// ID \p ID.
  /// \param ID The operand bundle tag ID to match.
  /// \param Idx The zero-based operand index.
  /// @return True if \p Idx is a bundle operand of the given type.
  bool isOperandBundleOfType(uint32_t ID, unsigned Idx) const {
    return isBundleOperand(Idx) &&
           getOperandBundleForOperand(Idx).getTagID() == ID;
  }

  /// Return whether \p U is a bundle operand of this call.
  /// \param U A use belonging to this instruction.
  /// @return True if \p U refers to an operand bundle input.
  bool isBundleOperand(const Use *U) const {
    assert(this == U->getUser() &&
           "Only valid to query with a use of this instruction!");
    return hasOperandBundles() && isBundleOperand(U - op_begin());
  }

  /// Return whether the use at \p UI is a bundle operand of this call.
  /// \param UI A value use iterator for this instruction.
  /// @return True if \p UI refers to an operand bundle input.
  bool isBundleOperand(Value::const_user_iterator UI) const {
    return isBundleOperand(&UI.getUse());
  }

  /// Return the total number of bundle input operands on this call.
  ///
  /// This counts individual bundle inputs, not the number of bundles.
  /// @return The total number of operands used by all operand bundles.
  unsigned getNumTotalBundleOperands() const {
    if (!hasOperandBundles())
      return 0;

    unsigned Begin = getBundleOperandsStartIndex();
    unsigned End = getBundleOperandsEndIndex();

    assert(Begin <= End && "Should be!");
    return End - Begin;
  }

  /// Return the operand bundle at a specific index.
  /// \param Index The zero-based operand bundle index.
  /// @return The operand bundle at \p Index.
  OperandBundleUse getOperandBundleAt(unsigned Index) const {
    assert(Index < getNumOperandBundles() && "Index out of bounds!");
    return operandBundleFromBundleOpInfo(*(bundle_op_info_begin() + Index));
  }

  /// Return the number of operand bundles with tag name \p Name.
  /// \param Name The operand bundle tag name to count.
  /// @return How many bundles on this call use \p Name.
  unsigned countOperandBundlesOfType(StringRef Name) const {
    unsigned Count = 0;
    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i)
      if (getOperandBundleAt(i).getTagName() == Name)
        Count++;

    return Count;
  }

  /// Return the number of operand bundles with tag ID \p ID.
  /// \param ID The operand bundle tag ID to count.
  /// @return How many bundles on this call use \p ID.
  unsigned countOperandBundlesOfType(uint32_t ID) const {
    unsigned Count = 0;
    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i)
      if (getOperandBundleAt(i).getTagID() == ID)
        Count++;

    return Count;
  }

  /// Return an operand bundle by tag name, if present.
  ///
  /// It is an error to call this for operand bundle types that may have
  /// multiple instances of them on the same instruction.
  /// \param Name The operand bundle tag name to look up.
  /// @return The matching bundle, or an empty optional if none is present.
  std::optional<OperandBundleUse> getOperandBundle(StringRef Name) const {
    assert(countOperandBundlesOfType(Name) < 2 && "Precondition violated!");

    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i) {
      OperandBundleUse U = getOperandBundleAt(i);
      if (U.getTagName() == Name)
        return U;
    }

    return std::nullopt;
  }

  /// Return an operand bundle by tag ID, if present.
  ///
  /// It is an error to call this for operand bundle types that may have
  /// multiple instances of them on the same instruction.
  /// \param ID The operand bundle tag ID to look up.
  /// @return The matching bundle, or an empty optional if none is present.
  std::optional<OperandBundleUse> getOperandBundle(uint32_t ID) const {
    assert(countOperandBundlesOfType(ID) < 2 && "Precondition violated!");

    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i) {
      OperandBundleUse U = getOperandBundleAt(i);
      if (U.getTagID() == ID)
        return U;
    }

    return std::nullopt;
  }

  /// Copy operand bundles on this call into \p Defs.
  ///
  /// This function copies the OperandBundleUse instances associated with this
  /// call to a vector of OperandBundleDefs. OperandBundleUses and
  /// OperandBundleDefs are non-trivially different representations of operand
  /// bundles (see documentation above).
  /// \param Defs Output vector populated with OperandBundleDef copies.
  LLVM_ABI void
  getOperandBundlesAsDefs(SmallVectorImpl<OperandBundleDef> &Defs) const;

  /// Return the operand bundle containing the operand at index \p OpIdx.
  ///
  /// It is an error to call this with an \p OpIdx that does not correspond to
  /// a bundle operand.
  /// \param OpIdx The zero-based operand index.
  /// @return The operand bundle that owns \p OpIdx.
  OperandBundleUse getOperandBundleForOperand(unsigned OpIdx) const {
    return operandBundleFromBundleOpInfo(getBundleOpInfoForOperand(OpIdx));
  }

  /// Return whether any operand bundle on this call may read from the heap.
  /// @return True if a reading operand bundle is present.
  LLVM_ABI bool hasReadingOperandBundles() const;

  /// Return whether any operand bundle on this call may write to the heap.
  /// @return True if a clobbering operand bundle is present.
  LLVM_ABI bool hasClobberingOperandBundles() const;

  /// Return whether the bundle operand at index \p OpIdx has attribute \p A.
  /// \param OpIdx The zero-based operand index of a bundle input.
  /// \param A The attribute kind to test.
  /// @return True if the bundle operand has \p A.
  bool bundleOperandHasAttr(unsigned OpIdx,  Attribute::AttrKind A) const {
    auto &BOI = getBundleOpInfoForOperand(OpIdx);
    auto OBU = operandBundleFromBundleOpInfo(BOI);
    return OBU.operandHasAttr(OpIdx - BOI.Begin, A);
  }

  /// Return whether this call has the same operand bundle layout as \p Other.
  ///
  /// Two calls match when they have the same sequence of bundle tags and the
  /// same number of inputs in each bundle.
  /// \param Other The call whose operand bundle schema is compared.
  /// @return True if the bundle schemas are identical.
  bool hasIdenticalOperandBundleSchema(const CallBase &Other) const {
    if (getNumOperandBundles() != Other.getNumOperandBundles())
      return false;

    return std::equal(bundle_op_info_begin(), bundle_op_info_end(),
                      Other.bundle_op_info_begin());
  }

  /// Return whether this call has operand bundles whose tags are not in \p IDs.
  /// \param IDs Allowed operand bundle tag IDs.
  /// @return True if any bundle tag is not listed in \p IDs.
  bool hasOperandBundlesOtherThan(ArrayRef<uint32_t> IDs) const {
    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i) {
      uint32_t ID = getOperandBundleAt(i).getTagID();
      if (!is_contained(IDs, ID))
        return true;
    }
    return false;
  }

  /// Descriptor for one contiguous operand bundle in the Use list.
  ///
  /// Used to keep track of an operand bundle. See the main comment on
  /// CallBase above.
  struct BundleOpInfo {
    /// The operand bundle tag, interned by
    /// LLVMContextImpl::getOrInsertBundleTag.
    StringMapEntry<uint32_t> *Tag;

    /// The index in the Use& vector where operands for this operand
    /// bundle starts.
    uint32_t Begin;

    /// The index in the Use& vector where operands for this operand
    /// bundle ends.
    uint32_t End;

    /// Return whether two bundle descriptors refer to the same bundle slice.
    /// \param Other The descriptor to compare against.
    /// @return True if tag and begin/end indices match.
    bool operator==(const BundleOpInfo &Other) const {
      return Tag == Other.Tag && Begin == Other.Begin && End == Other.End;
    }
  };

  /// Map a \p BOI descriptor to an OperandBundleUse view.
  /// \param BOI The bundle descriptor to convert.
  /// @return An OperandBundleUse covering the same inputs.
  OperandBundleUse
  operandBundleFromBundleOpInfo(const BundleOpInfo &BOI) const {
    const auto *begin = op_begin();
    ArrayRef<Use> Inputs(begin + BOI.Begin, begin + BOI.End);
    return OperandBundleUse(BOI.Tag, Inputs);
  }

  /// Iterator over mutable BundleOpInfo descriptors for this call.
  using bundle_op_iterator = BundleOpInfo *;
  /// Iterator over const BundleOpInfo descriptors for this call.
  using const_bundle_op_iterator = const BundleOpInfo *;

  /// Return an iterator to the first BundleOpInfo descriptor for this call.
  ///
  /// CallBase uses the descriptor area co-allocated with the host User
  /// to store some meta information about which operands are "normal" operands,
  /// and which ones belong to some operand bundle.
  ///
  /// The layout of an operand bundle user is
  ///
  ///          +-----------uint32_t End-------------------------------------+
  ///          |                                                            |
  ///          |  +--------uint32_t Begin--------------------+              |
  ///          |  |                                          |              |
  ///          ^  ^                                          v              v
  ///  |------|------|----|----|----|----|----|---------|----|---------|----|-----
  ///  | BOI0 | BOI1 | .. | DU | U0 | U1 | .. | BOI0_U0 | .. | BOI1_U0 | .. | Un
  ///  |------|------|----|----|----|----|----|---------|----|---------|----|-----
  ///   v  v                                  ^              ^
  ///   |  |                                  |              |
  ///   |  +--------uint32_t Begin------------+              |
  ///   |                                                    |
  ///   +-----------uint32_t End-----------------------------+
  ///
  ///
  /// BOI0, BOI1 ... are descriptions of operand bundles in this User's use
  /// list. These descriptions are installed and managed by this class, and
  /// they're all instances of CallBase::BundleOpInfo.
  ///
  /// DU is an additional descriptor installed by User's 'operator new' to keep
  /// track of the 'BOI0 ... BOIN' co-allocation.  OperandBundleUser does not
  /// access or modify DU in any way, it's an implementation detail private to
  /// User.
  ///
  /// The regular Use& vector for the User starts at U0.  The operand bundle
  /// uses are part of the Use& vector, just like normal uses.  In the diagram
  /// above, the operand bundle uses start at BOI0_U0.  Each instance of
  /// BundleOpInfo has information about a contiguous set of uses constituting
  /// an operand bundle, and the total set of operand bundle uses themselves
  /// form a contiguous set of uses (i.e. there are no gaps between uses
  /// corresponding to individual operand bundles).
  ///
  /// This class does not know the location of the set of operand bundle uses
  /// within the use list -- that is decided by the User using this class via
  /// the BeginIdx argument in populateBundleOperandInfos.
  ///
  /// Currently operand bundle users with hung-off operands are not supported.
  /// @return Iterator to the first BundleOpInfo, or null if none are present.
  bundle_op_iterator bundle_op_info_begin() {
    if (!hasDescriptor())
      return nullptr;

    uint8_t *BytesBegin = getDescriptor().begin();
    return reinterpret_cast<bundle_op_iterator>(BytesBegin);
  }

  /// Return a const iterator to the first BundleOpInfo descriptor.
  /// @return Const iterator to the first BundleOpInfo, or null if none are
  /// present.
  const_bundle_op_iterator bundle_op_info_begin() const {
    auto *NonConstThis = const_cast<CallBase *>(this);
    return NonConstThis->bundle_op_info_begin();
  }

  /// Return an iterator past the last BundleOpInfo descriptor.
  /// @return Iterator past the last BundleOpInfo, or null if none are present.
  bundle_op_iterator bundle_op_info_end() {
    if (!hasDescriptor())
      return nullptr;

    uint8_t *BytesEnd = getDescriptor().end();
    return reinterpret_cast<bundle_op_iterator>(BytesEnd);
  }

  /// Return a const iterator past the last BundleOpInfo descriptor.
  /// @return Const iterator past the last BundleOpInfo, or null if none are
  /// present.
  const_bundle_op_iterator bundle_op_info_end() const {
    auto *NonConstThis = const_cast<CallBase *>(this);
    return NonConstThis->bundle_op_info_end();
  }

  /// Return the range of mutable BundleOpInfo descriptors for this call.
  /// @return Range covering [\p bundle_op_info_begin(), \p bundle_op_info_end()).
  iterator_range<bundle_op_iterator> bundle_op_infos() {
    return make_range(bundle_op_info_begin(), bundle_op_info_end());
  }

  /// Return the range of const BundleOpInfo descriptors for this call.
  /// @return Const range covering [\p bundle_op_info_begin(),
  /// \p bundle_op_info_end()).
  iterator_range<const_bundle_op_iterator> bundle_op_infos() const {
    return make_range(bundle_op_info_begin(), bundle_op_info_end());
  }

  /// Iterate over OperandBundleUse views for each bundle on this call.
  /// @return A mapped range of operand bundles.
  auto operand_bundles() const {
    return map_range(bundle_op_infos(), [this](BundleOpInfo BOI) {
      return operandBundleFromBundleOpInfo(BOI);
    });
  }

  /// Populate bundle descriptors and operand uses from \p Bundles.
  ///
  /// Each \p OperandBundleDef instance is tracked by a BundleOpInfo instance
  /// allocated in this instruction's descriptor.
  /// \param Bundles Operand bundle definitions to install.
  /// \param BeginIndex Index in the Use list where bundle operands start.
  /// @return Iterator past the last installed bundle operand use.
  LLVM_ABI op_iterator populateBundleOperandInfos(
      ArrayRef<OperandBundleDef> Bundles, const unsigned BeginIndex);

  /// Return whether this call has a deopt-state operand bundle.
  /// @return True if the deopt bundle is present.
  bool hasDeoptState() const {
    return getOperandBundle(LLVMContext::OB_deopt).has_value();
  }

public:
  /// Return the BundleOpInfo descriptor for bundle operand index \p OpIdx.
  ///
  /// It is an error to call this with an \p OpIdx that does not correspond to
  /// a bundle operand.
  /// \param OpIdx The zero-based operand index.
  /// @return The BundleOpInfo owning \p OpIdx.
  LLVM_ABI BundleOpInfo &getBundleOpInfoForOperand(unsigned OpIdx);
  /// Return the const BundleOpInfo descriptor for bundle operand index \p OpIdx.
  /// \param OpIdx The zero-based operand index.
  /// @return The BundleOpInfo owning \p OpIdx.
  const BundleOpInfo &getBundleOpInfoForOperand(unsigned OpIdx) const {
    return const_cast<CallBase *>(this)->getBundleOpInfoForOperand(OpIdx);
  }

protected:
  /// Return the total number of bundle input values in \p Bundles.
  /// \param Bundles Operand bundle definitions to measure.
  /// @return The sum of input counts across all bundles.
  static unsigned CountBundleInputs(ArrayRef<OperandBundleDef> Bundles) {
    unsigned Total = 0;
    for (const auto &B : Bundles)
      Total += B.input_size();
    return Total;
  }

  /// @}
  // End of operand bundle API.

private:
  LLVM_ABI bool hasFnAttrOnCalledFunction(Attribute::AttrKind Kind) const;
  LLVM_ABI bool hasFnAttrOnCalledFunction(StringRef Kind) const;

  template <typename AttrKind> bool hasFnAttrImpl(AttrKind Kind) const {
    if (Attrs.hasFnAttr(Kind))
      return true;

    return hasFnAttrOnCalledFunction(Kind);
  }
  template <typename AK> Attribute getFnAttrOnCalledFunction(AK Kind) const;
  template <typename AK>
  Attribute getParamAttrOnCalledFunction(unsigned ArgNo, AK Kind) const;

  /// Determine whether the return value has the given attribute. Supports
  /// Attribute::AttrKind and StringRef as \p AttrKind types.
  template <typename AttrKind> bool hasRetAttrImpl(AttrKind Kind) const {
    if (Attrs.hasRetAttr(Kind))
      return true;

    // Look at the callee, if available.
    if (const Function *F = getCalledFunction())
      return F->getAttributes().hasRetAttr(Kind);
    return false;
  }
};

/// Operand layout traits for CallBase.
template <>
struct OperandTraits<CallBase> : public VariadicOperandTraits<CallBase> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CallBase, Value)

//===----------------------------------------------------------------------===//
//                           FuncletPadInst Class
//===----------------------------------------------------------------------===//

/// Base class for funclet pad instructions used in Windows EH.
class FuncletPadInst : public Instruction {
private:
  FuncletPadInst(const FuncletPadInst &CPI, AllocInfo AllocInfo);

  LLVM_ABI explicit FuncletPadInst(Instruction::FuncletPadOps Op,
                                   Value *ParentPad, ArrayRef<Value *> Args,
                                   AllocInfo AllocInfo, const Twine &NameStr,
                                   InsertPosition InsertBefore);

  void init(Value *ParentPad, ArrayRef<Value *> Args, const Twine &NameStr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;
  friend class CatchPadInst;
  friend class CleanupPadInst;

  /// Clone this funclet pad instruction.
  /// @return A copy of this instruction.
  LLVM_ABI FuncletPadInst *cloneImpl() const;

public:
  /// Return operand at index \p i_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// @return The operand value at that index.
  inline Value *getOperand(unsigned i_nocapture) const;
  /// Set operand at index \p i_nocapture to \p Val_nocapture.
  /// \param i_nocapture The zero-based operand index.
  /// \param Val_nocapture The new operand value.
  inline void setOperand(unsigned i_nocapture, Value *Val_nocapture);
  /// Return an iterator to the first operand.
  /// @return Iterator to the first operand.
  inline op_iterator op_begin();
  /// Return a const iterator to the first operand.
  /// @return Const iterator to the first operand.
  inline const_op_iterator op_begin() const;
  /// Return an iterator past the last operand.
  /// @return Iterator past the last operand.
  inline op_iterator op_end();
  /// Return a const iterator past the last operand.
  /// @return Const iterator past the last operand.
  inline const_op_iterator op_end() const;
protected:
  /// Return a reference to the operand at a compile-time index.
  /// @return Reference to the operand Use.
  template <int> inline Use &Op();
  /// Return a const reference to the operand at a compile-time index.
  /// @return Const reference to the operand Use.
  template <int> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Return the number of funclet pad arguments.
  ///
  /// Excludes the parent pad operand stored at the end of the operand list.
  /// @return The argument count.
  unsigned arg_size() const { return getNumOperands() - 1; }

  /// Return the outer EH pad this funclet is nested within.
  ///
  /// Note: This returns the associated CatchSwitchInst if this FuncletPadInst
  /// is a CatchPadInst.
  /// @return The parent funclet pad token.
  Value *getParentPad() const { return Op<-1>(); }
  /// Set the outer EH pad that contains this funclet.
  /// \param ParentPad The parent funclet pad token.
  void setParentPad(Value *ParentPad) {
    assert(ParentPad);
    Op<-1>() = ParentPad;
  }

  /// Return the i-th funclet pad argument operand.
  /// \param i The zero-based argument index.
  /// @return The argument value at \p i.
  Value *getArgOperand(unsigned i) const { return getOperand(i); }
  /// Set the i-th funclet pad argument operand.
  /// \param i The zero-based argument index.
  /// \param v The new argument value.
  void setArgOperand(unsigned i, Value *v) { setOperand(i, v); }

  /// Iterate over funclet pad argument operands.
  /// @return Range covering all argument operands.
  op_range arg_operands() { return op_range(op_begin(), op_end() - 1); }

  /// Iterate over funclet pad argument operands.
  /// @return Const range covering all argument operands.
  const_op_range arg_operands() const {
    return const_op_range(op_begin(), op_end() - 1);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I is a funclet pad instruction.
  static bool classof(const Instruction *I) { return I->isFuncletPad(); }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a FuncletPadInst.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

/// Operand layout traits for FuncletPadInst.
template <>
struct OperandTraits<FuncletPadInst>
    : public VariadicOperandTraits<FuncletPadInst> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(FuncletPadInst, Value)

} // end namespace llvm

#endif // LLVM_IR_INSTRTYPES_H
