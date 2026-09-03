//===-- llvm/Operator.h - Operator utility subclass -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines various classes for working with Instructions and
// ConstantExprs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_OPERATOR_H
#define LLVM_IR_OPERATOR_H

#include "llvm/ADT/MapVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/GEPNoWrapFlags.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include <cstddef>
#include <optional>

namespace llvm {

/// This is a utility class that provides an abstraction for the common
/// functionality between Instructions and ConstantExprs.
class Operator : public User {
public:
  // The Operator class is intended to be used as a utility, and is never itself
  // instantiated.
  /// Deleted default constructor; Operator is never instantiated.
  Operator() = delete;
  /// Deleted destructor; Operator is never instantiated.
  ~Operator() = delete;

  /// Deleted; Operator objects are never allocated.
  /// \param s Unused allocation size.
  void *operator new(size_t s) = delete;

  /// Return the opcode for this Instruction or ConstantExpr.
  /// @return The opcode of this Instruction or ConstantExpr.
  unsigned getOpcode() const {
    if (const Instruction *I = dyn_cast<Instruction>(this))
      return I->getOpcode();
    return cast<ConstantExpr>(this)->getOpcode();
  }

  /// If V is an Instruction or ConstantExpr, return its opcode.
  /// Otherwise return UserOp1.
  /// \param V The value whose opcode to query.
  /// @return The opcode of \p V, or UserOp1 if it is neither.
  static unsigned getOpcode(const Value *V) {
    if (const Instruction *I = dyn_cast<Instruction>(V))
      return I->getOpcode();
    if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(V))
      return CE->getOpcode();
    return Instruction::UserOp1;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I An instruction (always an Operator).
  /// @return True (every Instruction is an Operator).
  static bool classof(const Instruction *I) { return true; }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param CE A constant expression (always an Operator).
  /// @return True (every ConstantExpr is an Operator).
  static bool classof(const ConstantExpr *CE) { return true; }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an Instruction or ConstantExpr.
  static bool classof(const Value *V) {
    return isa<Instruction>(V) || isa<ConstantExpr>(V);
  }

  /// Return true if this operator has flags which may cause this operator
  /// to evaluate to poison despite having non-poison inputs.
  /// @return True if this operator has poison-generating flags.
  LLVM_ABI bool hasPoisonGeneratingFlags() const;

  /// Return true if this operator has poison-generating flags,
  /// return attributes or metadata. The latter two is only possible for
  /// instructions.
  /// @return True if this operator has poison-generating flags, attributes,
  /// or metadata.
  LLVM_ABI bool hasPoisonGeneratingAnnotations() const;
};

/// Utility class for integer operators that may exhibit overflow.
///
/// Covers Add, Sub, Mul, and Shl. It does not include SDiv, despite that
/// operator having the potential for overflow.
class OverflowingBinaryOperator : public Operator {
public:
  /// No-wrap flag bits stored in SubclassOptionalData.
  enum {
    AnyWrap = 0,                  ///< No no-wrap guarantees.
    NoUnsignedWrap = (1 << 0),    ///< Operation is known not to unsigned-wrap.
    NoSignedWrap = (1 << 1)       ///< Operation is known not to signed-wrap.
  };

private:
  friend class Instruction;
  friend class ConstantExpr;

  void setHasNoUnsignedWrap(bool B) {
    assert(isa<Instruction>(this) && "cannot modify ConstantExpr");
    SubclassOptionalData =
      (SubclassOptionalData & ~NoUnsignedWrap) | (B * NoUnsignedWrap);
  }
  void setHasNoSignedWrap(bool B) {
    assert(isa<Instruction>(this) && "cannot modify ConstantExpr");
    SubclassOptionalData =
      (SubclassOptionalData & ~NoSignedWrap) | (B * NoSignedWrap);
  }

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
  /// Return a reference to the operand at compile-time index \p Idx.
  /// @return Reference to the operand Use.
  template <int Idx> inline Use &Op();
  /// Return a const reference to the operand at compile-time index \p Idx.
  /// @return Const reference to the operand Use.
  template <int Idx> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Test whether this operation is known to never
  /// undergo unsigned overflow, aka the nuw property.
  /// @return True if the operation has the nuw property.
  bool hasNoUnsignedWrap() const {
    return SubclassOptionalData & NoUnsignedWrap;
  }

