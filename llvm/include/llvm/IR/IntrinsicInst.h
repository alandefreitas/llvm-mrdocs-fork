//===-- llvm/IntrinsicInst.h - Intrinsic Instruction Wrappers ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes that make it really easy to deal with intrinsic
// functions with the isa/dyncast family of functions.  In particular, this
// allows you to do things like:
//
//     if (MemCpyInst *MCI = dyn_cast<MemCpyInst>(Inst))
//        ... MCI->getDest() ... MCI->getSource() ...
//
// All intrinsic function calls are instances of the call instruction, so these
// are all subclasses of the CallInst class.  Note that none of these classes
// has state or virtual methods, which is an important part of this gross/neat
// hack working.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INTRINSICINST_H
#define LLVM_IR_INTRINSICINST_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/FPEnv.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>
#include <optional>

namespace llvm {

class Metadata;

/// A wrapper class for inspecting calls to intrinsic functions.
/// This allows the standard isa/dyncast/cast functionality to work with calls
/// to intrinsic functions.
class IntrinsicInst : public CallInst {
public:
  /// IntrinsicInst objects are not default-constructible.
  IntrinsicInst() = delete;
  /// IntrinsicInst objects are not copy-constructible.
  /// \param Other Unused; the copy constructor is deleted.
  IntrinsicInst(const IntrinsicInst &Other) = delete;
  /// IntrinsicInst objects are not copy-assignable.
  /// \param Other Unused; the copy assignment operator is deleted.
  IntrinsicInst &operator=(const IntrinsicInst &Other) = delete;

  /// Return the intrinsic ID of this intrinsic.
  /// @return The intrinsic ID of this intrinsic.
  Intrinsic::ID getIntrinsicID() const {
    return cast<Function>(getCalledOperand())->getIntrinsicID();
  }

  /// Return true if this intrinsic is an associative operation.
  /// @return True if this intrinsic is an associative operation.
  bool isAssociative() const {
    switch (getIntrinsicID()) {
    case Intrinsic::smax:
    case Intrinsic::smin:
    case Intrinsic::umax:
    case Intrinsic::umin:
      return true;
    default:
      return false;
    }
  }

  /// Return true if swapping the first two arguments to the intrinsic produces
  /// the same result.
  /// @return True if swapping the first two arguments to the intrinsic produces
  /// the same result.
  bool isCommutative() const {
    switch (getIntrinsicID()) {
    case Intrinsic::maxnum:
    case Intrinsic::minnum:
    case Intrinsic::maximum:
    case Intrinsic::minimum:
    case Intrinsic::maximumnum:
    case Intrinsic::minimumnum:
    case Intrinsic::smax:
    case Intrinsic::smin:
    case Intrinsic::umax:
    case Intrinsic::umin:
    case Intrinsic::sadd_sat:
    case Intrinsic::uadd_sat:
    case Intrinsic::sadd_with_overflow:
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::smul_with_overflow:
    case Intrinsic::umul_with_overflow:
    case Intrinsic::smul_fix:
    case Intrinsic::umul_fix:
    case Intrinsic::smul_fix_sat:
    case Intrinsic::umul_fix_sat:
    case Intrinsic::fma:
    case Intrinsic::fmuladd:
      return true;
    default:
      return false;
    }
  }

  /// Return true if operand \p Op participates in a commutative intrinsic.
  /// \param Op Zero-based operand index to test.
  /// @return True if operand \p Op participates in a commutative intrinsic.
  bool isCommutableOperand(unsigned Op) const {
    constexpr unsigned NumCommutativeOps = 2;
    return isCommutative() && Op < NumCommutativeOps;
  }

  /// Checks if the intrinsic is an annotation.
  /// @return True if the intrinsic is an annotation.
  bool isAssumeLikeIntrinsic() const {
    switch (getIntrinsicID()) {
    default: break;
    case Intrinsic::assume:
    case Intrinsic::sideeffect:
    case Intrinsic::pseudoprobe:
    case Intrinsic::dbg_assign:
    case Intrinsic::dbg_declare:
    case Intrinsic::dbg_value:
    case Intrinsic::dbg_label:
    case Intrinsic::invariant_start:
    case Intrinsic::invariant_end:
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::experimental_noalias_scope_decl:
    case Intrinsic::objectsize:
    case Intrinsic::ptr_annotation:
    case Intrinsic::var_annotation:
      return true;
    }
    return false;
  }

