//===-- llvm/ADT/APSInt.h - Arbitrary Precision Signed Int -----*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the APSInt class, which is a simple class that
/// represents an arbitrary sized integer that knows its signedness.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_APSINT_H
#define LLVM_ADT_APSINT_H

#include "llvm/ADT/APInt.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// An arbitrary precision integer that knows its signedness.
class [[nodiscard]] APSInt : public APInt {
  bool IsUnsigned = false;

public:
  /// Default constructor that creates an uninitialized APInt.
  explicit APSInt() = default;

  /// Create an APSInt with the specified width, default to unsigned.
  explicit APSInt(uint32_t BitWidth, bool isUnsigned = true)
      : APInt(BitWidth, 0), IsUnsigned(isUnsigned) {}

  /// Construct from an APInt and a signedness flag.
  ///
  /// \param I the bit pattern to adopt
  /// \param isUnsigned true if the value should be treated as unsigned
  explicit APSInt(APInt I, bool isUnsigned = true)
      : APInt(std::move(I)), IsUnsigned(isUnsigned) {}

  /// Construct an APSInt from a string representation.
  ///
  /// This constructor interprets the string \p Str using the radix of 10.
  /// The interpretation stops at the end of the string. The bit width of the
  /// constructed APSInt is determined automatically.
  ///
  /// \param Str the string to be interpreted.
  LLVM_ABI explicit APSInt(StringRef Str);

  /// Determine sign of this APSInt.
  ///
  /// \returns true if this APSInt is negative, false otherwise
  bool isNegative() const { return isSigned() && APInt::isNegative(); }

  /// Determine if this APSInt Value is non-negative (>= 0)
  ///
  /// \returns true if this APSInt is non-negative, false otherwise
  bool isNonNegative() const { return !isNegative(); }

  /// Determine if this APSInt Value is positive.
  ///
  /// This tests if the value of this APSInt is positive (> 0). Note
  /// that 0 is not a positive value.
  ///
  /// \returns true if this APSInt is positive.
  bool isStrictlyPositive() const { return isNonNegative() && !isZero(); }

  /// Assign from an APInt while retaining signedness.
  APSInt &operator=(APInt RHS) {
    // Retain our current sign.
    APInt::operator=(std::move(RHS));
    return *this;
  }

  /// Assign from a 64-bit value while retaining signedness.
  APSInt &operator=(uint64_t RHS) {
    // Retain our current sign.
    APInt::operator=(RHS);
    return *this;
  }

  /// Return true if this APSInt is signed.
  bool isSigned() const { return !IsUnsigned; }
  /// Return true if this APSInt is unsigned.
  bool isUnsigned() const { return IsUnsigned; }
  /// Set whether this APSInt is treated as unsigned.
  void setIsUnsigned(bool Val) { IsUnsigned = Val; }
  /// Set whether this APSInt is treated as signed.
  void setIsSigned(bool Val) { IsUnsigned = !Val; }

  /// Append this APSInt to the specified SmallString.
  void toString(SmallVectorImpl<char> &Str, unsigned Radix = 10) const {
    APInt::toString(Str, Radix, isSigned());
  }
  /// Import APInt string conversion overloads.
  using APInt::toString;

  /// If this int is representable using an int64_t.
  bool isRepresentableByInt64() const {
    // For unsigned values with 64 active bits, they technically fit into a
    // int64_t, but the user may get negative numbers and has to manually cast
    // them to unsigned. Let's not bet the user has the sanity to do that and
    // not give them a vague value at the first place.
    return isSigned() ? isSignedIntN(64) : isIntN(63);
  }

  /// Get the correctly-extended \c int64_t value.
  int64_t getExtValue() const {
    assert(isRepresentableByInt64() && "Too many bits for int64_t");
    return isSigned() ? getSExtValue() : getZExtValue();
  }

  /// Try to get a correctly-extended int64_t value.
  ///
  /// \returns the value if representable in int64_t, otherwise nullopt.
  std::optional<int64_t> tryExtValue() const {
    return isRepresentableByInt64() ? std::optional<int64_t>(getExtValue())
                                    : std::nullopt;
  }

