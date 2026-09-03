//===- llvm/Support/KnownBits.h - Stores known zeros/ones -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a class for representing known zeros and ones used by
// computeKnownBits.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_KNOWNBITS_H
#define LLVM_SUPPORT_KNOWNBITS_H

#include "llvm/ADT/APInt.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {

/// Tracks known zero and one bits of an integer value.
struct KnownBits {
  /// Bits known to be zero.
  APInt Zero;
  /// Bits known to be one.
  APInt One;

private:
  // Internal constructor for creating a KnownBits from two APInts.
  KnownBits(APInt Zero, APInt One)
      : Zero(std::move(Zero)), One(std::move(One)) {}

  // Flip the range of values: [-0x80000000, 0x7FFFFFFF] <-> [0, 0xFFFFFFFF]
  static KnownBits flipSignBit(const KnownBits &Val);

public:
  /// Default construct Zero and One.
  KnownBits() = default;

  /// Create a known bits object of BitWidth bits initialized to unknown.
  ///
  /// \param BitWidth Number of bits to track.
  explicit KnownBits(unsigned BitWidth) : Zero(BitWidth, 0), One(BitWidth, 0) {}

  /// Get the bit width of this value.
  /// @return The bit width of this value.
  unsigned getBitWidth() const {
    assert(Zero.getBitWidth() == One.getBitWidth() &&
           "Zero and One should have the same width!");
    return Zero.getBitWidth();
  }

  /// Returns true if there is conflicting information.
  /// @return True if there is conflicting information.
  bool hasConflict() const { return Zero.intersects(One); }

  /// Returns true if we know the value of all bits.
  /// @return True if we know the value of all bits.
  bool isConstant() const { return Zero.isInverseOf(One); }

  /// Returns the value when all bits have a known value. This just returns One
  /// with a protective assertion.
  /// @return The constant value when all bits are known.
  const APInt &getConstant() const {
    assert(isConstant() && "Can only get value when all bits are known");
    return One;
  }

  /// Returns true if we don't know any bits.
  /// @return True if we don't know any bits.
  bool isUnknown() const { return Zero.isZero() && One.isZero(); }

  /// Returns true if we don't know the sign bit.
  /// @return True if we don't know the sign bit.
  bool isSignUnknown() const {
    return !Zero.isSignBitSet() && !One.isSignBitSet();
  }

  /// Resets the known state of all bits.
  void resetAll() {
    Zero.clearAllBits();
    One.clearAllBits();
  }

  /// Returns true if value is all zero.
  /// @return True if value is all zero.
  bool isZero() const { return Zero.isAllOnes(); }

  /// Returns true if value is all one bits.
  /// @return True if value is all one bits.
  bool isAllOnes() const { return One.isAllOnes(); }

  /// Make all bits known to be zero and discard any previous information.
  void setAllZero() {
    Zero.setAllBits();
    One.clearAllBits();
  }

  /// Make all bits known to be one and discard any previous information.
  void setAllOnes() {
    Zero.clearAllBits();
    One.setAllBits();
  }

  /// Make all bits known to be both zero and one. Useful before a loop that
  /// calls intersectWith.
  void setAllConflict() {
    Zero.setAllBits();
    One.setAllBits();
  }

  /// Returns true if this value is known to be negative.
  /// @return True if this value is known to be negative.
  bool isNegative() const { return One.isSignBitSet(); }

  /// Returns true if this value is known to be non-negative.
  /// @return True if this value is known to be non-negative.
  bool isNonNegative() const { return Zero.isSignBitSet(); }

  /// Returns true if this value is known to be non-zero.
  /// @return True if this value is known to be non-zero.
  bool isNonZero() const { return !One.isZero(); }

  /// Returns true if this value is known to be positive.
  /// @return True if this value is known to be positive.
  bool isStrictlyPositive() const {
    return Zero.isSignBitSet() && !One.isZero();
  }

  /// Returns true if this value is known to be non-positive.
  /// @return True if this value is known to be non-positive.
  bool isNonPositive() const { return getSignedMaxValue().isNonPositive(); }

  /// Make this value negative.
  void makeNegative() {
    One.setSignBit();
  }

  /// Make this value non-negative.
  void makeNonNegative() {
    Zero.setSignBit();
  }

  /// Return the minimal unsigned value possible given these KnownBits.
  /// @return The minimal unsigned value possible given these KnownBits.
  APInt getMinValue() const {
    // Assume that all bits that aren't known-ones are zeros.
    return One;
  }

  /// Return the minimal signed value possible given these KnownBits.
  /// @return The minimal signed value possible given these KnownBits.
  APInt getSignedMinValue() const {
    // Assume that all bits that aren't known-ones are zeros.
    APInt Min = One;
    // Sign bit is unknown.
    if (Zero.isSignBitClear())
      Min.setSignBit();
    return Min;
  }