  /// Check if the intrinsic might lower into a regular function call in the
  /// course of IR transformations.
  /// \param IID Intrinsic identifier to test.
  /// @return True if the intrinsic might lower into a regular function call in
  /// the course of IR transformations.
  LLVM_ABI static bool mayLowerToFunctionCall(Intrinsic::ID IID);

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The call instruction to test.
  /// @return True if \p I is an IntrinsicInst.
  static bool classof(const CallInst *I) {
    auto *F = dyn_cast_or_null<Function>(I->getCalledOperand());
    return F && F->isIntrinsic();
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an IntrinsicInst.
  static bool classof(const Value *V) {
    return isa<CallInst>(V) && classof(cast<CallInst>(V));
  }
};

/// Check if \p ID corresponds to a lifetime intrinsic.
static inline bool isLifetimeIntrinsic(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end:
    return true;
  default:
    return false;
  }
}

/// This is the common base class for lifetime intrinsics.
class LifetimeIntrinsic : public IntrinsicInst {
public:
  /// \name Casting methods
  /// @{
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a LifetimeIntrinsic.
  static bool classof(const IntrinsicInst *I) {
    return isLifetimeIntrinsic(I->getIntrinsicID());
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a LifetimeIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

/// Check if \p ID corresponds to a debug info intrinsic.
static inline bool isDbgInfoIntrinsic(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::dbg_declare:
  case Intrinsic::dbg_value:
  case Intrinsic::dbg_label:
  case Intrinsic::dbg_assign:
    return true;
  default:
    return false;
  }
}

/// This is the common base class for debug info intrinsics.
class DbgInfoIntrinsic : public IntrinsicInst {
public:
  /// \name Casting methods
  /// @{
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a DbgInfoIntrinsic.
  static bool classof(const IntrinsicInst *I) {
    return isDbgInfoIntrinsic(I->getIntrinsicID());
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a DbgInfoIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

/// Bidirectional iterator over debug-info location operands.
///
/// Iterates ValueAsMetadata either as a single pointer or as a pointer to an
/// array of ValueAsMetadata*, dereferencing to the underlying Value*.
class location_op_iterator
    : public iterator_facade_base<location_op_iterator,
                                  std::bidirectional_iterator_tag, Value *> {
  PointerUnion<ValueAsMetadata *, ValueAsMetadata **> I;

public:
  /// Construct an iterator over a single ValueAsMetadata location.
  /// \param SingleIter Pointer to the sole location metadata.
  location_op_iterator(ValueAsMetadata *SingleIter) : I(SingleIter) {}
  /// Construct an iterator over an array of ValueAsMetadata locations.
  /// \param MultiIter Pointer to the first element of the location array.
  location_op_iterator(ValueAsMetadata **MultiIter) : I(MultiIter) {}

  /// Copy-construct a location operand iterator.
  /// \param R Iterator to copy.
  location_op_iterator(const location_op_iterator &R) : I(R.I) {}
  /// Copy-assign a location operand iterator.
  /// \param R Iterator to assign from.
  /// @return Reference to this iterator.
  location_op_iterator &operator=(const location_op_iterator &R) {
    I = R.I;
    return *this;
  }
  /// Return true if both iterators refer to the same location.
  /// \param RHS Iterator to compare against.
  /// @return True if both iterators refer to the same location.
  bool operator==(const location_op_iterator &RHS) const { return I == RHS.I; }
  /// Dereference to the current location Value (const).
  /// @return The current location Value.
  const Value *operator*() const {
    ValueAsMetadata *VAM = isa<ValueAsMetadata *>(I)
                               ? cast<ValueAsMetadata *>(I)
                               : *cast<ValueAsMetadata **>(I);
    return VAM->getValue();
  };
  /// Dereference to the current location Value.
  /// @return The current location Value.
  Value *operator*() {
    ValueAsMetadata *VAM = isa<ValueAsMetadata *>(I)
                               ? cast<ValueAsMetadata *>(I)
                               : *cast<ValueAsMetadata **>(I);
    return VAM->getValue();
  }
  /// Advance to the next location operand.
  /// @return Reference to this iterator.
  location_op_iterator &operator++() {
    if (isa<ValueAsMetadata *>(I))
      I = cast<ValueAsMetadata *>(I) + 1;
    else
      I = cast<ValueAsMetadata **>(I) + 1;
    return *this;
  }
  /// Move to the previous location operand.
  /// @return Reference to this iterator.
  location_op_iterator &operator--() {
    if (isa<ValueAsMetadata *>(I))
      I = cast<ValueAsMetadata *>(I) - 1;
    else
      I = cast<ValueAsMetadata **>(I) - 1;
    return *this;
  }
};

/// Lightweight class that wraps the location operand metadata of a debug
/// intrinsic. The raw location may be a ValueAsMetadata, an empty MDTuple,
/// or a DIArgList.
class RawLocationWrapper {
  Metadata *RawLocation = nullptr;

public:
  /// Construct an empty (null) raw location wrapper.
  RawLocationWrapper() = default;
  /// Construct a wrapper around non-null location operand metadata.
  /// \param RawLocation ValueAsMetadata, empty MDTuple, or DIArgList.
  explicit RawLocationWrapper(Metadata *RawLocation)
      : RawLocation(RawLocation) {
    // Allow ValueAsMetadata, empty MDTuple, DIArgList.
    assert(RawLocation && "unexpected null RawLocation");
    assert(isa<ValueAsMetadata>(RawLocation) || isa<DIArgList>(RawLocation) ||
           (isa<MDNode>(RawLocation) &&
            !cast<MDNode>(RawLocation)->getNumOperands()));
  }
  /// Return the raw location operand metadata.
  /// @return The raw location operand metadata.
  Metadata *getRawLocation() const { return RawLocation; }
  /// Return an iterator range over the variable's location operands.
  ///
  /// Depending on the intrinsic, these may be the variable's value or its
  /// address.
  /// @return An iterator range over the variable's location operands.
  LLVM_ABI iterator_range<location_op_iterator> location_ops() const;
  /// Return the location operand at index \p OpIdx.
  /// \param OpIdx Zero-based location operand index.
  /// @return The location operand at index \p OpIdx.
  LLVM_ABI Value *getVariableLocationOp(unsigned OpIdx) const;
  /// Return the number of variable location operands.
  /// @return The number of variable location operands.
  unsigned getNumVariableLocationOps() const {
    if (hasArgList())
      return cast<DIArgList>(getRawLocation())->getArgs().size();
    return 1;
  }
  /// Return true if the raw location is a DIArgList.
  /// @return True if the raw location is a DIArgList.
  bool hasArgList() const { return isa<DIArgList>(getRawLocation()); }
  /// Return true if this location is a kill (empty/undef) for \p Expression.
  /// \param Expression DIExpression associated with the location.
  /// @return True if this location is a kill (empty/undef) for \p Expression.
  bool isKillLocation(const DIExpression *Expression) const {
    // Check for "kill" sentinel values.
    // Non-variadic: empty metadata.
    if (!hasArgList() && isa<MDNode>(getRawLocation()))
      return true;
    // Variadic: empty DIArgList with empty expression.
    if (getNumVariableLocationOps() == 0 && !Expression->isComplex())
      return true;
    // Variadic and non-variadic: Interpret expressions using undef or poison
    // values as kills.
    return any_of(location_ops(), [](Value *V) { return isa<UndefValue>(V); });
  }

  /// Return true if \p A and \p B wrap the same raw location metadata.
  /// \param A Left-hand raw location wrapper.
  /// \param B Right-hand raw location wrapper.
  /// @return True if \p A and \p B wrap the same raw location metadata.
  friend bool operator==(const RawLocationWrapper &A,
                         const RawLocationWrapper &B) {
    return A.RawLocation == B.RawLocation;
  }
  /// Return true if \p A and \p B wrap different raw location metadata.
  /// \param A Left-hand raw location wrapper.
  /// \param B Right-hand raw location wrapper.
  /// @return True if \p A and \p B wrap different raw location metadata.
  friend bool operator!=(const RawLocationWrapper &A,
                         const RawLocationWrapper &B) {
    return !(A == B);
  }
  /// Order wrappers by raw location metadata pointer address.
  /// \param A Left-hand raw location wrapper.
  /// \param B Right-hand raw location wrapper.
  /// @return True if \p A orders after \p B by raw location metadata pointer address.
  friend bool operator>(const RawLocationWrapper &A,
                        const RawLocationWrapper &B) {
    return A.RawLocation > B.RawLocation;
  }
  /// Order wrappers by raw location metadata pointer address.
  /// \param A Left-hand raw location wrapper.
  /// \param B Right-hand raw location wrapper.
  /// @return True if \p A orders after or equal to \p B by raw location
  /// metadata pointer address.
  friend bool operator>=(const RawLocationWrapper &A,
                         const RawLocationWrapper &B) {
    return A.RawLocation >= B.RawLocation;
  }
  /// Order wrappers by raw location metadata pointer address.
  /// \param A Left-hand raw location wrapper.
  /// \param B Right-hand raw location wrapper.
  /// @return True if \p A orders before \p B by raw location metadata pointer address.
  friend bool operator<(const RawLocationWrapper &A,
                        const RawLocationWrapper &B) {
    return A.RawLocation < B.RawLocation;
  }
  /// Order wrappers by raw location metadata pointer address.
  /// \param A Left-hand raw location wrapper.
  /// \param B Right-hand raw location wrapper.
  /// @return True if \p A orders before or equal to \p B by raw location
  /// metadata pointer address.
  friend bool operator<=(const RawLocationWrapper &A,
                         const RawLocationWrapper &B) {
    return A.RawLocation <= B.RawLocation;
  }
};

/// This is the common base class for debug info intrinsics for variables.
class DbgVariableIntrinsic : public DbgInfoIntrinsic {
public:
  /// Return an iterator range over the variable's location operands.
  ///
  /// Depending on the intrinsic, these may be the variable's value or its
  /// address.
  /// @return An iterator range over the variable's location operands.
  LLVM_ABI iterator_range<location_op_iterator> location_ops() const;

  /// Return the location operand at index \p OpIdx.
  /// \param OpIdx Zero-based location operand index.
  /// @return The location operand at index \p OpIdx.
  LLVM_ABI Value *getVariableLocationOp(unsigned OpIdx) const;

  /// Replace location operand \p OldValue with \p NewValue.
  /// \param OldValue Existing location value to replace.
  /// \param NewValue Replacement location value.
  /// \param AllowEmpty If true, allow the old value to be missing.
  LLVM_ABI void replaceVariableLocationOp(Value *OldValue, Value *NewValue,
                                          bool AllowEmpty = false);
  /// Replace the location operand at index \p OpIdx with \p NewValue.
  /// \param OpIdx Zero-based location operand index.
  /// \param NewValue Replacement location value.
  LLVM_ABI void replaceVariableLocationOp(unsigned OpIdx, Value *NewValue);
  /// Append location operands and set a matching DIExpression.
  ///
  /// Adding a new location operand will always result in this intrinsic using
  /// an ArgList, and must always be accompanied by a new expression that uses
  /// the new operand.
  /// \param NewValues Additional location values to append.
  /// \param NewExpr Expression that references the new operands.
  LLVM_ABI void addVariableLocationOps(ArrayRef<Value *> NewValues,
                                       DIExpression *NewExpr);

  /// Set the DILocalVariable described by this intrinsic.
  /// \param NewVar New local variable metadata.
  void setVariable(DILocalVariable *NewVar) {
    setArgOperand(1, MetadataAsValue::get(NewVar->getContext(), NewVar));
  }

  /// Set the DIExpression for this intrinsic.
  /// \param NewExpr New debug expression metadata.
  void setExpression(DIExpression *NewExpr) {
    setArgOperand(2, MetadataAsValue::get(NewExpr->getContext(), NewExpr));
  }

  /// Return the number of variable location operands.
  /// @return The number of variable location operands.
  unsigned getNumVariableLocationOps() const {
    return getWrappedLocation().getNumVariableLocationOps();
  }

  /// Return true if the location operand is a DIArgList.
  /// @return True if the location operand is a DIArgList.
  bool hasArgList() const { return getWrappedLocation().hasArgList(); }

  /// Return true if this describes the address of a local variable.
  ///
  /// True for dbg.declare, but not dbg.value, which describes its value, or
  /// dbg.assign, which describes a combination of the variable's value and
  /// address.
  /// @return True if this describes the address of a local variable.
  bool isAddressOfVariable() const {
    return getIntrinsicID() == Intrinsic::dbg_declare;
  }

  /// Return true if this describes the value of a local variable.
  ///
  /// It is true for dbg.value, but false for dbg.declare, which describes its
  /// address, and false for dbg.assign, which describes a combination of the
  /// variable's value and address.
  /// @return True if this describes the value of a local variable.
  bool isValueOfVariable() const {
    return getIntrinsicID() == Intrinsic::dbg_value;
  }

  /// Mark all location operands as killed (poison).
  void setKillLocation() {
    // TODO: When/if we remove duplicate values from DIArgLists, we don't need
    // this set anymore.
    SmallPtrSet<Value *, 4> RemovedValues;
    for (Value *OldValue : location_ops()) {
      if (!RemovedValues.insert(OldValue).second)
        continue;
      Value *Poison = PoisonValue::get(OldValue->getType());
      replaceVariableLocationOp(OldValue, Poison);
    }
  }

  /// Return true if this intrinsic's location is a kill location.
  /// @return True if this intrinsic's location is a kill location.
  bool isKillLocation() const {
    return getWrappedLocation().isKillLocation(getExpression());
  }

  /// Return the DILocalVariable described by this intrinsic.
  /// @return The DILocalVariable described by this intrinsic.
  DILocalVariable *getVariable() const {
    return cast<DILocalVariable>(getRawVariable());
  }

  /// Return the DIExpression for this intrinsic.
  /// @return The DIExpression for this intrinsic.
  DIExpression *getExpression() const {
    return cast<DIExpression>(getRawExpression());
  }

  /// Return the raw location operand metadata.
  /// @return The raw location operand metadata.
  Metadata *getRawLocation() const {
    return cast<MetadataAsValue>(getArgOperand(0))->getMetadata();
  }

  /// Return a RawLocationWrapper for the location operand.
  /// @return A RawLocationWrapper for the location operand.
  RawLocationWrapper getWrappedLocation() const {
    return RawLocationWrapper(getRawLocation());
  }

  /// Return the raw DILocalVariable metadata operand.
  /// @return The raw DILocalVariable metadata operand.
  Metadata *getRawVariable() const {
    return cast<MetadataAsValue>(getArgOperand(1))->getMetadata();
  }

  /// Return the raw DIExpression metadata operand.
  /// @return The raw DIExpression metadata operand.
  Metadata *getRawExpression() const {
    return cast<MetadataAsValue>(getArgOperand(2))->getMetadata();
  }

  /// Set the raw location operand metadata directly.
  ///
  /// Use of this should generally be avoided; instead,
  /// replaceVariableLocationOp and addVariableLocationOps should be used where
  /// possible to avoid creating invalid state.
  /// \param Location New raw location metadata.
  void setRawLocation(Metadata *Location) {
    return setArgOperand(0, MetadataAsValue::get(getContext(), Location));
  }

  /// Get the size (in bits) of the variable, or fragment of the variable that
  /// is described.
  /// @return The size (in bits) of the variable, or fragment of the variable
  /// that is described.
  LLVM_ABI std::optional<uint64_t> getFragmentSizeInBits() const;

  /// Get the FragmentInfo for the variable.
  /// @return The FragmentInfo for the variable.
  std::optional<DIExpression::FragmentInfo> getFragment() const {
    return getExpression()->getFragmentInfo();
  }

  /// Return fragment info for the described slice of the variable.
  ///
  /// Get the FragmentInfo for the variable if it exists, otherwise return a
  /// FragmentInfo that covers the entire variable if the variable size is
  /// known, otherwise return a zero-sized fragment.
  /// @return Fragment info for the described slice of the variable.
  DIExpression::FragmentInfo getFragmentOrEntireVariable() const {
    DIExpression::FragmentInfo VariableSlice(0, 0);
    // Get the fragment or variable size, or zero.
    if (auto Sz = getFragmentSizeInBits())
      VariableSlice.SizeInBits = *Sz;
    if (auto Frag = getExpression()->getFragmentInfo())
      VariableSlice.OffsetInBits = Frag->OffsetInBits;
    return VariableSlice;
  }

  /// \name Casting methods
  /// @{
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a DbgVariableIntrinsic.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::dbg_declare:
    case Intrinsic::dbg_value:
    case Intrinsic::dbg_assign:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a DbgVariableIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
protected:
  /// Set argument operand \p i to \p v without going through public setters.
  /// \param i Zero-based argument operand index.
  /// \param v New operand value.
  void setArgOperand(unsigned i, Value *v) {
    DbgInfoIntrinsic::setArgOperand(i, v);
  }
  /// Set operand \p i to \p v without going through public setters.
  /// \param i Zero-based operand index.
  /// \param v New operand value.
  void setOperand(unsigned i, Value *v) { DbgInfoIntrinsic::setOperand(i, v); }
};

/// This represents the llvm.dbg.declare instruction.
class DbgDeclareInst : public DbgVariableIntrinsic {
public:
  /// Return the single address location operand for this dbg.declare.
  /// @return The single address location operand for this dbg.declare.
  Value *getAddress() const {
    assert(getNumVariableLocationOps() == 1 &&
           "dbg.declare must have exactly 1 location operand.");
    return getVariableLocationOp(0);
  }

  /// \name Casting methods
  /// @{
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a DbgDeclareInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::dbg_declare;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a DbgDeclareInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

/// This represents the llvm.dbg.value instruction.
class DbgValueInst : public DbgVariableIntrinsic {
public:
  /// Return the value location operand at index \p OpIdx.
  ///
  /// The default argument should only be used in ISel, and the default option
  /// should be removed once ISel support for multiple location ops is complete.
  /// \param OpIdx Zero-based location operand index.
  /// @return The value location operand at index \p OpIdx.
  Value *getValue(unsigned OpIdx = 0) const {
    return getVariableLocationOp(OpIdx);
  }
  /// Return an iterator range over all value location operands.
  /// @return An iterator range over all value location operands.
  iterator_range<location_op_iterator> getValues() const {
    return location_ops();
  }

  /// \name Casting methods
  /// @{
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a DbgValueInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::dbg_value ||
           I->getIntrinsicID() == Intrinsic::dbg_assign;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a DbgValueInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

/// This represents the llvm.dbg.assign instruction.
class DbgAssignIntrinsic : public DbgValueInst {
  enum Operands {
    OpValue,
    OpVar,
    OpExpr,
    OpAssignID,
    OpAddress,
    OpAddressExpr,
  };

public:
  /// Return the address component of this dbg.assign.
  /// @return The address component of this dbg.assign.
  LLVM_ABI Value *getAddress() const;
  /// Return the raw address metadata operand.
  /// @return The raw address metadata operand.
  Metadata *getRawAddress() const {
    return cast<MetadataAsValue>(getArgOperand(OpAddress))->getMetadata();
  }
  /// Return the raw DIAssignID metadata operand.
  /// @return The raw DIAssignID metadata operand.
  Metadata *getRawAssignID() const {
    return cast<MetadataAsValue>(getArgOperand(OpAssignID))->getMetadata();
  }
  /// Return the DIAssignID linking this intrinsic to a store.
  /// @return The DIAssignID linking this intrinsic to a store.
  DIAssignID *getAssignID() const { return cast<DIAssignID>(getRawAssignID()); }
  /// Return the raw address DIExpression metadata operand.
  /// @return The raw address DIExpression metadata operand.
  Metadata *getRawAddressExpression() const {
    return cast<MetadataAsValue>(getArgOperand(OpAddressExpr))->getMetadata();
  }
  /// Return the DIExpression applied to the address component.
  /// @return The DIExpression applied to the address component.
  DIExpression *getAddressExpression() const {
    return cast<DIExpression>(getRawAddressExpression());
  }
  /// Set the DIExpression applied to the address component.
  /// \param NewExpr New address expression metadata.
  void setAddressExpression(DIExpression *NewExpr) {
    setArgOperand(OpAddressExpr,
                  MetadataAsValue::get(NewExpr->getContext(), NewExpr));
  }
  /// Set the DIAssignID linking this intrinsic to a store.
  /// \param New New assign ID metadata.
  LLVM_ABI void setAssignId(DIAssignID *New);
  /// Set the address component of this dbg.assign.
  /// \param V New address value.
  LLVM_ABI void setAddress(Value *V);
  /// Kill the address component.
  LLVM_ABI void setKillAddress();
  /// Return true if this kills the address component.
  ///
  /// This doesn't take into account the position of the intrinsic, therefore a
  /// returned value of false does not guarentee the address is a valid location
  /// for the variable at the intrinsic's position in IR.
  /// @return True if this kills the address component.
  LLVM_ABI bool isKillAddress() const;
  /// Set the value component of this dbg.assign.
  /// \param V New value.
  LLVM_ABI void setValue(Value *V);
  /// \name Casting methods
  /// @{
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a DbgAssignIntrinsic.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::dbg_assign;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a DbgAssignIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

/// This represents the llvm.dbg.label instruction.
class DbgLabelInst : public DbgInfoIntrinsic {
public:
  /// Return the DILabel described by this dbg.label.
  /// @return The DILabel described by this dbg.label.
  DILabel *getLabel() const { return cast<DILabel>(getRawLabel()); }
  /// Set the DILabel described by this dbg.label.
  /// \param NewLabel New label metadata.
  void setLabel(DILabel *NewLabel) {
    setArgOperand(0, MetadataAsValue::get(getContext(), NewLabel));
  }

  /// Return the raw DILabel metadata operand.
  /// @return The raw DILabel metadata operand.
  Metadata *getRawLabel() const {
    return cast<MetadataAsValue>(getArgOperand(0))->getMetadata();
  }

  /// \name Casting methods
  /// @{
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a DbgLabelInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::dbg_label;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a DbgLabelInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

/// This is the common base class for vector predication intrinsics.
class VPIntrinsic : public IntrinsicInst {
public:
  /// Declare a llvm.vp.* intrinsic in \p M matching \p Params.
  ///
  /// Additionally, the load and gather intrinsics require \p ReturnType to be
  /// specified.
  /// \param M Module in which to declare the intrinsic.
  /// \param ID VP intrinsic identifier.
  /// \param ReturnType Result type for load/gather-style intrinsics.
  /// \param Params Operand values that determine the overloaded types.
  /// @return The declared Function for the VP intrinsic.
  LLVM_ABI static Function *
  getOrInsertDeclarationForParams(Module *M, Intrinsic::ID ID, Type *ReturnType,
                                  ArrayRef<Value *> Params);

  /// Return the operand index of the mask parameter for \p IntrinsicID.
  /// \param IntrinsicID VP intrinsic identifier.
  /// @return The operand index of the mask parameter for \p IntrinsicID.
  LLVM_ABI static std::optional<unsigned>
  getMaskParamPos(Intrinsic::ID IntrinsicID);
  /// Return the operand index of the vector-length parameter for \p IntrinsicID.
  /// \param IntrinsicID VP intrinsic identifier.
  /// @return The operand index of the vector-length parameter for \p IntrinsicID.
  LLVM_ABI static std::optional<unsigned>
  getVectorLengthParamPos(Intrinsic::ID IntrinsicID);

  /// Return true if \p ID is a VP intrinsic identifier.
  /// \param ID Intrinsic identifier to test.
  /// @return True if \p ID is a VP intrinsic identifier.
  LLVM_ABI static bool isVPIntrinsic(Intrinsic::ID ID);

  /// Return the mask parameter, or nullptr if none.
  /// @return The mask parameter, or nullptr if none.
  LLVM_ABI Value *getMaskParam() const;
  /// Set the mask parameter.
  /// \param NewMask New mask value.
  LLVM_ABI void setMaskParam(Value *NewMask);

  /// Return the vector-length parameter, or nullptr if none.
  /// @return The vector-length parameter, or nullptr if none.
  LLVM_ABI Value *getVectorLengthParam() const;
  /// Set the vector-length parameter.
  /// \param NewVL New vector-length value.
  LLVM_ABI void setVectorLengthParam(Value *NewVL);

  /// Return true if the vector-length parameter can be ignored.
  /// @return True if the vector-length parameter can be ignored.
  LLVM_ABI bool canIgnoreVectorLengthParam() const;

  /// Return the static element count the vector-length parameter applies to.
  /// @return The static element count the vector-length parameter applies to.
  LLVM_ABI ElementCount getStaticVectorLength() const;

  /// Return the alignment of the pointer used by this load/store/gather/scatter.
  /// @return The alignment of the pointer used by this load/store/gather/scatter.
  LLVM_ABI MaybeAlign getPointerAlignment() const;
  // MaybeAlign setPointerAlignment(Align NewAlign); // TODO

  /// Return the pointer operand of this load, store, gather, or scatter.
  /// @return The pointer operand of this load, store, gather, or scatter.
  LLVM_ABI Value *getMemoryPointerParam() const;
  /// Return the operand index of the memory pointer parameter for \p ID.
  /// \param ID VP intrinsic identifier.
  /// @return The operand index of the memory pointer parameter for \p ID.
  LLVM_ABI static std::optional<unsigned>
      getMemoryPointerParamPos(Intrinsic::ID ID);

  /// Return the data (payload) operand of this store or scatter.
  /// @return The data (payload) operand of this store or scatter.
  LLVM_ABI Value *getMemoryDataParam() const;
  /// Return the operand index of the memory data parameter for \p ID.
  /// \param ID VP intrinsic identifier.
  /// @return The operand index of the memory data parameter for \p ID.
  LLVM_ABI static std::optional<unsigned> getMemoryDataParamPos(Intrinsic::ID ID);

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a VPIntrinsic.
  static bool classof(const IntrinsicInst *I) {
    return isVPIntrinsic(I->getIntrinsicID());
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a VPIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the equivalent non-predicated opcode for this VP intrinsic.
  /// @return The equivalent non-predicated opcode for this VP intrinsic.
  std::optional<unsigned> getFunctionalOpcode() const {
    return getFunctionalOpcodeForVP(getIntrinsicID());
  }

  /// Return the equivalent non-predicated intrinsic ID for this VP intrinsic.
  /// @return The equivalent non-predicated intrinsic ID for this VP intrinsic.
  std::optional<unsigned> getFunctionalIntrinsicID() const {
    return getFunctionalIntrinsicIDForVP(getIntrinsicID());
  }

  /// Return the equivalent non-predicated opcode for VP intrinsic \p ID.
  /// \param ID VP intrinsic identifier.
  /// @return The equivalent non-predicated opcode for VP intrinsic \p ID.
  LLVM_ABI static std::optional<unsigned>
  getFunctionalOpcodeForVP(Intrinsic::ID ID);

  /// Return the equivalent non-predicated intrinsic ID for VP intrinsic \p ID.
  /// \param ID VP intrinsic identifier.
  /// @return The equivalent non-predicated intrinsic ID for VP intrinsic \p ID.
  LLVM_ABI static std::optional<Intrinsic::ID>
  getFunctionalIntrinsicIDForVP(Intrinsic::ID ID);
};

/// This represents vector predication reduction intrinsics.
class VPReductionIntrinsic : public VPIntrinsic {
public:
  /// Return true if \p ID is a VP reduction intrinsic.
  /// \param ID Intrinsic identifier to test.
  /// @return True if \p ID is a VP reduction intrinsic.
  LLVM_ABI static bool isVPReduction(Intrinsic::ID ID);

  /// Return the operand index of the reduction start value.
  /// @return The operand index of the reduction start value.
  LLVM_ABI unsigned getStartParamPos() const;
  /// Return the operand index of the vector value being reduced.
  /// @return The operand index of the vector value being reduced.
  LLVM_ABI unsigned getVectorParamPos() const;

  /// Return the start-value operand index for VP reduction \p ID.
  /// \param ID VP reduction intrinsic identifier.
  /// @return The start-value operand index for VP reduction \p ID.
  LLVM_ABI static std::optional<unsigned> getStartParamPos(Intrinsic::ID ID);
  /// Return the vector operand index for VP reduction \p ID.
  /// \param ID VP reduction intrinsic identifier.
  /// @return The vector operand index for VP reduction \p ID.
  LLVM_ABI static std::optional<unsigned> getVectorParamPos(Intrinsic::ID ID);

  /// \name Casting methods
  /// @{
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a VPReductionIntrinsic.
  static bool classof(const IntrinsicInst *I) {
    return VPReductionIntrinsic::isVPReduction(I->getIntrinsicID());
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a VPReductionIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

/// This is the common base class for constrained floating point intrinsics.
class ConstrainedFPIntrinsic : public IntrinsicInst {
public:
  /// Return the number of non-metadata arguments to this intrinsic.
  /// @return The number of non-metadata arguments to this intrinsic.
  LLVM_ABI unsigned getNonMetadataArgCount() const;
  /// Return the rounding mode metadata argument, if present.
  /// @return The rounding mode metadata argument, if present.
  LLVM_ABI std::optional<RoundingMode> getRoundingMode() const;
  /// Return the exception behavior metadata argument, if present.
  /// @return The exception behavior metadata argument, if present.
  LLVM_ABI std::optional<fp::ExceptionBehavior> getExceptionBehavior() const;
  /// Return true if this uses the default floating-point environment.
  /// @return True if this uses the default floating-point environment.
  LLVM_ABI bool isDefaultFPEnvironment() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a ConstrainedFPIntrinsic.
  LLVM_ABI static bool classof(const IntrinsicInst *I);
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a ConstrainedFPIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Constrained floating point compare intrinsics.
class ConstrainedFPCmpIntrinsic : public ConstrainedFPIntrinsic {
public:
  /// Return the floating-point comparison predicate.
  /// @return The floating-point comparison predicate.
  LLVM_ABI FCmpInst::Predicate getPredicate() const;
  /// Return true if this is a signaling constrained fcmp (fcmps).
  /// @return True if this is a signaling constrained fcmp (fcmps).
  bool isSignaling() const {
    return getIntrinsicID() == Intrinsic::experimental_constrained_fcmps;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a ConstrainedFPCmpIntrinsic.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::experimental_constrained_fcmp:
    case Intrinsic::experimental_constrained_fcmps:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a ConstrainedFPCmpIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class represents min/max intrinsics.
class MinMaxIntrinsic : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a MinMaxIntrinsic.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::umin:
    case Intrinsic::umax:
    case Intrinsic::smin:
    case Intrinsic::smax:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a MinMaxIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the left-hand-side operand.
  /// @return The left-hand-side operand.
  Value *getLHS() const { return getArgOperand(0); }
  /// Return the right-hand-side operand.
  /// @return The right-hand-side operand.
  Value *getRHS() const { return getArgOperand(1); }

  /// Returns the comparison predicate underlying the intrinsic.
  /// \param ID Min/max intrinsic identifier.
  /// @return The comparison predicate underlying the intrinsic.
  static ICmpInst::Predicate getPredicate(Intrinsic::ID ID) {
    switch (ID) {
    case Intrinsic::umin:
      return ICmpInst::Predicate::ICMP_ULT;
    case Intrinsic::umax:
      return ICmpInst::Predicate::ICMP_UGT;
    case Intrinsic::smin:
      return ICmpInst::Predicate::ICMP_SLT;
    case Intrinsic::smax:
      return ICmpInst::Predicate::ICMP_SGT;
    default:
      llvm_unreachable("Invalid intrinsic");
    }
  }

  /// Returns the comparison predicate underlying the intrinsic.
  /// @return The comparison predicate underlying the intrinsic.
  ICmpInst::Predicate getPredicate() const {
    return getPredicate(getIntrinsicID());
  }

  /// Whether the intrinsic is signed or unsigned.
  /// \param ID Min/max intrinsic identifier.
  /// @return True if the intrinsic is signed.
  static bool isSigned(Intrinsic::ID ID) {
    return ICmpInst::isSigned(getPredicate(ID));
  };

  /// Whether the intrinsic is signed or unsigned.
  /// @return True if the intrinsic is signed.
  bool isSigned() const { return isSigned(getIntrinsicID()); };

  /// Whether the intrinsic is a smin or umin.
  /// \param ID Min/max intrinsic identifier.
  /// @return True if the intrinsic is a smin or umin.
  static bool isMin(Intrinsic::ID ID) {
    switch (ID) {
    case Intrinsic::umin:
    case Intrinsic::smin:
      return true;
    case Intrinsic::umax:
    case Intrinsic::smax:
      return false;
    default:
      llvm_unreachable("Invalid intrinsic");
    }
  }

  /// Whether the intrinsic is a smin or a umin.
  /// @return True if the intrinsic is a smin or a umin.
  bool isMin() const { return isMin(getIntrinsicID()); }

  /// Whether the intrinsic is a smax or a umax.
  /// @return True if the intrinsic is a smax or a umax.
  bool isMax() const { return !isMin(getIntrinsicID()); }

  /// Returns the identity value for this min/max intrinsic, such
  /// that minmax(X, Identity) == X.
  /// \param ID Min/max intrinsic identifier.
  /// \param NumBits Bit width of the identity value.
  /// @return The identity value for this min/max intrinsic, such that minmax(X,
  /// Identity) == X.
  static APInt getIdentity(Intrinsic::ID ID, unsigned NumBits) {
    switch (ID) {
    case Intrinsic::umin:
      return APInt::getMaxValue(NumBits);
    case Intrinsic::umax:
      return APInt::getMinValue(NumBits);
    case Intrinsic::smin:
      return APInt::getSignedMaxValue(NumBits);
    case Intrinsic::smax:
      return APInt::getSignedMinValue(NumBits);
    default:
      llvm_unreachable("Invalid intrinsic");
    }
  }

  /// Returns the identity value for this min/max intrinsic, such
  /// that minmax(X, Identity) == X.
  /// @return The identity value for this min/max intrinsic, such that minmax(X,
  /// Identity) == X.
  APInt getIdentity() const {
    return getIdentity(getIntrinsicID(), getType()->getScalarSizeInBits());
  }

  /// Return the monotonic saturation threshold for this min/max intrinsic.
  ///
  /// Min/max intrinsics are monotonic, they operate on a fixed-bitwidth values,
  /// so there is a certain threshold value, upon reaching which,
  /// their value can no longer change. Return said threshold.
  /// \param ID Min/max intrinsic identifier.
  /// \param numBits Bit width of the saturation point.
  /// @return The monotonic saturation threshold for this min/max intrinsic.
  static APInt getSaturationPoint(Intrinsic::ID ID, unsigned numBits) {
    switch (ID) {
    case Intrinsic::umin:
      return APInt::getMinValue(numBits);
    case Intrinsic::umax:
      return APInt::getMaxValue(numBits);
    case Intrinsic::smin:
      return APInt::getSignedMinValue(numBits);
    case Intrinsic::smax:
      return APInt::getSignedMaxValue(numBits);
    default:
      llvm_unreachable("Invalid intrinsic");
    }
  }

  /// Return the monotonic saturation threshold for this min/max intrinsic.
  ///
  /// Min/max intrinsics are monotonic, they operate on a fixed-bitwidth values,
  /// so there is a certain threshold value, upon reaching which,
  /// their value can no longer change. Return said threshold.
  /// \param numBits Bit width of the saturation point.
  /// @return The monotonic saturation threshold for this min/max intrinsic.
  APInt getSaturationPoint(unsigned numBits) const {
    return getSaturationPoint(getIntrinsicID(), numBits);
  }

  /// Return the monotonic saturation threshold for this min/max intrinsic.
  ///
  /// Min/max intrinsics are monotonic, they operate on a fixed-bitwidth values,
  /// so there is a certain threshold value, upon reaching which,
  /// their value can no longer change. Return said threshold.
  /// \param ID Min/max intrinsic identifier.
  /// \param Ty Integer (or vector-of-integer) type for the constant.
  /// @return The monotonic saturation threshold for this min/max intrinsic.
  static Constant *getSaturationPoint(Intrinsic::ID ID, Type *Ty) {
    return Constant::getIntegerValue(
        Ty, getSaturationPoint(ID, Ty->getScalarSizeInBits()));
  }

  /// Return the monotonic saturation threshold for this min/max intrinsic.
  ///
  /// Min/max intrinsics are monotonic, they operate on a fixed-bitwidth values,
  /// so there is a certain threshold value, upon reaching which,
  /// their value can no longer change. Return said threshold.
  /// \param Ty Integer (or vector-of-integer) type for the constant.
  /// @return The monotonic saturation threshold for this min/max intrinsic.
  Constant *getSaturationPoint(Type *Ty) const {
    return getSaturationPoint(getIntrinsicID(), Ty);
  }
};

/// This class represents a ucmp/scmp intrinsic
class CmpIntrinsic : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a CmpIntrinsic.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::scmp:
    case Intrinsic::ucmp:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a CmpIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the left-hand-side operand.
  /// @return The left-hand-side operand.
  Value *getLHS() const { return getArgOperand(0); }
  /// Return the right-hand-side operand.
  /// @return The right-hand-side operand.
  Value *getRHS() const { return getArgOperand(1); }

  /// Return true if \p ID is the signed scmp intrinsic.
  /// \param ID ucmp/scmp intrinsic identifier.
  /// @return True if \p ID is the signed scmp intrinsic.
  static bool isSigned(Intrinsic::ID ID) { return ID == Intrinsic::scmp; }
  /// Return true if this is the signed scmp intrinsic.
  /// @return True if this is the signed scmp intrinsic.
  bool isSigned() const { return isSigned(getIntrinsicID()); }

  /// Return the greater-than predicate for ucmp/scmp intrinsic \p ID.
  /// \param ID ucmp/scmp intrinsic identifier.
  /// @return The greater-than predicate for ucmp/scmp intrinsic \p ID.
  static CmpInst::Predicate getGTPredicate(Intrinsic::ID ID) {
    return isSigned(ID) ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT;
  }
  /// Return the greater-than predicate for this ucmp/scmp intrinsic.
  /// @return The greater-than predicate for this ucmp/scmp intrinsic.
  CmpInst::Predicate getGTPredicate() const {
    return getGTPredicate(getIntrinsicID());
  }

  /// Return the less-than predicate for ucmp/scmp intrinsic \p ID.
  /// \param ID ucmp/scmp intrinsic identifier.
  /// @return The less-than predicate for ucmp/scmp intrinsic \p ID.
  static CmpInst::Predicate getLTPredicate(Intrinsic::ID ID) {
    return isSigned(ID) ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT;
  }
  /// Return the less-than predicate for this ucmp/scmp intrinsic.
  /// @return The less-than predicate for this ucmp/scmp intrinsic.
  CmpInst::Predicate getLTPredicate() const {
    return getLTPredicate(getIntrinsicID());
  }
};

/// This class represents an intrinsic that is based on a binary operation.
/// This includes op.with.overflow and saturating add/sub intrinsics.
class BinaryOpIntrinsic : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a BinaryOpIntrinsic.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::sadd_with_overflow:
    case Intrinsic::usub_with_overflow:
    case Intrinsic::ssub_with_overflow:
    case Intrinsic::umul_with_overflow:
    case Intrinsic::smul_with_overflow:
    case Intrinsic::uadd_sat:
    case Intrinsic::sadd_sat:
    case Intrinsic::usub_sat:
    case Intrinsic::ssub_sat:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a BinaryOpIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the left-hand-side operand.
  /// @return The left-hand-side operand.
  Value *getLHS() const { return getArgOperand(0); }
  /// Return the right-hand-side operand.
  /// @return The right-hand-side operand.
  Value *getRHS() const { return getArgOperand(1); }

  /// Returns the binary operation underlying the intrinsic.
  /// @return The binary operation underlying the intrinsic.
  LLVM_ABI Instruction::BinaryOps getBinaryOp() const;

  /// Whether the intrinsic is signed or unsigned.
  /// @return True if the intrinsic is signed.
  LLVM_ABI bool isSigned() const;

  /// Returns one of OBO::NoSignedWrap or OBO::NoUnsignedWrap.
  /// @return One of OBO::NoSignedWrap or OBO::NoUnsignedWrap.
  LLVM_ABI unsigned getNoWrapKind() const;
};

/// Represents an op.with.overflow intrinsic.
class WithOverflowInst : public BinaryOpIntrinsic {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a WithOverflowInst.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::sadd_with_overflow:
    case Intrinsic::usub_with_overflow:
    case Intrinsic::ssub_with_overflow:
    case Intrinsic::umul_with_overflow:
    case Intrinsic::smul_with_overflow:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a WithOverflowInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Represents a saturating add/sub intrinsic.
class SaturatingInst : public BinaryOpIntrinsic {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a SaturatingInst.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::uadd_sat:
    case Intrinsic::sadd_sat:
    case Intrinsic::usub_sat:
    case Intrinsic::ssub_sat:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a SaturatingInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// CRTP base providing common accessors for memory intrinsics.
///
/// Written as CRTP to avoid a common base class amongst the three atomicity
/// hierarchies.
template <typename Derived> class MemIntrinsicBase : public IntrinsicInst {
private:
  enum { ARG_DEST = 0, ARG_LENGTH = 2 };

public:
  /// Return the raw destination pointer operand.
  /// @return The raw destination pointer operand.
  Value *getRawDest() const {
    return const_cast<Value *>(getArgOperand(ARG_DEST));
  }
  /// Return the Use for the raw destination pointer operand (const).
  /// @return The Use for the raw destination pointer operand (const).
  const Use &getRawDestUse() const { return getArgOperandUse(ARG_DEST); }
  /// Return the Use for the raw destination pointer operand.
  /// @return The Use for the raw destination pointer operand.
  Use &getRawDestUse() { return getArgOperandUse(ARG_DEST); }

  /// Return the length operand.
  /// @return The length operand.
  Value *getLength() const {
    return const_cast<Value *>(getArgOperand(ARG_LENGTH));
  }
  /// Return the Use for the length operand (const).
  /// @return The Use for the length operand (const).
  const Use &getLengthUse() const { return getArgOperandUse(ARG_LENGTH); }
  /// Return the Use for the length operand.
  /// @return The Use for the length operand.
  Use &getLengthUse() { return getArgOperandUse(ARG_LENGTH); }

  /// Return the constant length in bytes, if the length operand is constant.
  /// @return The constant length in bytes, if the length operand is constant.
  std::optional<APInt> getLengthInBytes() const {
    ConstantInt *C = dyn_cast<ConstantInt>(getLength());
    if (!C)
      return std::nullopt;
    return C->getValue();
  }

  /// Return the destination pointer with pointer casts stripped.
  ///
  /// This is just like getRawDest, but it strips off any cast instructions
  /// (including addrspacecast) that feed it, giving the original input. The
  /// returned value is guaranteed to be a pointer.
  /// @return The destination pointer with pointer casts stripped.
  Value *getDest() const { return getRawDest()->stripPointerCasts(); }

  /// Return the address space of the raw destination pointer.
  /// @return The address space of the raw destination pointer.
  unsigned getDestAddressSpace() const {
    return cast<PointerType>(getRawDest()->getType())->getAddressSpace();
  }

  /// Return the alignment of the destination pointer parameter.
  /// @return The alignment of the destination pointer parameter.
  MaybeAlign getDestAlign() const { return getParamAlign(ARG_DEST); }

  /// Set the destination pointer operand.
  /// \param Ptr New destination pointer (must match the existing type).
  void setDest(Value *Ptr) {
    assert(getRawDest()->getType() == Ptr->getType() &&
           "setDest called with pointer of wrong type!");
    setArgOperand(ARG_DEST, Ptr);
  }

  /// Set the destination pointer alignment attribute.
  /// \param Alignment New alignment, or none to clear it.
  void setDestAlignment(MaybeAlign Alignment) {
    removeParamAttr(ARG_DEST, Attribute::Alignment);
    if (Alignment)
      addParamAttr(ARG_DEST,
                   Attribute::getWithAlignment(getContext(), *Alignment));
  }
  /// Set the destination pointer alignment attribute.
  /// \param Alignment New alignment.
  void setDestAlignment(Align Alignment) {
    removeParamAttr(ARG_DEST, Attribute::Alignment);
    addParamAttr(ARG_DEST,
                 Attribute::getWithAlignment(getContext(), Alignment));
  }

  /// Set the length operand.
  /// \param L New length value (must match the existing type).
  void setLength(Value *L) {
    assert(getLength()->getType() == L->getType() &&
           "setLength called with value of wrong type!");
    setArgOperand(ARG_LENGTH, L);
  }

  /// Set the length operand to constant \p L bytes.
  /// \param L New length in bytes.
  void setLength(uint64_t L) {
    setLength(ConstantInt::get(getLength()->getType(), L));
  }
};

/// Common base class for all memory transfer intrinsics. Simply provides
/// common methods.
template <class BaseCL> class MemTransferBase : public BaseCL {
private:
  enum { ARG_SOURCE = 1 };

public:
  /// Return the raw source pointer operand.
  /// @return The raw source pointer operand.
  Value *getRawSource() const {
    return const_cast<Value *>(BaseCL::getArgOperand(ARG_SOURCE));
  }
  /// Return the Use for the raw source pointer operand (const).
  /// @return The Use for the raw source pointer operand (const).
  const Use &getRawSourceUse() const {
    return BaseCL::getArgOperandUse(ARG_SOURCE);
  }
  /// Return the Use for the raw source pointer operand.
  /// @return The Use for the raw source pointer operand.
  Use &getRawSourceUse() { return BaseCL::getArgOperandUse(ARG_SOURCE); }

  /// Return the source pointer with pointer casts stripped.
  ///
  /// This is just like getRawSource, but it strips off any cast instructions
  /// that feed it, giving the original input. The returned value is guaranteed
  /// to be a pointer.
  /// @return The source pointer with pointer casts stripped.
  Value *getSource() const { return getRawSource()->stripPointerCasts(); }

  /// Return the address space of the raw source pointer.
  /// @return The address space of the raw source pointer.
  unsigned getSourceAddressSpace() const {
    return cast<PointerType>(getRawSource()->getType())->getAddressSpace();
  }

  /// Return the alignment of the source pointer parameter.
  /// @return The alignment of the source pointer parameter.
  MaybeAlign getSourceAlign() const {
    return BaseCL::getParamAlign(ARG_SOURCE);
  }

  /// Set the source pointer operand.
  /// \param Ptr New source pointer (must match the existing type).
  void setSource(Value *Ptr) {
    assert(getRawSource()->getType() == Ptr->getType() &&
           "setSource called with pointer of wrong type!");
    BaseCL::setArgOperand(ARG_SOURCE, Ptr);
  }

  /// Set the source pointer alignment attribute.
  /// \param Alignment New alignment, or none to clear it.
  void setSourceAlignment(MaybeAlign Alignment) {
    BaseCL::removeParamAttr(ARG_SOURCE, Attribute::Alignment);
    if (Alignment)
      BaseCL::addParamAttr(ARG_SOURCE, Attribute::getWithAlignment(
                                           BaseCL::getContext(), *Alignment));
  }

  /// Set the source pointer alignment attribute.
  /// \param Alignment New alignment.
  void setSourceAlignment(Align Alignment) {
    BaseCL::removeParamAttr(ARG_SOURCE, Attribute::Alignment);
    BaseCL::addParamAttr(ARG_SOURCE, Attribute::getWithAlignment(
                                         BaseCL::getContext(), Alignment));
  }
};

/// Common base class for all memset intrinsics. Simply provides
/// common methods.
template <class BaseCL> class MemSetBase : public BaseCL {
private:
  enum { ARG_VALUE = 1 };

public:
  /// Return the fill/value operand.
  /// @return The fill/value operand.
  Value *getValue() const {
    return const_cast<Value *>(BaseCL::getArgOperand(ARG_VALUE));
  }
  /// Return the Use for the fill/value operand (const).
  /// @return The Use for the fill/value operand (const).
  const Use &getValueUse() const { return BaseCL::getArgOperandUse(ARG_VALUE); }
  /// Return the Use for the fill/value operand.
  /// @return The Use for the fill/value operand.
  Use &getValueUse() { return BaseCL::getArgOperandUse(ARG_VALUE); }

  /// Set the fill/value operand.
  /// \param Val New fill value (must match the existing type).
  void setValue(Value *Val) {
    assert(getValue()->getType() == Val->getType() &&
           "setValue called with value of wrong type!");
    BaseCL::setArgOperand(ARG_VALUE, Val);
  }
};

/// This is the common base class for memset/memcpy/memmove.
class MemIntrinsic : public MemIntrinsicBase<MemIntrinsic> {
private:
  enum { ARG_VOLATILE = 3 };

public:
  /// Return the volatile flag operand as a ConstantInt.
  /// @return The volatile flag operand as a ConstantInt.
  ConstantInt *getVolatileCst() const {
    return cast<ConstantInt>(getArgOperand(ARG_VOLATILE));
  }

  /// Return true if this memory intrinsic is marked volatile.
  /// @return True if this memory intrinsic is marked volatile.
  bool isVolatile() const { return !getVolatileCst()->isZero(); }

  /// Set the volatile flag operand.
  /// \param V New volatile flag constant.
  void setVolatile(Constant *V) { setArgOperand(ARG_VOLATILE, V); }

  /// Return true if this is a force-inlined memset/memcpy intrinsic.
  /// @return True if this is a force-inlined memset/memcpy intrinsic.
  bool isForceInlined() const {
    switch (getIntrinsicID()) {
    case Intrinsic::memset_inline:
    case Intrinsic::memcpy_inline:
      return true;
    default:
      return false;
    }
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a MemIntrinsic.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memcpy:
    case Intrinsic::memmove:
    case Intrinsic::memset:
    case Intrinsic::memset_inline:
    case Intrinsic::memcpy_inline:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a MemIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class wraps the llvm.memset and llvm.memset.inline intrinsics.
class MemSetInst : public MemSetBase<MemIntrinsic> {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a MemSetInst.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memset:
    case Intrinsic::memset_inline:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a MemSetInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Wrapper for the llvm.experimental.memset.pattern intrinsic.
///
/// Note that despite the inheritance, this is not part of the MemIntrinsic
/// hierachy in terms of isa/cast.
class MemSetPatternInst : public MemSetBase<MemIntrinsic> {
private:
  enum { ARG_VOLATILE = 3 };

public:
  /// Return the volatile flag operand as a ConstantInt.
  /// @return The volatile flag operand as a ConstantInt.
  ConstantInt *getVolatileCst() const {
    return cast<ConstantInt>(getArgOperand(ARG_VOLATILE));
  }

  /// Return true if this memset.pattern is marked volatile.
  /// @return True if this memset.pattern is marked volatile.
  bool isVolatile() const { return !getVolatileCst()->isZero(); }

  /// Set the volatile flag operand.
  /// \param V New volatile flag constant.
  void setVolatile(Constant *V) { setArgOperand(ARG_VOLATILE, V); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a MemSetPatternInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::experimental_memset_pattern;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a MemSetPatternInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class wraps the llvm.memcpy/memmove intrinsics.
class MemTransferInst : public MemTransferBase<MemIntrinsic> {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a MemTransferInst.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memcpy:
    case Intrinsic::memmove:
    case Intrinsic::memcpy_inline:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a MemTransferInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class wraps the llvm.memcpy intrinsic.
class MemCpyInst : public MemTransferInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a MemCpyInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::memcpy ||
           I->getIntrinsicID() == Intrinsic::memcpy_inline;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a MemCpyInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class wraps the llvm.memmove intrinsic.
class MemMoveInst : public MemTransferInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a MemMoveInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::memmove;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a MemMoveInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Common base for atomic or non-atomic memset/memcpy/memmove intrinsics.
///
/// Covers llvm.element.unordered.atomic.memset/memcpy/memmove and
/// llvm.memset/memcpy/memmove.
class AnyMemIntrinsic : public MemIntrinsicBase<AnyMemIntrinsic> {
private:
  enum { ARG_ELEMENTSIZE = 3 };

public:
  /// Return true if this is a volatile non-atomic memory intrinsic.
  /// @return True if this is a volatile non-atomic memory intrinsic.
  bool isVolatile() const {
    // Only the non-atomic intrinsics can be volatile
    if (auto *MI = dyn_cast<MemIntrinsic>(this))
      return MI->isVolatile();
    return false;
  }

  /// Return true if this is an unordered-atomic memory intrinsic.
  /// @return True if this is an unordered-atomic memory intrinsic.
  bool isAtomic() const {
    switch (getIntrinsicID()) {
    case Intrinsic::memcpy_element_unordered_atomic:
    case Intrinsic::memmove_element_unordered_atomic:
    case Intrinsic::memset_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an AnyMemIntrinsic.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memcpy:
    case Intrinsic::memcpy_inline:
    case Intrinsic::memmove:
    case Intrinsic::memset:
    case Intrinsic::memset_inline:
    case Intrinsic::memcpy_element_unordered_atomic:
    case Intrinsic::memmove_element_unordered_atomic:
    case Intrinsic::memset_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an AnyMemIntrinsic.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the raw element-size-in-bytes operand (atomic intrinsics only).
  /// @return The raw element-size-in-bytes operand (atomic intrinsics only).
  Value *getRawElementSizeInBytes() const {
    assert(isAtomic());
    return getArgOperand(ARG_ELEMENTSIZE);
  }

  /// Return the element size in bytes (atomic intrinsics only).
  /// @return The element size in bytes (atomic intrinsics only).
  uint32_t getElementSizeInBytes() const {
    assert(isAtomic());
    return cast<ConstantInt>(getRawElementSizeInBytes())->getZExtValue();
  }
};

/// Wrapper for any memset intrinsic (atomic or non-atomic).
///
/// Covers llvm.element.unordered.atomic.memset and llvm.memset.
class AnyMemSetInst : public MemSetBase<AnyMemIntrinsic> {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an AnyMemSetInst.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memset:
    case Intrinsic::memset_inline:
    case Intrinsic::memset_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an AnyMemSetInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Wrapper for any memcpy/memmove intrinsic (atomic or non-atomic).
///
/// Covers llvm.element.unordered.atomic.memcpy/memmove and llvm.memcpy/memmove.
class AnyMemTransferInst : public MemTransferBase<AnyMemIntrinsic> {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an AnyMemTransferInst.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memcpy:
    case Intrinsic::memcpy_inline:
    case Intrinsic::memmove:
    case Intrinsic::memcpy_element_unordered_atomic:
    case Intrinsic::memmove_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an AnyMemTransferInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class represents any memcpy intrinsic
/// i.e. llvm.element.unordered.atomic.memcpy
///  and llvm.memcpy
class AnyMemCpyInst : public AnyMemTransferInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an AnyMemCpyInst.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memcpy:
    case Intrinsic::memcpy_inline:
    case Intrinsic::memcpy_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an AnyMemCpyInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class represents any memmove intrinsic
/// i.e. llvm.element.unordered.atomic.memmove
///  and llvm.memmove
class AnyMemMoveInst : public AnyMemTransferInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an AnyMemMoveInst.
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memmove:
    case Intrinsic::memmove_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an AnyMemMoveInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.va_start intrinsic.
class VAStartInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a VAStartInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::vastart;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a VAStartInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the va_list pointer operand.
  /// @return The va_list pointer operand.
  Value *getArgList() const { return getArgOperand(0); }
};

/// This represents the llvm.va_end intrinsic.
class VAEndInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a VAEndInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::vaend;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a VAEndInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the va_list pointer operand.
  /// @return The va_list pointer operand.
  Value *getArgList() const { return getArgOperand(0); }
};

/// This represents the llvm.va_copy intrinsic.
class VACopyInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a VACopyInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::vacopy;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a VACopyInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the destination va_list pointer operand.
  /// @return The destination va_list pointer operand.
  Value *getDest() const { return getArgOperand(0); }
  /// Return the source va_list pointer operand.
  /// @return The source va_list pointer operand.
  Value *getSrc() const { return getArgOperand(1); }
};

/// A base class for all instrprof intrinsics.
class InstrProfInstBase : public IntrinsicInst {
protected:
  /// Return true if \p I is a counter-style instrprof intrinsic.
  /// \param I Intrinsic call to test.
  /// @return True if \p I is a counter-style instrprof intrinsic.
  static bool isCounterBase(const IntrinsicInst &I) {
    switch (I.getIntrinsicID()) {
    case Intrinsic::instrprof_cover:
    case Intrinsic::instrprof_increment:
    case Intrinsic::instrprof_increment_step:
    case Intrinsic::instrprof_callsite:
    case Intrinsic::instrprof_timestamp:
    case Intrinsic::instrprof_value_profile:
      return true;
    }
    return false;
  }
  /// Return true if \p I is an MC/DC bitmap-style instrprof intrinsic.
  /// \param I Intrinsic call to test.
  /// @return True if \p I is an MC/DC bitmap-style instrprof intrinsic.
  static bool isMCDCBitmapBase(const IntrinsicInst &I) {
    switch (I.getIntrinsicID()) {
    case Intrinsic::instrprof_mcdc_parameters:
    case Intrinsic::instrprof_mcdc_tvbitmap_update:
      return true;
    }
    return false;
  }

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an InstrProfInstBase.
  static bool classof(const Value *V) {
    if (const auto *Instr = dyn_cast<IntrinsicInst>(V))
      return isCounterBase(*Instr) || isMCDCBitmapBase(*Instr);
    return false;
  }

  /// Return the instrumented function name as a GlobalVariable.
  /// @return The instrumented function name as a GlobalVariable.
  GlobalVariable *getName() const {
    return cast<GlobalVariable>(getNameValue());
  }

  /// Return the name operand relating this instruction to its function.
  ///
  /// This is the operand that can be used to relate the instruction to the
  /// function it belonged to at instrumentation time.
  /// @return The name operand relating this instruction to its function.
  Value *getNameValue() const { return getArgOperand(0)->stripPointerCasts(); }

  /// Set the name operand relating this instruction to its function.
  /// \param V New name value.
  void setNameValue(Value *V) { setArgOperand(0, V); }

  /// Return the CFG hash of the instrumented function.
  /// @return The CFG hash of the instrumented function.
  ConstantInt *getHash() const { return cast<ConstantInt>(getArgOperand(1)); }
};

/// A base class for all instrprof counter intrinsics.
class InstrProfCntrInstBase : public InstrProfInstBase {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an InstrProfCntrInstBase.
  static bool classof(const Value *V) {
    if (const auto *Instr = dyn_cast<IntrinsicInst>(V))
      return InstrProfInstBase::isCounterBase(*Instr);
    return false;
  }

  /// Return the number of counters for the instrumented function.
  /// @return The number of counters for the instrumented function.
  LLVM_ABI ConstantInt *getNumCounters() const;
  /// Return the index of the counter that this instruction acts on.
  /// @return The index of the counter that this instruction acts on.
  LLVM_ABI ConstantInt *getIndex() const;
  /// Set the counter index that this instruction acts on.
  /// \param Idx New counter index.
  LLVM_ABI void setIndex(uint32_t Idx);
};

/// This represents the llvm.instrprof.cover intrinsic.
class InstrProfCoverInst : public InstrProfCntrInstBase {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an InstrProfCoverInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_cover;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an InstrProfCoverInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.instrprof.increment intrinsic.
class InstrProfIncrementInst : public InstrProfCntrInstBase {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an InstrProfIncrementInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_increment ||
           I->getIntrinsicID() == Intrinsic::instrprof_increment_step;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an InstrProfIncrementInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// Return the step value added to the counter.
  /// @return The step value added to the counter.
  LLVM_ABI Value *getStep() const;
};

/// This represents the llvm.instrprof.increment.step intrinsic.
class InstrProfIncrementInstStep : public InstrProfIncrementInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an InstrProfIncrementInstStep.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_increment_step;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an InstrProfIncrementInstStep.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Wrapper for the llvm.instrprof.callsite intrinsic.
///
/// It is structurally like the increment or step counters, hence the
/// inheritance relationship, albeit somewhat tenuous (it's not 'counting' per
/// se).
class InstrProfCallsite : public InstrProfCntrInstBase {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an InstrProfCallsite.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_callsite;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an InstrProfCallsite.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// Return true if \p CB is a direct/indirect call we instrument.
  ///
  /// We instrument direct calls (but not to intrinsics), or indirect calls.
  /// \param CB Call to test.
  /// @return True if \p CB is a direct/indirect call we instrument.
  static bool canInstrumentCallsite(const CallBase &CB) {
    return !CB.isInlineAsm() &&
           (CB.isIndirectCall() ||
            (CB.getIntrinsicID() == Intrinsic::not_intrinsic));
  }
  /// Return the callee value associated with this callsite.
  /// @return The callee value associated with this callsite.
  LLVM_ABI Value *getCallee() const;
  /// Set the callee value associated with this callsite.
  /// \param Callee New callee value.
  LLVM_ABI void setCallee(Value *Callee);
};

/// This represents the llvm.instrprof.timestamp intrinsic.
class InstrProfTimestampInst : public InstrProfCntrInstBase {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an InstrProfTimestampInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_timestamp;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an InstrProfTimestampInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.instrprof.value.profile intrinsic.
class InstrProfValueProfileInst : public InstrProfCntrInstBase {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an InstrProfValueProfileInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_value_profile;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an InstrProfValueProfileInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the profiled target value operand.
  /// @return The profiled target value operand.
  Value *getTargetValue() const { return cast<Value>(getArgOperand(2)); }

  /// Return the value-profiling kind operand.
  /// @return The value-profiling kind operand.
  ConstantInt *getValueKind() const {
    return cast<ConstantInt>(getArgOperand(3));
  }

  /// Return the value site index.
  /// @return The value site index.
  ConstantInt *getIndex() const { return cast<ConstantInt>(getArgOperand(4)); }
};

/// A base class for instrprof mcdc intrinsics that require global bitmap bytes.
class InstrProfMCDCBitmapInstBase : public InstrProfInstBase {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an InstrProfMCDCBitmapInstBase.
  static bool classof(const IntrinsicInst *I) {
    return InstrProfInstBase::isMCDCBitmapBase(*I);
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an InstrProfMCDCBitmapInstBase.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the number of MCDC bitmap bits for the instrumented function.
  /// @return The number of MCDC bitmap bits for the instrumented function.
  ConstantInt *getNumBitmapBits() const {
    return cast<ConstantInt>(getArgOperand(2));
  }

  /// Return the number of MCDC bitmap bytes for the instrumented function.
  /// @return The number of MCDC bitmap bytes for the instrumented function.
  auto getNumBitmapBytes() const {
    return alignTo(getNumBitmapBits()->getZExtValue(), CHAR_BIT) / CHAR_BIT;
  }
};

/// This represents the llvm.instrprof.mcdc.parameters intrinsic.
class InstrProfMCDCBitmapParameters : public InstrProfMCDCBitmapInstBase {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an InstrProfMCDCBitmapParameters.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_mcdc_parameters;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an InstrProfMCDCBitmapParameters.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.instrprof.mcdc.tvbitmap.update intrinsic.
class InstrProfMCDCTVBitmapUpdate : public InstrProfMCDCBitmapInstBase {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an InstrProfMCDCTVBitmapUpdate.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_mcdc_tvbitmap_update;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an InstrProfMCDCTVBitmapUpdate.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the TestVector bitmap index this intrinsic acts on.
  /// @return The TestVector bitmap index this intrinsic acts on.
  ConstantInt *getBitmapIndex() const {
    return cast<ConstantInt>(getArgOperand(2));
  }

  /// Return the address of the condition bitmap for this TV update.
  ///
  /// The condition bitmap contains the index of the TestVector to update within
  /// the TestVector Bitmap.
  /// @return The address of the condition bitmap for this TV update.
  Value *getMCDCCondBitmapAddr() const { return cast<Value>(getArgOperand(3)); }
};

/// Wrapper for the llvm.pseudoprobe intrinsic.
class PseudoProbeInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a PseudoProbeInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::pseudoprobe;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a PseudoProbeInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the GUID of the function containing this probe.
  /// @return The GUID of the function containing this probe.
  ConstantInt *getFuncGuid() const {
    return cast<ConstantInt>(getArgOperand(0));
  }

  /// Return the probe index within the function.
  /// @return The probe index within the function.
  ConstantInt *getIndex() const { return cast<ConstantInt>(getArgOperand(1)); }

  /// Return the probe attribute flags.
  /// @return The probe attribute flags.
  ConstantInt *getAttributes() const {
    return cast<ConstantInt>(getArgOperand(2));
  }

  /// Return the probe factor (distribution weight).
  /// @return The probe factor (distribution weight).
  ConstantInt *getFactor() const { return cast<ConstantInt>(getArgOperand(3)); }
};

/// Wrapper for the llvm.experimental.noalias.scope.decl intrinsic.
class NoAliasScopeDeclInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a NoAliasScopeDeclInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::experimental_noalias_scope_decl;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a NoAliasScopeDeclInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the declared noalias scope list metadata.
  /// @return The declared noalias scope list metadata.
  MDNode *getScopeList() const {
    auto *MV =
        cast<MetadataAsValue>(getOperand(Intrinsic::NoAliasScopeDeclScopeArg));
    return cast<MDNode>(MV->getMetadata());
  }

  /// Set the declared noalias scope list metadata.
  /// \param ScopeList New scope-list metadata node.
  void setScopeList(MDNode *ScopeList) {
    setOperand(Intrinsic::NoAliasScopeDeclScopeArg,
               MetadataAsValue::get(getContext(), ScopeList));
  }
};

/// Common base class for representing values projected from a statepoint.
/// Currently, the only projections available are gc.result and gc.relocate.
class GCProjectionInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a GCProjectionInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::experimental_gc_relocate ||
      I->getIntrinsicID() == Intrinsic::experimental_gc_result;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a GCProjectionInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return true if this relocate is tied to the invoke statepoint.
  /// This includes relocates which are on the unwinding path.
  /// @return True if this relocate is tied to the invoke statepoint. This
  /// includes relocates which are on the unwinding path.
  bool isTiedToInvoke() const {
    const Value *Token = getArgOperand(0);

    return isa<LandingPadInst>(Token) || isa<InvokeInst>(Token);
  }

  /// The statepoint with which this gc.relocate is associated.
  /// @return The statepoint with which this projection is associated.
  LLVM_ABI const Value *getStatepoint() const;
};

/// Represents calls to the gc.relocate intrinsic.
class GCRelocateInst : public GCProjectionInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a GCRelocateInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::experimental_gc_relocate;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a GCRelocateInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// The index into the associate statepoint's argument list
  /// which contains the base pointer of the pointer whose
  /// relocation this gc.relocate describes.
  /// @return The statepoint argument-list index of the base pointer.
  unsigned getBasePtrIndex() const {
    return cast<ConstantInt>(getArgOperand(1))->getZExtValue();
  }

  /// The index into the associate statepoint's argument list which
  /// contains the pointer whose relocation this gc.relocate describes.
  /// @return The statepoint argument-list index of the derived pointer.
  unsigned getDerivedPtrIndex() const {
    return cast<ConstantInt>(getArgOperand(2))->getZExtValue();
  }

  /// Return the base pointer being relocated.
  /// @return The base pointer being relocated.
  LLVM_ABI Value *getBasePtr() const;
  /// Return the derived pointer being relocated.
  /// @return The derived pointer being relocated.
  LLVM_ABI Value *getDerivedPtr() const;
};

/// Represents calls to the gc.result intrinsic.
class GCResultInst : public GCProjectionInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a GCResultInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::experimental_gc_result;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a GCResultInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};


/// This represents the llvm.assume intrinsic.
class AssumeInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is an AssumeInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::assume;
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is an AssumeInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Check if \p ID corresponds to a convergence control intrinsic.
static inline bool isConvergenceControlIntrinsic(unsigned IntrinsicID) {
  switch (IntrinsicID) {
  default:
    return false;
  case Intrinsic::experimental_convergence_anchor:
  case Intrinsic::experimental_convergence_entry:
  case Intrinsic::experimental_convergence_loop:
    return true;
  }
}

/// Represents calls to the llvm.experimintal.convergence.* intrinsics.
class ConvergenceControlInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a ConvergenceControlInst.
  static bool classof(const IntrinsicInst *I) {
    return isConvergenceControlIntrinsic(I->getIntrinsicID());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a ConvergenceControlInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return true if this is a convergence.anchor intrinsic.
  /// @return True if this is a convergence.anchor intrinsic.
  bool isAnchor() const {
    return getIntrinsicID() == Intrinsic::experimental_convergence_anchor;
  }
  /// Return true if this is a convergence.entry intrinsic.
  /// @return True if this is a convergence.entry intrinsic.
  bool isEntry() const {
    return getIntrinsicID() == Intrinsic::experimental_convergence_entry;
  }
  /// Return true if this is a convergence.loop intrinsic.
  /// @return True if this is a convergence.loop intrinsic.
  bool isLoop() const {
    return getIntrinsicID() == Intrinsic::experimental_convergence_loop;
  }

  /// Create a convergence.anchor intrinsic at the end of \p BB.
  /// \param BB Basic block to insert into.
  /// @return The newly created ConvergenceControlInst.
  LLVM_ABI static ConvergenceControlInst *CreateAnchor(BasicBlock &BB);
  /// Create a convergence.entry intrinsic at the end of \p BB.
  /// \param BB Basic block to insert into.
  /// @return The newly created ConvergenceControlInst.
  LLVM_ABI static ConvergenceControlInst *CreateEntry(BasicBlock &BB);
  /// Create a convergence.loop intrinsic in \p BB under parent \p Parent.
  /// \param BB Basic block to insert into.
  /// \param Parent Parent convergence control token.
  /// @return The newly created ConvergenceControlInst.
  LLVM_ABI static ConvergenceControlInst *
  CreateLoop(BasicBlock &BB, ConvergenceControlInst *Parent);
};

/// Wrapper for the llvm.structured.alloca intrinsic.
class StructuredAllocaInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a StructuredAllocaInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::structured_alloca;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a StructuredAllocaInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the allocated element type from the ElementType attribute.
  /// @return The allocated element type from the ElementType attribute.
  Type *getAllocationType() const {
    return getRetAttr(Attribute::ElementType).getValueAsType();
  }
};

/// Wrapper for the llvm.structured.gep intrinsic.
class StructuredGEPInst : public IntrinsicInst {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param I The intrinsic call to test.
  /// @return True if \p I is a StructuredGEPInst.
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::structured_gep;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// @return True if \p V is a StructuredGEPInst.
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return the operand index of the base pointer (always 0).
  /// @return The operand index of the base pointer (always 0).
  static unsigned getPointerOperandIndex() { return 0; }

  /// Return the base pointer operand.
  /// @return The base pointer operand.
  Value *getPointerOperand() const {
    return getOperand(getPointerOperandIndex());
  }

  /// Return the base aggregate type from the ElementType attribute.
  /// @return The base aggregate type from the ElementType attribute.
  Type *getBaseType() const {
    return getParamAttr(0, Attribute::ElementType).getValueAsType();
  }

  /// Return the number of index operands.
  /// @return The number of index operands.
  unsigned getNumIndices() const { return arg_size() - 1; }

  /// Return the index operand at zero-based position \p Index.
  /// \param Index Zero-based index among GEP indices.
  /// @return The index operand at zero-based position \p Index.
  Value *getIndexOperand(size_t Index) const {
    assert(Index < getNumIndices());
    return getOperand(Index + 1);
  }

  /// Return an iterator range over the GEP index operands.
  /// @return An iterator range over the GEP index operands.
  inline iterator_range<op_iterator> indices() {
    return make_range(op_begin() + 1, op_begin() + 1 + getNumIndices());
  }

  /// Return the element type reached after applying all indices.
  /// @return The element type reached after applying all indices.
  Type *getResultElementType() const {
    Type *CurrentType = getBaseType();
    for (unsigned I = 0; I < getNumIndices(); I++) {
      if (ArrayType *AT = dyn_cast<ArrayType>(CurrentType)) {
        CurrentType = AT->getElementType();
      } else if (VectorType *VT = dyn_cast<VectorType>(CurrentType)) {
        CurrentType = VT->getElementType();
      } else if (StructType *ST = dyn_cast<StructType>(CurrentType)) {
        ConstantInt *CI = cast<ConstantInt>(getIndexOperand(I));
        CurrentType = ST->getElementType(CI->getZExtValue());
      } else {
        // FIXME(Keenuts): add testing reaching those places once initial
        // implementation has landed.
        llvm_unreachable("unimplemented");
      }
    }

    return CurrentType;
  }
};

} // end namespace llvm

#endif // LLVM_IR_INTRINSICINST_H