  /// Truncate to \p width bits, preserving signedness.
  APSInt trunc(uint32_t width) const {
    return APSInt(APInt::trunc(width), IsUnsigned);
  }

  /// Zero- or sign-extend to \p width, preserving signedness.
  APSInt extend(uint32_t width) const {
    if (IsUnsigned)
      return APSInt(zext(width), IsUnsigned);
    return APSInt(sext(width), IsUnsigned);
  }

  /// Extend or truncate to \p width, preserving signedness.
  APSInt extOrTrunc(uint32_t width) const {
    if (IsUnsigned)
      return APSInt(zextOrTrunc(width), IsUnsigned);
    return APSInt(sextOrTrunc(width), IsUnsigned);
  }

  /// Remainder-assign using signed or unsigned rules.
  const APSInt &operator%=(const APSInt &RHS) {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    if (IsUnsigned)
      *this = urem(RHS);
    else
      *this = srem(RHS);
    return *this;
  }
  /// Divide-assign using signed or unsigned rules.
  const APSInt &operator/=(const APSInt &RHS) {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    if (IsUnsigned)
      *this = udiv(RHS);
    else
      *this = sdiv(RHS);
    return *this;
  }
  /// Remainder using signed or unsigned rules.
  APSInt operator%(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return IsUnsigned ? APSInt(urem(RHS), true) : APSInt(srem(RHS), false);
  }
  /// Divide using signed or unsigned rules.
  APSInt operator/(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return IsUnsigned ? APSInt(udiv(RHS), true) : APSInt(sdiv(RHS), false);
  }

  /// Arithmetic or logical right shift based on signedness.
  APSInt operator>>(unsigned Amt) const {
    return IsUnsigned ? APSInt(lshr(Amt), true) : APSInt(ashr(Amt), false);
  }
  /// Right-shift-assign based on signedness.
  APSInt &operator>>=(unsigned Amt) {
    if (IsUnsigned)
      lshrInPlace(Amt);
    else
      ashrInPlace(Amt);
    return *this;
  }
  /// Relative right shift based on signedness.
  APSInt relativeShr(unsigned Amt) const {
    return IsUnsigned ? APSInt(relativeLShr(Amt), true)
                      : APSInt(relativeAShr(Amt), false);
  }

  /// Less-than comparison using this APSInt's signedness.
  inline bool operator<(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return IsUnsigned ? ult(RHS) : slt(RHS);
  }
  /// Greater-than comparison using this APSInt's signedness.
  inline bool operator>(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return IsUnsigned ? ugt(RHS) : sgt(RHS);
  }
  /// Less-or-equal comparison using this APSInt's signedness.
  inline bool operator<=(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return IsUnsigned ? ule(RHS) : sle(RHS);
  }
  /// Greater-or-equal comparison using this APSInt's signedness.
  inline bool operator>=(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return IsUnsigned ? uge(RHS) : sge(RHS);
  }
  /// Equality comparison of two APSInts.
  inline bool operator==(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return eq(RHS);
  }
  /// Inequality comparison of two APSInts.
  inline bool operator!=(const APSInt &RHS) const { return !((*this) == RHS); }

  /// Compare with a signed 64-bit value for equality.
  bool operator==(int64_t RHS) const {
    return compareValues(*this, get(RHS)) == 0;
  }
  /// Compare with a signed 64-bit value for inequality.
  bool operator!=(int64_t RHS) const {
    return compareValues(*this, get(RHS)) != 0;
  }
  /// Compare with a signed 64-bit value for less-or-equal.
  bool operator<=(int64_t RHS) const {
    return compareValues(*this, get(RHS)) <= 0;
  }
  /// Compare with a signed 64-bit value for greater-or-equal.
  bool operator>=(int64_t RHS) const {
    return compareValues(*this, get(RHS)) >= 0;
  }
  /// Compare with a signed 64-bit value for less-than.
  bool operator<(int64_t RHS) const {
    return compareValues(*this, get(RHS)) < 0;
  }
  /// Compare with a signed 64-bit value for greater-than.
  bool operator>(int64_t RHS) const {
    return compareValues(*this, get(RHS)) > 0;
  }