  /// Return the maximal unsigned value possible given these KnownBits.
  /// @return The maximal unsigned value possible given these KnownBits.
  APInt getMaxValue() const {
    // Assume that all bits that aren't known-zeros are ones.
    return ~Zero;
  }

  /// Return the maximal signed value possible given these KnownBits.
  /// @return The maximal signed value possible given these KnownBits.
  APInt getSignedMaxValue() const {
    // Assume that all bits that aren't known-zeros are ones.
    APInt Max = ~Zero;
    // Sign bit is unknown.
    if (One.isSignBitClear())
      Max.clearSignBit();
    return Max;
  }

  /// Return if the value is known even (the low bit is 0).
  /// @return True if the value is known even (the low bit is 0).
  bool isEven() const { return Zero[0]; }

  /// Return known bits for a truncation of the value we're tracking.
  ///
  /// \param BitWidth Bit width of the truncated result.
  /// @return Known bits for a truncation of the tracked value.
  KnownBits trunc(unsigned BitWidth) const {
    return KnownBits(Zero.trunc(BitWidth), One.trunc(BitWidth));
  }

  /// Return known bits for an "any" extension of the value we're tracking,
  /// where we don't know anything about the extended bits.
  ///
  /// \param BitWidth Bit width of the extended result.
  /// @return Known bits for an any-extension of the tracked value.
  KnownBits anyext(unsigned BitWidth) const {
    return KnownBits(Zero.zext(BitWidth), One.zext(BitWidth));
  }

  /// Return known bits for a zero extension of the value we're tracking.
  ///
  /// \param BitWidth Bit width of the extended result.
  /// @return Known bits for a zero extension of the tracked value.
  KnownBits zext(unsigned BitWidth) const {
    unsigned OldBitWidth = getBitWidth();
    APInt NewZero = Zero.zext(BitWidth);
    NewZero.setBitsFrom(OldBitWidth);
    return KnownBits(NewZero, One.zext(BitWidth));
  }

  /// Return known bits for a sign extension of the value we're tracking.
  ///
  /// \param BitWidth Bit width of the extended result.
  /// @return Known bits for a sign extension of the tracked value.
  KnownBits sext(unsigned BitWidth) const {
    return KnownBits(Zero.sext(BitWidth), One.sext(BitWidth));
  }

  /// Return known bits for an "any" extension or truncation of the value we're
  /// tracking.
  ///
  /// \param BitWidth Desired bit width of the result.
  /// @return Known bits after any-extension or truncation to BitWidth.
  KnownBits anyextOrTrunc(unsigned BitWidth) const {
    if (BitWidth > getBitWidth())
      return anyext(BitWidth);
    if (BitWidth < getBitWidth())
      return trunc(BitWidth);
    return *this;
  }

  /// Return known bits for a zero extension or truncation of the value we're
  /// tracking.
  ///
  /// \param BitWidth Desired bit width of the result.
  /// @return Known bits after zero extension or truncation to BitWidth.
  KnownBits zextOrTrunc(unsigned BitWidth) const {
    if (BitWidth > getBitWidth())
      return zext(BitWidth);
    if (BitWidth < getBitWidth())
      return trunc(BitWidth);
    return *this;
  }

  /// Return known bits for a sign extension or truncation of the value we're
  /// tracking.
  ///
  /// \param BitWidth Desired bit width of the result.
  /// @return Known bits after sign extension or truncation to BitWidth.
  KnownBits sextOrTrunc(unsigned BitWidth) const {
    if (BitWidth > getBitWidth())
      return sext(BitWidth);
    if (BitWidth < getBitWidth())
      return trunc(BitWidth);
    return *this;
  }

  /// Truncate with signed saturation (signed input -> signed output)
  ///
  /// \param BitWidth Bit width of the saturated result.
  /// @return Known bits after truncate with signed saturation.
  LLVM_ABI KnownBits truncSSat(unsigned BitWidth) const;

  /// Truncate with signed saturation to unsigned (signed input -> unsigned
  /// output)
  ///
  /// \param BitWidth Bit width of the saturated result.
  /// @return Known bits after truncate with signed saturation to unsigned.
  LLVM_ABI KnownBits truncSSatU(unsigned BitWidth) const;

  /// Truncate with unsigned saturation (unsigned input -> unsigned output)
  ///
  /// \param BitWidth Bit width of the saturated result.
  /// @return Known bits after truncate with unsigned saturation.
  LLVM_ABI KnownBits truncUSat(unsigned BitWidth) const;