  /// Test whether this operation is known to never
  /// undergo signed overflow, aka the nsw property.
  /// @return True if the operation has the nsw property.
  bool hasNoSignedWrap() const {
    return (SubclassOptionalData & NoSignedWrap) != 0;
  }

  /// Returns the no-wrap kind of the operation.
  /// @return A bitmask of NoUnsignedWrap and/or NoSignedWrap flags.
  unsigned getNoWrapKind() const {
    unsigned NoWrapKind = 0;
    if (hasNoUnsignedWrap())
      NoWrapKind |= NoUnsignedWrap;

    if (hasNoSignedWrap())
      NoWrapKind |= NoSignedWrap;

    return NoWrapKind;
  }

  /// Return true if the instruction is commutative
  /// @return True if the opcode is commutative.
  bool isCommutative() const { return Instruction::isCommutative(getOpcode()); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I is an overflowing binary operator.
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Add ||
           I->getOpcode() == Instruction::Sub ||
           I->getOpcode() == Instruction::Mul ||
           I->getOpcode() == Instruction::Shl;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param CE The constant expression to test.
  /// @return True if \p CE is an overflowing binary operator.
  static bool classof(const ConstantExpr *CE) {
    return CE->getOpcode() == Instruction::Add ||
           CE->getOpcode() == Instruction::Sub;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an OverflowingBinaryOperator.
  static bool classof(const Value *V) {
    return (isa<Instruction>(V) && classof(cast<Instruction>(V))) ||
           (isa<ConstantExpr>(V) && classof(cast<ConstantExpr>(V)));
  }
};

/// Operand layout traits for OverflowingBinaryOperator.
template <>
struct OperandTraits<OverflowingBinaryOperator>
    : public FixedNumOperandTraits<OverflowingBinaryOperator, 2> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(OverflowingBinaryOperator, Value)

/// A udiv, sdiv, lshr, or ashr instruction, which can be marked as "exact",
/// indicating that no bits are destroyed.
class PossiblyExactOperator : public Operator {
public:
  /// Exactness flag bits stored in SubclassOptionalData.
  enum {
    IsExact = (1 << 0) ///< Operation is known to be exact.
  };

private:
  friend class Instruction;
  friend class ConstantExpr;

  void setIsExact(bool B) {
    SubclassOptionalData = (SubclassOptionalData & ~IsExact) | (B * IsExact);
  }

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
  /// Return a reference to the operand at compile-time index \p Idx.
  /// @return Reference to the operand Use.
  template <int Idx> inline Use &Op();
  /// Return a const reference to the operand at compile-time index \p Idx.
  /// @return Const reference to the operand Use.
  template <int Idx> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Test whether this division is known to be exact, with zero remainder.
  /// @return True if the exact flag is set.
  bool isExact() const {
    return SubclassOptionalData & IsExact;
  }

  /// Return true if \p OpC is an opcode that may carry the exact flag.
  /// \param OpC The instruction opcode to test.
  /// @return True if \p OpC may carry the exact flag.
  static bool isPossiblyExactOpcode(unsigned OpC) {
    return OpC == Instruction::SDiv ||
           OpC == Instruction::UDiv ||
           OpC == Instruction::AShr ||
           OpC == Instruction::LShr;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I is a possibly-exact operator.
  static bool classof(const Instruction *I) {
    return isPossiblyExactOpcode(I->getOpcode());
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a PossiblyExactOperator.
  static bool classof(const Value *V) {
    return (isa<Instruction>(V) && classof(cast<Instruction>(V)));
  }
};

/// Operand layout traits for PossiblyExactOperator.
template <>
struct OperandTraits<PossiblyExactOperator>
    : public FixedNumOperandTraits<PossiblyExactOperator, 2> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(PossiblyExactOperator, Value)

/// Utility class for floating point operations which can have
/// information about relaxed accuracy requirements attached to them.
class FPMathOperator : public Operator {
private:
  friend class Instruction;

  LLVM_ABI LLVM_READONLY FastMathFlags &getFastMathFlagsImpl();

  /// 'Fast' means all bits are set.
  void setFast(bool B) {
    setHasAllowReassoc(B);
    setHasNoNaNs(B);
    setHasNoInfs(B);
    setHasNoSignedZeros(B);
    setHasAllowReciprocal(B);
    setHasAllowContract(B);
    setHasApproxFunc(B);
  }

  void setHasAllowReassoc(bool B) { getFastMathFlagsImpl().setAllowReassoc(B); }

  void setHasNoNaNs(bool B) { getFastMathFlagsImpl().setNoNaNs(B); }

  void setHasNoInfs(bool B) { getFastMathFlagsImpl().setNoInfs(B); }

  void setHasNoSignedZeros(bool B) {
    getFastMathFlagsImpl().setNoSignedZeros(B);
  }

  void setHasAllowReciprocal(bool B) {
    getFastMathFlagsImpl().setAllowReciprocal(B);
  }

  void setHasAllowContract(bool B) {
    getFastMathFlagsImpl().setAllowContract(B);
  }

  void setHasApproxFunc(bool B) { getFastMathFlagsImpl().setApproxFunc(B); }

  /// Convenience function for setting multiple fast-math flags.
  /// FMF is a mask of the bits to set.
  void setFastMathFlags(FastMathFlags FMF) { getFastMathFlagsImpl() |= FMF; }

  /// Convenience function for copying all fast-math flags.
  /// All values in FMF are transferred to this operator.
  void copyFastMathFlags(FastMathFlags FMF) { getFastMathFlagsImpl() = FMF; }

  /// Returns true if `Ty` is composed of a single kind of float-poing type
  /// (possibly repeated within an aggregate).
  static bool isComposedOfHomogeneousFloatingPointTypes(Type *Ty) {
    if (auto *StructTy = dyn_cast<StructType>(Ty)) {
      if (!StructTy->isLiteral() || !StructTy->containsHomogeneousTypes())
        return false;
      Ty = StructTy->elements().front();
    } else if (auto *ArrayTy = dyn_cast<ArrayType>(Ty)) {
      do {
        Ty = ArrayTy->getElementType();
      } while ((ArrayTy = dyn_cast<ArrayType>(Ty)) != nullptr);
    }
    return Ty->isFPOrFPVectorTy();
  };

public:
  /// Test if this operation allows all non-strict floating-point transforms.
  /// @return True if all fast-math flags are set.
  bool isFast() const { return getFastMathFlags().isFast(); }

  /// Test if this operation may be simplified with reassociative transforms.
  /// @return True if the allow-reassociation flag is set.
  bool hasAllowReassoc() const { return getFastMathFlags().allowReassoc(); }

  /// Test if this operation's arguments and results are assumed not-NaN.
  /// @return True if the no-NaNs flag is set.
  bool hasNoNaNs() const { return getFastMathFlags().noNaNs(); }

  /// Test if this operation's arguments and results are assumed not-infinite.
  /// @return True if the no-infs flag is set.
  bool hasNoInfs() const { return getFastMathFlags().noInfs(); }

  /// Test if this operation can ignore the sign of zero.
  /// @return True if the no-signed-zeros flag is set.
  bool hasNoSignedZeros() const { return getFastMathFlags().noSignedZeros(); }

  /// Test if this operation can use reciprocal multiply instead of division.
  /// @return True if the allow-reciprocal flag is set.
  bool hasAllowReciprocal() const {
    return getFastMathFlags().allowReciprocal();
  }

  /// Test if this operation can be floating-point contracted (FMA).
  /// @return True if the allow-contract flag is set.
  bool hasAllowContract() const { return getFastMathFlags().allowContract(); }

  /// Test if this operation allows approximations of math library functions or
  /// intrinsics.
  /// @return True if the approximate-functions flag is set.
  bool hasApproxFunc() const { return getFastMathFlags().approxFunc(); }

  /// Convenience function for getting all the fast-math flags
  /// @return The fast-math flags attached to this operator.
  FastMathFlags getFastMathFlags() const {
    return const_cast<FPMathOperator *>(this)->getFastMathFlagsImpl();
  }

  /// Get the maximum error permitted by this operation in ULPs. An accuracy of
  /// 0.0 means that the operation should be performed with the default
  /// precision.
  /// @return The maximum error in ULPs, or 0.0 for default precision.
  LLVM_ABI float getFPAccuracy() const;

  /// Returns true if `Ty` is a supported floating-point type for phi, select,
  /// or call FPMathOperators.
  /// \param Ty The type to test for FPMathOperator support.
  /// @return True if \p Ty is a supported floating-point type.
  static bool isSupportedFloatingPointType(Type *Ty) {
    return Ty->isFPOrFPVectorTy() ||
           isComposedOfHomogeneousFloatingPointTypes(Ty);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an FPMathOperator.
  static bool classof(const Value *V) {
    unsigned Opcode;
    if (auto *I = dyn_cast<Instruction>(V))
      Opcode = I->getOpcode();
    else
      return false;

    switch (Opcode) {
    case Instruction::FNeg:
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FDiv:
    case Instruction::FRem:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    // FIXME: To clean up and correct the semantics of fast-math-flags, FCmp
    //        should not be treated as a math op, but the other opcodes should.
    //        This would make things consistent with Select/PHI (FP value type
    //        determines whether they are math ops and, therefore, capable of
    //        having fast-math-flags).
    case Instruction::FCmp:
      return true;
    case Instruction::PHI:
    case Instruction::Select:
    case Instruction::Call: {
      return isSupportedFloatingPointType(V->getType());
    }
    default:
      return false;
    }
  }
};

/// A helper template for defining operators for individual opcodes.
template<typename SuperClass, unsigned Opc>
class ConcreteOperator : public SuperClass {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The instruction to test.
  /// @return True if \p I has this operator's opcode.
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Opc;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param CE The constant expression to test.
  /// @return True if \p CE has this operator's opcode.
  static bool classof(const ConstantExpr *CE) {
    return CE->getOpcode() == Opc;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an Instruction or ConstantExpr with this opcode.
  static bool classof(const Value *V) {
    return (isa<Instruction>(V) && classof(cast<Instruction>(V))) ||
           (isa<ConstantExpr>(V) && classof(cast<ConstantExpr>(V)));
  }
};

/// Concrete operator for the Add instruction opcode.
class AddOperator
  : public ConcreteOperator<OverflowingBinaryOperator, Instruction::Add> {
};
/// Concrete operator for the Sub instruction opcode.
class SubOperator
  : public ConcreteOperator<OverflowingBinaryOperator, Instruction::Sub> {
};
/// Concrete operator for the Mul instruction opcode.
class MulOperator
  : public ConcreteOperator<OverflowingBinaryOperator, Instruction::Mul> {
};
/// Concrete operator for the Shl instruction opcode.
class ShlOperator
  : public ConcreteOperator<OverflowingBinaryOperator, Instruction::Shl> {
};

/// Concrete operator for the AShr instruction opcode.
class AShrOperator
  : public ConcreteOperator<PossiblyExactOperator, Instruction::AShr> {
};
/// Concrete operator for the LShr instruction opcode.
class LShrOperator
  : public ConcreteOperator<PossiblyExactOperator, Instruction::LShr> {
};

/// Concrete operator for the GetElementPtr instruction opcode.
class GEPOperator
    : public ConcreteOperator<Operator, Instruction::GetElementPtr> {
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
  /// Return a reference to the operand at compile-time index \p Idx.
  /// @return Reference to the operand Use.
  template <int Idx> inline Use &Op();
  /// Return a const reference to the operand at compile-time index \p Idx.
  /// @return Const reference to the operand Use.
  template <int Idx> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Return the GEP no-wrap flags attached to this operator.
  /// @return The GEP no-wrap flags.
  GEPNoWrapFlags getNoWrapFlags() const {
    return GEPNoWrapFlags::fromRaw(SubclassOptionalData);
  }

  /// Test whether this is an inbounds GEP, as defined by LangRef.html.
  /// @return True if the inbounds flag is set.
  bool isInBounds() const { return getNoWrapFlags().isInBounds(); }

  /// Return true if this GEP has the nusw (no unsigned signed wrap) flag.
  /// @return True if the nusw flag is set.
  bool hasNoUnsignedSignedWrap() const {
    return getNoWrapFlags().hasNoUnsignedSignedWrap();
  }

  /// Return true if this GEP has the nuw (no unsigned wrap) flag.
  /// @return True if the nuw flag is set.
  bool hasNoUnsignedWrap() const {
    return getNoWrapFlags().hasNoUnsignedWrap();
  }

  /// Returns the offset of the index with an inrange attachment, or
  /// std::nullopt if none.
  /// @return The inrange ConstantRange, or std::nullopt if none.
  LLVM_ABI std::optional<ConstantRange> getInRange() const;

  /// Return an iterator to the first index operand.
  /// @return Iterator to the first index operand.
  inline op_iterator       idx_begin()       { return op_begin()+1; }
  /// Return a const iterator to the first index operand.
  /// @return Const iterator to the first index operand.
  inline const_op_iterator idx_begin() const { return op_begin()+1; }
  /// Return an iterator past the last index operand.
  /// @return Iterator past the last index operand.
  inline op_iterator       idx_end()         { return op_end(); }
  /// Return a const iterator past the last index operand.
  /// @return Const iterator past the last index operand.
  inline const_op_iterator idx_end()   const { return op_end(); }

  /// Return a mutable range over the GEP index operands.
  /// @return A mutable range over the index operands.
  inline iterator_range<op_iterator> indices() {
    return make_range(idx_begin(), idx_end());
  }

  /// Return a const range over the GEP index operands.
  /// @return A const range over the index operands.
  inline iterator_range<const_op_iterator> indices() const {
    return make_range(idx_begin(), idx_end());
  }

  /// Return the pointer operand of this GEP.
  /// @return The base pointer operand.
  Value *getPointerOperand() {
    return getOperand(0);
  }
  /// Return the pointer operand of this GEP.
  /// @return The base pointer operand.
  const Value *getPointerOperand() const {
    return getOperand(0);
  }
  /// Return the operand index of the pointer operand (always 0).
  /// @return The pointer operand index (always 0).
  static unsigned getPointerOperandIndex() {
    return 0U;                      // get index for modifying correct operand
  }

  /// Method to return the pointer operand as a PointerType.
  /// @return The type of the pointer operand.
  Type *getPointerOperandType() const {
    return getPointerOperand()->getType();
  }

  /// Return the source element type of this GEP.
  /// @return The source element type.
  LLVM_ABI Type *getSourceElementType() const;
  /// Return the result element type of this GEP.
  /// @return The result element type.
  LLVM_ABI Type *getResultElementType() const;

  /// Method to return the address space of the pointer operand.
  /// @return The address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperandType()->getPointerAddressSpace();
  }

  /// Return the number of index operands in this GEP.
  /// @return The number of index operands.
  unsigned getNumIndices() const {  // Note: always non-negative
    return getNumOperands() - 1;
  }

  /// Return true if this GEP has one or more index operands.
  /// @return True if there is at least one index operand.
  bool hasIndices() const {
    return getNumOperands() > 1;
  }

  /// Return true if all of the indices of this GEP are zeros.
  /// If so, the result pointer and the first operand have the same
  /// value, just potentially different types.
  /// @return True if every index operand is zero.
  bool hasAllZeroIndices() const {
    for (const_op_iterator I = idx_begin(), E = idx_end(); I != E; ++I) {
      if (ConstantInt *C = dyn_cast<ConstantInt>(I))
        if (C->isZero())
          continue;
      return false;
    }
    return true;
  }

  /// Return true if all of the indices of this GEP are constant integers.
  /// If so, the result pointer and the first operand have
  /// a constant offset between them.
  /// @return True if every index operand is a constant integer.
  bool hasAllConstantIndices() const {
    for (const_op_iterator I = idx_begin(), E = idx_end(); I != E; ++I) {
      if (!isa<ConstantInt>(I))
        return false;
    }
    return true;
  }

  /// Return the number of index operands that are not constant integers.
  /// @return The count of non-constant index operands.
  unsigned countNonConstantIndices() const {
    return count_if(indices(), [](const Use& use) {
        return !isa<ConstantInt>(*use);
      });
  }

  /// Compute the maximum alignment that this GEP is garranteed to preserve.
  /// \param DL The data layout used to interpret types and pointer sizes.
  /// @return The maximum alignment this GEP is guaranteed to preserve.
  LLVM_ABI Align getMaxPreservedAlignment(const DataLayout &DL) const;

  /// Accumulate the constant address offset of this GEP if possible.
  ///
  /// This routine accepts an APInt into which it will try to accumulate the
  /// constant offset of this GEP.
  ///
  /// If \p ExternalAnalysis is provided it will be used to calculate a offset
  /// when a operand of GEP is not constant.
  /// For example, for a value \p ExternalAnalysis might try to calculate a
  /// lower bound. If \p ExternalAnalysis is successful, it should return true.
  ///
  /// If the \p ExternalAnalysis returns false or the value returned by \p
  /// ExternalAnalysis results in a overflow/underflow, this routine returns
  /// false and the value of the offset APInt is undefined (it is *not*
  /// preserved!).
  ///
  /// The APInt passed into this routine must be at exactly as wide as the
  /// IntPtr type for the address space of the base GEP pointer.
  /// \param DL The data layout used to interpret types and pointer sizes.
  /// \param Offset APInt that receives the accumulated constant offset.
  /// \param ExternalAnalysis Optional callback to resolve non-constant indices.
  /// @return True if a constant offset was successfully accumulated.
  LLVM_ABI bool accumulateConstantOffset(
      const DataLayout &DL, APInt &Offset,
      function_ref<bool(Value &, APInt &)> ExternalAnalysis = nullptr) const;

  /// Accumulate a constant GEP offset from a source type and index list.
  /// \param SourceType The GEP source element type.
  /// \param Index The GEP index operands.
  /// \param DL The data layout used to interpret types and pointer sizes.
  /// \param Offset APInt that receives the accumulated constant offset.
  /// \param ExternalAnalysis Optional callback to resolve non-constant indices.
  /// @return True if a constant offset was successfully accumulated.
  LLVM_ABI static bool accumulateConstantOffset(
      Type *SourceType, ArrayRef<const Value *> Index, const DataLayout &DL,
      APInt &Offset,
      function_ref<bool(Value &, APInt &)> ExternalAnalysis = nullptr);

  /// Collect the offset of this GEP as a map of Values to their associated
  /// APInt multipliers, as well as a total Constant Offset.
  /// \param DL The data layout used to interpret types and pointer sizes.
  /// \param BitWidth Bit width of the offset APInts to produce.
  /// \param VariableOffsets Map from variable index values to their multipliers.
  /// \param ConstantOffset Receives the total constant portion of the offset.
  /// @return True if the offset was successfully collected.
  LLVM_ABI bool
  collectOffset(const DataLayout &DL, unsigned BitWidth,
                SmallMapVector<Value *, APInt, 4> &VariableOffsets,
                APInt &ConstantOffset) const;
};

/// Operand layout traits for GEPOperator.
template <>
struct OperandTraits<GEPOperator> : public VariadicOperandTraits<GEPOperator> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(GEPOperator, Value)

/// Concrete operator for the PtrToInt instruction opcode.
class PtrToIntOperator
    : public ConcreteOperator<Operator, Instruction::PtrToInt> {
  /// Incomplete type befriended for ptrtoint constant-expression access.
  friend class PtrToInt;
  friend class ConstantExpr;

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
  /// Return a reference to the operand at compile-time index \p Idx.
  /// @return Reference to the operand Use.
  template <int Idx> inline Use &Op();
  /// Return a const reference to the operand at compile-time index \p Idx.
  /// @return Const reference to the operand Use.
  template <int Idx> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Return the pointer operand of this ptrtoint.
  /// @return The pointer operand.
  Value *getPointerOperand() {
    return getOperand(0);
  }
  /// Return the pointer operand of this ptrtoint.
  /// @return The pointer operand.
  const Value *getPointerOperand() const {
    return getOperand(0);
  }

  /// Return the operand index of the pointer operand (always 0).
  /// @return The pointer operand index (always 0).
  static unsigned getPointerOperandIndex() {
    return 0U;                      // get index for modifying correct operand
  }

  /// Method to return the pointer operand as a PointerType.
  /// @return The type of the pointer operand.
  Type *getPointerOperandType() const {
    return getPointerOperand()->getType();
  }

  /// Method to return the address space of the pointer operand.
  /// @return The address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return cast<PointerType>(getPointerOperandType())->getAddressSpace();
  }
};

/// Operand layout traits for PtrToIntOperator.
template <>
struct OperandTraits<PtrToIntOperator>
    : public FixedNumOperandTraits<PtrToIntOperator, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(PtrToIntOperator, Value)

/// Concrete operator for the PtrToAddr instruction opcode.
class PtrToAddrOperator
    : public ConcreteOperator<Operator, Instruction::PtrToAddr> {
  /// Incomplete type befriended for ptrtoaddr constant-expression access.
  friend class PtrToAddr;
  friend class ConstantExpr;

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
  /// Return a reference to the operand at compile-time index \p Idx.
  /// @return Reference to the operand Use.
  template <int Idx> inline Use &Op();
  /// Return a const reference to the operand at compile-time index \p Idx.
  /// @return Const reference to the operand Use.
  template <int Idx> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Return the pointer operand of this ptrtoaddr.
  /// @return The pointer operand.
  Value *getPointerOperand() { return getOperand(0); }
  /// Return the pointer operand of this ptrtoaddr.
  /// @return The pointer operand.
  const Value *getPointerOperand() const { return getOperand(0); }

  /// Return the operand index of the pointer operand (always 0).
  /// @return The pointer operand index (always 0).
  static unsigned getPointerOperandIndex() {
    return 0U; // get index for modifying correct operand
  }

  /// Method to return the pointer operand as a PointerType.
  /// @return The type of the pointer operand.
  Type *getPointerOperandType() const { return getPointerOperand()->getType(); }

  /// Method to return the address space of the pointer operand.
  /// @return The address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return cast<PointerType>(getPointerOperandType())->getAddressSpace();
  }
};

/// Operand layout traits for PtrToAddrOperator.
template <>
struct OperandTraits<PtrToAddrOperator>
    : public FixedNumOperandTraits<PtrToAddrOperator, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(PtrToAddrOperator, Value)

/// Concrete operator for the BitCast instruction opcode.
class BitCastOperator
    : public ConcreteOperator<Operator, Instruction::BitCast> {
  friend class BitCastInst;
  friend class ConstantExpr;

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
  /// Return a reference to the operand at compile-time index \p Idx.
  /// @return Reference to the operand Use.
  template <int Idx> inline Use &Op();
  /// Return a const reference to the operand at compile-time index \p Idx.
  /// @return Const reference to the operand Use.
  template <int Idx> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Return the source type of this bitcast.
  /// @return The type of the bitcast source operand.
  Type *getSrcTy() const {
    return getOperand(0)->getType();
  }

  /// Return the destination type of this bitcast.
  /// @return The type of the bitcast result.
  Type *getDestTy() const {
    return getType();
  }
};

/// Operand layout traits for BitCastOperator.
template <>
struct OperandTraits<BitCastOperator>
    : public FixedNumOperandTraits<BitCastOperator, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(BitCastOperator, Value)

/// Concrete operator for the AddrSpaceCast instruction opcode.
class AddrSpaceCastOperator
    : public ConcreteOperator<Operator, Instruction::AddrSpaceCast> {
  friend class AddrSpaceCastInst;
  friend class ConstantExpr;

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
  /// Return a reference to the operand at compile-time index \p Idx.
  /// @return Reference to the operand Use.
  template <int Idx> inline Use &Op();
  /// Return a const reference to the operand at compile-time index \p Idx.
  /// @return Const reference to the operand Use.
  template <int Idx> inline const Use &Op() const;
public:
  /// Return the number of operands.
  /// @return The operand count.
  inline unsigned getNumOperands() const;

  /// Return the pointer operand of this addrspacecast.
  /// @return The pointer operand.
  Value *getPointerOperand() { return getOperand(0); }

  /// Return the pointer operand of this addrspacecast.
  /// @return The pointer operand.
  const Value *getPointerOperand() const { return getOperand(0); }

  /// Return the source address space of this addrspacecast.
  /// @return The address space of the source pointer.
  unsigned getSrcAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  /// Return the destination address space of this addrspacecast.
  /// @return The address space of the result pointer.
  unsigned getDestAddressSpace() const {
    return getType()->getPointerAddressSpace();
  }
};

/// Operand layout traits for AddrSpaceCastOperator.
template <>
struct OperandTraits<AddrSpaceCastOperator>
    : public FixedNumOperandTraits<AddrSpaceCastOperator, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(AddrSpaceCastOperator, Value)

} // end namespace llvm

#endif // LLVM_IR_OPERATOR_H