  // The remaining operators just wrap the logic of APInt, but retain the
  // signedness information.

  /// Left shift, retaining signedness.
  APSInt operator<<(unsigned Bits) const {
    return APSInt(static_cast<const APInt &>(*this) << Bits, IsUnsigned);
  }
  /// Left-shift-assign, retaining signedness.
  APSInt &operator<<=(unsigned Amt) {
    static_cast<APInt &>(*this) <<= Amt;
    return *this;
  }
  /// Relative left shift based on signedness.
  APSInt relativeShl(unsigned Amt) const {
    return IsUnsigned ? APSInt(relativeLShl(Amt), true)
                      : APSInt(relativeAShl(Amt), false);
  }

  /// Pre-increment.
  APSInt &operator++() {
    ++(static_cast<APInt &>(*this));
    return *this;
  }
  /// Pre-decrement.
  APSInt &operator--() {
    --(static_cast<APInt &>(*this));
    return *this;
  }
  /// Post-increment, returning the value before the increment.
  APSInt operator++(int) {
    return APSInt(++static_cast<APInt &>(*this), IsUnsigned);
  }
  /// Post-decrement, returning the value before the decrement.
  APSInt operator--(int) {
    return APSInt(--static_cast<APInt &>(*this), IsUnsigned);
  }
  /// Unary negation, retaining signedness.
  APSInt operator-() const {
    return APSInt(-static_cast<const APInt &>(*this), IsUnsigned);
  }
  /// Add-assign another APSInt.
  APSInt &operator+=(const APSInt &RHS) {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    static_cast<APInt &>(*this) += RHS;
    return *this;
  }
  /// Subtract-assign another APSInt.
  APSInt &operator-=(const APSInt &RHS) {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    static_cast<APInt &>(*this) -= RHS;
    return *this;
  }
  /// Multiply-assign another APSInt.
  APSInt &operator*=(const APSInt &RHS) {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    static_cast<APInt &>(*this) *= RHS;
    return *this;
  }
  /// Bitwise AND-assign another APSInt.
  APSInt &operator&=(const APSInt &RHS) {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    static_cast<APInt &>(*this) &= RHS;
    return *this;
  }
  /// Bitwise OR-assign another APSInt.
  APSInt &operator|=(const APSInt &RHS) {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    static_cast<APInt &>(*this) |= RHS;
    return *this;
  }
  /// Bitwise XOR-assign another APSInt.
  APSInt &operator^=(const APSInt &RHS) {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    static_cast<APInt &>(*this) ^= RHS;
    return *this;
  }

  /// Bitwise AND of two APSInts.
  APSInt operator&(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return APSInt(static_cast<const APInt &>(*this) & RHS, IsUnsigned);
  }

  /// Bitwise OR of two APSInts.
  APSInt operator|(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return APSInt(static_cast<const APInt &>(*this) | RHS, IsUnsigned);
  }

  /// Bitwise XOR of two APSInts.
  APSInt operator^(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return APSInt(static_cast<const APInt &>(*this) ^ RHS, IsUnsigned);
  }

  /// Multiply two APSInts.
  APSInt operator*(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return APSInt(static_cast<const APInt &>(*this) * RHS, IsUnsigned);
  }
  /// Add two APSInts.
  APSInt operator+(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return APSInt(static_cast<const APInt &>(*this) + RHS, IsUnsigned);
  }
  /// Subtract two APSInts.
  APSInt operator-(const APSInt &RHS) const {
    assert(IsUnsigned == RHS.IsUnsigned && "Signedness mismatch!");
    return APSInt(static_cast<const APInt &>(*this) - RHS, IsUnsigned);
  }
  /// Bitwise complement, retaining signedness.
  APSInt operator~() const {
    return APSInt(~static_cast<const APInt &>(*this), IsUnsigned);
  }