  /// Return known bits for a in-register sign extension of the value we're
  /// tracking.
  ///
  /// \param SrcBitWidth Original bit width before the in-register sext.
  /// @return Known bits for an in-register sign extension.
  LLVM_ABI KnownBits sextInReg(unsigned SrcBitWidth) const;

  /// Insert the bits from a smaller known bits starting at bitPosition.
  ///
  /// \param SubBits Known bits to insert.
  /// \param BitPosition Starting bit index for the insertion.
  void insertBits(const KnownBits &SubBits, unsigned BitPosition) {
    Zero.insertBits(SubBits.Zero, BitPosition);
    One.insertBits(SubBits.One, BitPosition);
  }

  /// Return a subset of the known bits from [bitPosition,bitPosition+numBits).
  ///
  /// \param NumBits Number of bits to extract.
  /// \param BitPosition Starting bit index of the range.
  /// @return Known bits for the extracted bit range.
  KnownBits extractBits(unsigned NumBits, unsigned BitPosition) const {
    return KnownBits(Zero.extractBits(NumBits, BitPosition),
                     One.extractBits(NumBits, BitPosition));
  }

  /// Concatenate the bits from \p Lo onto the bottom of *this.  This is
  /// equivalent to:
  ///   (this->zext(NewWidth) << Lo.getBitWidth()) | Lo.zext(NewWidth)
  ///
  /// \param Lo Low-order known bits to concatenate.
  /// @return Known bits for the concatenated value.
  KnownBits concat(const KnownBits &Lo) const {
    return KnownBits(Zero.concat(Lo.Zero), One.concat(Lo.One));
  }

  /// Return KnownBits based on this, but updated given that the underlying
  /// value is known to be greater than or equal to Val.
  ///
  /// \param Val Minimum known unsigned value of the underlying integer.
  /// @return KnownBits updated given the underlying value is >= Val.
  LLVM_ABI KnownBits makeGE(const APInt &Val) const;

  /// Returns the minimum number of trailing zero bits.
  /// @return The minimum number of trailing zero bits.
  unsigned countMinTrailingZeros() const { return Zero.countr_one(); }

  /// Returns the minimum number of trailing one bits.
  /// @return The minimum number of trailing one bits.
  unsigned countMinTrailingOnes() const { return One.countr_one(); }

  /// Returns the minimum number of leading zero bits.
  /// @return The minimum number of leading zero bits.
  unsigned countMinLeadingZeros() const { return Zero.countl_one(); }

  /// Returns the minimum number of leading one bits.
  /// @return The minimum number of leading one bits.
  unsigned countMinLeadingOnes() const { return One.countl_one(); }

  /// Returns the number of times the sign bit is replicated into the other
  /// bits.
  /// @return The number of times the sign bit is replicated.
  unsigned countMinSignBits() const {
    if (isNonNegative())
      return countMinLeadingZeros();
    if (isNegative())
      return countMinLeadingOnes();
    // Every value has at least 1 sign bit.
    return 1;
  }

  /// Returns the maximum significant bit width of possible signed values.
  ///
  /// This is the inverse of the minimum number of known sign bits. Examples for
  /// bitwidth 5:
  /// 110?? --> 4
  /// 0000? --> 2
  /// @return The maximum significant bit width of possible signed values.
  unsigned countMaxSignificantBits() const {
    return getBitWidth() - countMinSignBits() + 1;
  }

  /// Returns the maximum number of trailing zero bits possible.
  /// @return The maximum number of trailing zero bits possible.
  unsigned countMaxTrailingZeros() const { return One.countr_zero(); }

  /// Returns the maximum number of trailing one bits possible.
  /// @return The maximum number of trailing one bits possible.
  unsigned countMaxTrailingOnes() const { return Zero.countr_zero(); }

  /// Returns the maximum number of leading zero bits possible.
  /// @return The maximum number of leading zero bits possible.
  unsigned countMaxLeadingZeros() const { return One.countl_zero(); }

  /// Returns the maximum number of leading one bits possible.
  /// @return The maximum number of leading one bits possible.
  unsigned countMaxLeadingOnes() const { return Zero.countl_zero(); }

  /// Returns the number of bits known to be one.
  /// @return The number of bits known to be one.
  unsigned countMinPopulation() const { return One.popcount(); }

  /// Returns the maximum number of bits that could be one.
  /// @return The maximum number of bits that could be one.
  unsigned countMaxPopulation() const {
    return getBitWidth() - Zero.popcount();
  }

  /// Returns the maximum active bit width of possible unsigned values.
  ///
  /// This is the inverse of the minimum number of leading zeros.
  /// @return The maximum active bit width of possible unsigned values.
  unsigned countMaxActiveBits() const {
    return getBitWidth() - countMinLeadingZeros();
  }

  /// Create known bits from a known constant.
  ///
  /// \param C Constant value whose bits are all known.
  /// @return Known bits representing the constant C.
  static KnownBits makeConstant(const APInt &C) {
    return KnownBits(~C, C);
  }

  /// Returns KnownBits information that is known to be true for both this and
  /// RHS.
  ///
  /// When an operation is known to return one of its operands, this can be used
  /// to combine information about the known bits of the operands to get the
  /// information that must be true about the result.
  ///
  /// \param RHS Other known bits to intersect with.
  /// @return KnownBits information known to be true for both this and RHS.
  KnownBits intersectWith(const KnownBits &RHS) const {
    return KnownBits(Zero & RHS.Zero, One & RHS.One);
  }

  /// Returns KnownBits information that is known to be true for either this or
  /// RHS or both.
  ///
  /// This can be used to combine different sources of information about the
  /// known bits of a single value, e.g. information about the low bits and the
  /// high bits of the result of a multiplication.
  ///
  /// \param RHS Other known bits to union with.
  /// @return KnownBits information known to be true for either this or RHS or both.
  KnownBits unionWith(const KnownBits &RHS) const {
    return KnownBits(Zero | RHS.Zero, One | RHS.One);
  }

  /// Return true if LHS and RHS have no common bits set.
  ///
  /// \param LHS Left-hand known bits.
  /// \param RHS Right-hand known bits.
  /// @return True if LHS and RHS have no common bits set.
  static bool haveNoCommonBitsSet(const KnownBits &LHS, const KnownBits &RHS) {
    return (LHS.Zero | RHS.Zero).isAllOnes();
  }

  /// Compute known bits resulting from adding LHS, RHS and a 1-bit Carry.
  ///
  /// \param LHS Left-hand addend known bits.
  /// \param RHS Right-hand addend known bits.
  /// \param Carry One-bit carry-in known bits.
  /// @return Known bits resulting from adding LHS, RHS, and Carry.
  LLVM_ABI static KnownBits computeForAddCarry(const KnownBits &LHS,
                                               const KnownBits &RHS,
                                               const KnownBits &Carry);

  /// Compute known bits resulting from adding LHS and RHS.
  ///
  /// \param Add True to add, false to subtract.
  /// \param NSW True if the operation is known not to wrap signed.
  /// \param NUW True if the operation is known not to wrap unsigned.
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits resulting from adding or subtracting LHS and RHS.
  LLVM_ABI static KnownBits computeForAddSub(bool Add, bool NSW, bool NUW,
                                             const KnownBits &LHS,
                                             const KnownBits &RHS);

  /// Compute known bits results from subtracting RHS from LHS with 1-bit
  /// Borrow.
  ///
  /// \param LHS Minuend known bits.
  /// \param RHS Subtrahend known bits.
  /// \param Borrow One-bit borrow-in known bits.
  /// @return Known bits resulting from subtracting RHS from LHS with Borrow.
  LLVM_ABI static KnownBits computeForSubBorrow(const KnownBits &LHS,
                                                KnownBits RHS,
                                                const KnownBits &Borrow);

  /// Compute knownbits resulting from addition of LHS and RHS.
  ///
  /// \param LHS Left-hand addend known bits.
  /// \param RHS Right-hand addend known bits.
  /// \param NSW True if the addition is known not to wrap signed.
  /// \param NUW True if the addition is known not to wrap unsigned.
  /// \param SelfAdd True if this is ADD(X,X), which is equivalent to SHL(X,1).
  /// @return Known bits resulting from the addition.
  static KnownBits add(const KnownBits &LHS, const KnownBits &RHS,
                       bool NSW = false, bool NUW = false,
                       bool SelfAdd = false) {
    // ADD(X,X) is equivalent to SHL(X,1), the low bit is always zero.
    if (SelfAdd) {
      // Shift amount bitwidth is independent of src bitwidth (and we're
      // just shifting by one so don't have any bounds issues).
      assert(LHS == RHS && "Expected matching knownbits");
      KnownBits Amt = KnownBits::makeConstant(APInt(8, 1));
      return KnownBits::shl(LHS, Amt, NUW, NSW, /*ShAmtNonZero=*/true);
    }
    return computeForAddSub(/*Add=*/true, NSW, NUW, LHS, RHS);
  }

  /// Compute knownbits resulting from subtraction of LHS and RHS.
  ///
  /// \param LHS Minuend known bits.
  /// \param RHS Subtrahend known bits.
  /// \param NSW True if the subtraction is known not to wrap signed.
  /// \param NUW True if the subtraction is known not to wrap unsigned.
  /// @return Known bits resulting from the subtraction.
  static KnownBits sub(const KnownBits &LHS, const KnownBits &RHS,
                       bool NSW = false, bool NUW = false) {
    return computeForAddSub(/*Add=*/false, NSW, NUW, LHS, RHS);
  }