  /// Return the APSInt representing the maximum integer value with the given
  /// bit width and signedness.
  static APSInt getMaxValue(uint32_t numBits, bool Unsigned) {
    return APSInt(Unsigned ? APInt::getMaxValue(numBits)
                           : APInt::getSignedMaxValue(numBits),
                  Unsigned);
  }

  /// Return the APSInt representing the minimum integer value with the given
  /// bit width and signedness.
  static APSInt getMinValue(uint32_t numBits, bool Unsigned) {
    return APSInt(Unsigned ? APInt::getMinValue(numBits)
                           : APInt::getSignedMinValue(numBits),
                  Unsigned);
  }

  /// Determine if two APSInts have the same value, zero- or
  /// sign-extending as needed.
  static bool isSameValue(const APSInt &I1, const APSInt &I2) {
    return !compareValues(I1, I2);
  }

  /// Compare underlying values of two numbers.
  static int compareValues(const APSInt &I1, const APSInt &I2) {
    if (I1.getBitWidth() == I2.getBitWidth() && I1.isSigned() == I2.isSigned())
      return I1.IsUnsigned ? I1.compare(I2) : I1.compareSigned(I2);

    // Check for a bit-width mismatch.
    if (I1.getBitWidth() > I2.getBitWidth())
      return compareValues(I1, I2.extend(I1.getBitWidth()));
    if (I2.getBitWidth() > I1.getBitWidth())
      return compareValues(I1.extend(I2.getBitWidth()), I2);

    // We have a signedness mismatch. Check for negative values and do an
    // unsigned compare if both are positive.
    if (I1.isSigned()) {
      assert(!I2.isSigned() && "Expected signed mismatch");
      if (I1.isNegative())
        return -1;
    } else {
      assert(I2.isSigned() && "Expected signed mismatch");
      if (I2.isNegative())
        return 1;
    }

    return I1.compare(I2);
  }

  /// Create a signed 64-bit APSInt from \p X.
  static APSInt get(int64_t X) { return APSInt(APInt(64, X), false); }
  /// Create an unsigned 64-bit APSInt from \p X.
  static APSInt getUnsigned(uint64_t X) { return APSInt(APInt(64, X), true); }

  /// Used to insert APSInt objects, or objects that contain APSInt objects,
  /// into FoldingSets.
  LLVM_ABI void Profile(FoldingSetNodeID &ID) const;
};

/// Compare a signed 64-bit value with an APSInt for equality.
inline bool operator==(int64_t V1, const APSInt &V2) { return V2 == V1; }
/// Compare a signed 64-bit value with an APSInt for inequality.
inline bool operator!=(int64_t V1, const APSInt &V2) { return V2 != V1; }
/// Compare a signed 64-bit value with an APSInt for less-or-equal.
inline bool operator<=(int64_t V1, const APSInt &V2) { return V2 >= V1; }
/// Compare a signed 64-bit value with an APSInt for greater-or-equal.
inline bool operator>=(int64_t V1, const APSInt &V2) { return V2 <= V1; }
/// Compare a signed 64-bit value with an APSInt for less-than.
inline bool operator<(int64_t V1, const APSInt &V2) { return V2 > V1; }
/// Compare a signed 64-bit value with an APSInt for greater-than.
inline bool operator>(int64_t V1, const APSInt &V2) { return V2 < V1; }

/// Print \p I to \p OS using its signedness.
inline raw_ostream &operator<<(raw_ostream &OS, const APSInt &I) {
  I.print(OS, I.isSigned());
  return OS;
}

/// Provide DenseMapInfo for APSInt, using the DenseMapInfo for APInt.
template <> struct DenseMapInfo<APSInt, void> {
  static unsigned getHashValue(const APSInt &Key) {
    return DenseMapInfo<APInt, void>::getHashValue(Key);
  }

  static bool isEqual(const APSInt &LHS, const APSInt &RHS) {
    return LHS.getBitWidth() == RHS.getBitWidth() &&
           LHS.isUnsigned() == RHS.isUnsigned() && LHS == RHS;
  }
};

} // end namespace llvm

#endif