  /// Compute knownbits resulting from llvm.sadd.sat(LHS, RHS)
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits resulting from llvm.sadd.sat(LHS, RHS).
  LLVM_ABI static KnownBits sadd_sat(const KnownBits &LHS,
                                     const KnownBits &RHS);

  /// Compute knownbits resulting from llvm.uadd.sat(LHS, RHS)
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits resulting from llvm.uadd.sat(LHS, RHS).
  LLVM_ABI static KnownBits uadd_sat(const KnownBits &LHS,
                                     const KnownBits &RHS);

  /// Compute knownbits resulting from llvm.ssub.sat(LHS, RHS)
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits resulting from llvm.ssub.sat(LHS, RHS).
  LLVM_ABI static KnownBits ssub_sat(const KnownBits &LHS,
                                     const KnownBits &RHS);

  /// Compute knownbits resulting from llvm.usub.sat(LHS, RHS)
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits resulting from llvm.usub.sat(LHS, RHS).
  LLVM_ABI static KnownBits usub_sat(const KnownBits &LHS,
                                     const KnownBits &RHS);

  /// Compute knownbits resulting from APIntOps::avgFloorS
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits resulting from APIntOps::avgFloorS.
  LLVM_ABI static KnownBits avgFloorS(const KnownBits &LHS,
                                      const KnownBits &RHS);

  /// Compute knownbits resulting from APIntOps::avgFloorU
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits resulting from APIntOps::avgFloorU.
  LLVM_ABI static KnownBits avgFloorU(const KnownBits &LHS,
                                      const KnownBits &RHS);

  /// Compute knownbits resulting from APIntOps::avgCeilS
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits resulting from APIntOps::avgCeilS.
  LLVM_ABI static KnownBits avgCeilS(const KnownBits &LHS,
                                     const KnownBits &RHS);

  /// Compute knownbits resulting from APIntOps::avgCeilU
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits resulting from APIntOps::avgCeilU.
  LLVM_ABI static KnownBits avgCeilU(const KnownBits &LHS,
                                     const KnownBits &RHS);

  /// Compute known bits resulting from multiplying LHS and RHS.
  ///
  /// \param LHS Left-hand factor known bits.
  /// \param RHS Right-hand factor known bits.
  /// \param NoUndefSelfMultiply True if multiplying a value by itself is known
  ///        not to produce poison or undef.
  /// @return Known bits resulting from multiplying LHS and RHS.
  LLVM_ABI static KnownBits mul(const KnownBits &LHS, const KnownBits &RHS,
                                bool NoUndefSelfMultiply = false);

  /// Compute known bits from sign-extended multiply-hi.
  ///
  /// \param LHS Left-hand factor known bits.
  /// \param RHS Right-hand factor known bits.
  /// @return Known bits from sign-extended multiply-hi.
  LLVM_ABI static KnownBits mulhs(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits from zero-extended multiply-hi.
  ///
  /// \param LHS Left-hand factor known bits.
  /// \param RHS Right-hand factor known bits.
  /// @return Known bits from zero-extended multiply-hi.
  LLVM_ABI static KnownBits mulhu(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for sdiv(LHS, RHS).
  ///
  /// \param LHS Dividend known bits.
  /// \param RHS Divisor known bits.
  /// \param Exact True if the division is known to have no remainder.
  /// @return Known bits for sdiv(LHS, RHS).
  LLVM_ABI static KnownBits sdiv(const KnownBits &LHS, const KnownBits &RHS,
                                 bool Exact = false);

  /// Compute known bits for udiv(LHS, RHS).
  ///
  /// \param LHS Dividend known bits.
  /// \param RHS Divisor known bits.
  /// \param Exact True if the division is known to have no remainder.
  /// @return Known bits for udiv(LHS, RHS).
  LLVM_ABI static KnownBits udiv(const KnownBits &LHS, const KnownBits &RHS,
                                 bool Exact = false);

  /// Compute known bits for urem(LHS, RHS).
  ///
  /// \param LHS Dividend known bits.
  /// \param RHS Divisor known bits.
  /// @return Known bits for urem(LHS, RHS).
  LLVM_ABI static KnownBits urem(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for srem(LHS, RHS).
  ///
  /// \param LHS Dividend known bits.
  /// \param RHS Divisor known bits.
  /// @return Known bits for srem(LHS, RHS).
  LLVM_ABI static KnownBits srem(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for umax(LHS, RHS).
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits for umax(LHS, RHS).
  LLVM_ABI static KnownBits umax(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for umin(LHS, RHS).
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits for umin(LHS, RHS).
  LLVM_ABI static KnownBits umin(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for smax(LHS, RHS).
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits for smax(LHS, RHS).
  LLVM_ABI static KnownBits smax(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for smin(LHS, RHS).
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits for smin(LHS, RHS).
  LLVM_ABI static KnownBits smin(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for abdu(LHS, RHS).
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits for abdu(LHS, RHS).
  LLVM_ABI static KnownBits abdu(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for abds(LHS, RHS).
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits for abds(LHS, RHS).
  LLVM_ABI static KnownBits abds(KnownBits LHS, KnownBits RHS);

  /// Compute known bits for shl(LHS, RHS).
  /// NOTE: RHS (shift amount) bitwidth doesn't need to be the same as LHS.
  ///
  /// \param LHS Value being shifted.
  /// \param RHS Shift amount known bits.
  /// \param NUW True if the shift is known not to wrap unsigned.
  /// \param NSW True if the shift is known not to wrap signed.
  /// \param ShAmtNonZero True if the shift amount is known to be non-zero.
  /// @return Known bits for shl(LHS, RHS).
  LLVM_ABI static KnownBits shl(const KnownBits &LHS, const KnownBits &RHS,
                                bool NUW = false, bool NSW = false,
                                bool ShAmtNonZero = false);

  /// Compute known bits for lshr(LHS, RHS).
  /// NOTE: RHS (shift amount) bitwidth doesn't need to be the same as LHS.
  ///
  /// \param LHS Value being shifted.
  /// \param RHS Shift amount known bits.
  /// \param ShAmtNonZero True if the shift amount is known to be non-zero.
  /// \param Exact True if bits shifted out are known to be zero.
  /// @return Known bits for lshr(LHS, RHS).
  LLVM_ABI static KnownBits lshr(const KnownBits &LHS, const KnownBits &RHS,
                                 bool ShAmtNonZero = false, bool Exact = false);

  /// Compute known bits for ashr(LHS, RHS).
  /// NOTE: RHS (shift amount) bitwidth doesn't need to be the same as LHS.
  ///
  /// \param LHS Value being shifted.
  /// \param RHS Shift amount known bits.
  /// \param ShAmtNonZero True if the shift amount is known to be non-zero.
  /// \param Exact True if bits shifted out are known to be zero.
  /// @return Known bits for ashr(LHS, RHS).
  LLVM_ABI static KnownBits ashr(const KnownBits &LHS, const KnownBits &RHS,
                                 bool ShAmtNonZero = false, bool Exact = false);

  /// Compute known bits for fshl(LHS, RHS, Amt).
  ///
  /// \param LHS High half of the concatenated value.
  /// \param RHS Low half of the concatenated value.
  /// \param Amt Funnel-shift amount.
  /// @return Known bits for fshl(LHS, RHS, Amt).
  LLVM_ABI static KnownBits fshl(const KnownBits &LHS, const KnownBits &RHS,
                                 const APInt &Amt);

  /// Compute known bits for fshr(LHS, RHS, Amt).
  ///
  /// \param LHS High half of the concatenated value.
  /// \param RHS Low half of the concatenated value.
  /// \param Amt Funnel-shift amount.
  /// @return Known bits for fshr(LHS, RHS, Amt).
  LLVM_ABI static KnownBits fshr(const KnownBits &LHS, const KnownBits &RHS,
                                 const APInt &Amt);

  /// Compute known bits for clmul(LHS, RHS).
  ///
  /// \param LHS Left-hand operand known bits.
  /// \param RHS Right-hand operand known bits.
  /// @return Known bits for clmul(LHS, RHS).
  LLVM_ABI static KnownBits clmul(const KnownBits &LHS, const KnownBits &RHS);

  /// Compute known bits for pext(Val, Mask).
  ///
  /// \param Val Source value known bits.
  /// \param Mask Parallel extract mask known bits.
  /// @return Known bits for pext(Val, Mask).
  LLVM_ABI static KnownBits pext(const KnownBits &Val, const KnownBits &Mask);

  /// Compute known bits for pdep(Val, Mask).
  ///
  /// \param Val Source value known bits.
  /// \param Mask Parallel deposit mask known bits.
  /// @return Known bits for pdep(Val, Mask).
  LLVM_ABI static KnownBits pdep(const KnownBits &Val, const KnownBits &Mask);

  /// Determine if these known bits always give the same ICMP_EQ result.
  ///
  /// \param LHS Left-hand known bits.
  /// \param RHS Right-hand known bits.
  /// @return The constant ICMP_EQ result if always the same, otherwise std::nullopt.
  LLVM_ABI static std::optional<bool> eq(const KnownBits &LHS,
                                         const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_NE result.
  ///
  /// \param LHS Left-hand known bits.
  /// \param RHS Right-hand known bits.
  /// @return The constant ICMP_NE result if always the same, otherwise std::nullopt.
  LLVM_ABI static std::optional<bool> ne(const KnownBits &LHS,
                                         const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_UGT result.
  ///
  /// \param LHS Left-hand known bits.
  /// \param RHS Right-hand known bits.
  /// @return The constant ICMP_UGT result if always the same, otherwise std::nullopt.
  LLVM_ABI static std::optional<bool> ugt(const KnownBits &LHS,
                                          const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_UGE result.
  ///
  /// \param LHS Left-hand known bits.
  /// \param RHS Right-hand known bits.
  /// @return The constant ICMP_UGE result if always the same, otherwise std::nullopt.
  LLVM_ABI static std::optional<bool> uge(const KnownBits &LHS,
                                          const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_ULT result.
  ///
  /// \param LHS Left-hand known bits.
  /// \param RHS Right-hand known bits.
  /// @return The constant ICMP_ULT result if always the same, otherwise std::nullopt.
  LLVM_ABI static std::optional<bool> ult(const KnownBits &LHS,
                                          const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_ULE result.
  ///
  /// \param LHS Left-hand known bits.
  /// \param RHS Right-hand known bits.
  /// @return The constant ICMP_ULE result if always the same, otherwise std::nullopt.
  LLVM_ABI static std::optional<bool> ule(const KnownBits &LHS,
                                          const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_SGT result.
  ///
  /// \param LHS Left-hand known bits.
  /// \param RHS Right-hand known bits.
  /// @return The constant ICMP_SGT result if always the same, otherwise std::nullopt.
  LLVM_ABI static std::optional<bool> sgt(const KnownBits &LHS,
                                          const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_SGE result.
  ///
  /// \param LHS Left-hand known bits.
  /// \param RHS Right-hand known bits.
  /// @return The constant ICMP_SGE result if always the same, otherwise std::nullopt.
  LLVM_ABI static std::optional<bool> sge(const KnownBits &LHS,
                                          const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_SLT result.
  ///
  /// \param LHS Left-hand known bits.
  /// \param RHS Right-hand known bits.
  /// @return The constant ICMP_SLT result if always the same, otherwise std::nullopt.
  LLVM_ABI static std::optional<bool> slt(const KnownBits &LHS,
                                          const KnownBits &RHS);

  /// Determine if these known bits always give the same ICMP_SLE result.
  ///
  /// \param LHS Left-hand known bits.
  /// \param RHS Right-hand known bits.
  /// @return The constant ICMP_SLE result if always the same, otherwise std::nullopt.
  LLVM_ABI static std::optional<bool> sle(const KnownBits &LHS,
                                          const KnownBits &RHS);

  /// Update known bits based on ANDing with RHS.
  ///
  /// \param RHS Known bits to AND with.
  /// @return Reference to this KnownBits after the AND update.
  LLVM_ABI KnownBits &operator&=(const KnownBits &RHS);

  /// Update known bits based on ORing with RHS.
  ///
  /// \param RHS Known bits to OR with.
  /// @return Reference to this KnownBits after the OR update.
  LLVM_ABI KnownBits &operator|=(const KnownBits &RHS);

  /// Update known bits based on XORing with RHS.
  ///
  /// \param RHS Known bits to XOR with.
  /// @return Reference to this KnownBits after the XOR update.
  LLVM_ABI KnownBits &operator^=(const KnownBits &RHS);

  /// Shift known bits left by ShAmt. Shift in bits are unknown.
  ///
  /// \param ShAmt Number of bits to shift left.
  /// @return Reference to this KnownBits after the left shift.
  KnownBits &operator<<=(unsigned ShAmt) {
    Zero <<= ShAmt;
    One <<= ShAmt;
    return *this;
  }

  /// Shift known bits right by ShAmt. Shifted in bits are unknown.
  ///
  /// \param ShAmt Number of bits to shift right.
  /// @return Reference to this KnownBits after the right shift.
  KnownBits &operator>>=(unsigned ShAmt) {
    Zero.lshrInPlace(ShAmt);
    One.lshrInPlace(ShAmt);
    return *this;
  }

  /// Compute known bits for the absolute value.
  ///
  /// \param IntMinIsPoison True if abs of the signed minimum is poison.
  /// @return Known bits for the absolute value.
  LLVM_ABI KnownBits abs(bool IntMinIsPoison = false) const;

  /// Compute known bits for horizontal add for a vector with NumElts
  /// elements, where each element has the known bits represented by this
  /// object.
  ///
  /// \param NumElts Number of vector elements in the reduction.
  /// @return Known bits for the horizontal add reduction.
  LLVM_ABI KnownBits reduceAdd(unsigned NumElts) const;

  /// Return known bits with bytes swapped.
  /// @return Known bits with bytes swapped.
  KnownBits byteSwap() const {
    return KnownBits(Zero.byteSwap(), One.byteSwap());
  }

  /// Return known bits with bit order reversed.
  /// @return Known bits with bit order reversed.
  KnownBits reverseBits() const {
    return KnownBits(Zero.reverseBits(), One.reverseBits());
  }

  /// Compute known bits for X & -X, which has only the lowest bit set of X set.
  /// The name comes from the X86 BMI instruction
  /// @return Known bits for X & -X.
  LLVM_ABI KnownBits blsi() const;

  /// Compute known bits for X ^ (X - 1), which has all bits up to and including
  /// the lowest set bit of X set. The name comes from the X86 BMI instruction.
  /// @return Known bits for X ^ (X - 1).
  LLVM_ABI KnownBits blsmsk() const;

  /// Return true if this and Other have the same known zeros and ones.
  ///
  /// \param Other Known bits to compare against.
  /// @return True if this and Other have the same known zeros and ones.
  bool operator==(const KnownBits &Other) const {
    return Zero == Other.Zero && One == Other.One;
  }

  /// Return true if this and Other differ in known zeros or ones.
  ///
  /// \param Other Known bits to compare against.
  /// @return True if this and Other differ in known zeros or ones.
  bool operator!=(const KnownBits &Other) const { return !(*this == Other); }

  /// Print this KnownBits to OS.
  ///
  /// \param OS Stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this KnownBits to stderr for debugging.
  LLVM_DUMP_METHOD void dump() const;
#endif

private:
  // Internal helper for getting the initial KnownBits for an `srem` or `urem`
  // operation with the low-bits set.
  static KnownBits remGetLowBits(const KnownBits &LHS, const KnownBits &RHS);
};

/// Helpers for analyzing sign-bit count through rotate operations.
namespace SignBitsOps {

/// Compute the number of sign bits after rotating a value.
///
/// \param SrcSignBits Number of sign bits in the unrotated value.
/// \param BitWidth Bit width of the value being rotated.
/// \param RotAmt Optional rotate amount; unknown when empty.
/// \param IsRotateRight True for rotate-right, false for rotate-left.
/// @return The number of sign bits after the rotation.
LLVM_ABI unsigned rot(unsigned SrcSignBits, unsigned BitWidth,
                      std::optional<APInt> RotAmt, bool IsRotateRight);

} // end namespace SignBitsOps

/// Bitwise AND of two KnownBits values.
///
/// \param LHS Left-hand known bits (taken by value).
/// \param RHS Right-hand known bits.
/// @return Known bits of the bitwise AND.
inline KnownBits operator&(KnownBits LHS, const KnownBits &RHS) {
  LHS &= RHS;
  return LHS;
}

/// Bitwise AND of two KnownBits values.
///
/// \param LHS Left-hand known bits.
/// \param RHS Right-hand known bits (taken by rvalue reference).
/// @return Known bits of the bitwise AND.
inline KnownBits operator&(const KnownBits &LHS, KnownBits &&RHS) {
  RHS &= LHS;
  return std::move(RHS);
}

/// Bitwise OR of two KnownBits values.
///
/// \param LHS Left-hand known bits (taken by value).
/// \param RHS Right-hand known bits.
/// @return Known bits of the bitwise OR.
inline KnownBits operator|(KnownBits LHS, const KnownBits &RHS) {
  LHS |= RHS;
  return LHS;
}

/// Bitwise OR of two KnownBits values.
///
/// \param LHS Left-hand known bits.
/// \param RHS Right-hand known bits (taken by rvalue reference).
/// @return Known bits of the bitwise OR.
inline KnownBits operator|(const KnownBits &LHS, KnownBits &&RHS) {
  RHS |= LHS;
  return std::move(RHS);
}

/// Bitwise XOR of two KnownBits values.
///
/// \param LHS Left-hand known bits (taken by value).
/// \param RHS Right-hand known bits.
/// @return Known bits of the bitwise XOR.
inline KnownBits operator^(KnownBits LHS, const KnownBits &RHS) {
  LHS ^= RHS;
  return LHS;
}

/// Bitwise XOR of two KnownBits values.
///
/// \param LHS Left-hand known bits.
/// \param RHS Right-hand known bits (taken by rvalue reference).
/// @return Known bits of the bitwise XOR.
inline KnownBits operator^(const KnownBits &LHS, KnownBits &&RHS) {
  RHS ^= LHS;
  return std::move(RHS);
}

/// Print Known to OS.
///
/// \param OS Stream to print to.
/// \param Known Known bits to print.
/// @return The output stream OS.
inline raw_ostream &operator<<(raw_ostream &OS, const KnownBits &Known) {
  Known.print(OS);
  return OS;
}

} // end namespace llvm

#endif
