//===- APFixedPoint.h - Fixed point constant handling -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the fixed point number interface.
/// This is a class for abstracting various operations performed on fixed point
/// types.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_APFIXEDPOINT_H
#define LLVM_ADT_APFIXEDPOINT_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// Arbitrary-precision floating-point value. @seebelow
class APFloat;
/// Floating-point format description (exponent/mantissa layout). @seebelow
struct fltSemantics;

/// Fixed-point layout and saturation flags, similar to fltSemantics.
///
/// The width specifies the whole bit width of the underlying scaled integer
/// (with padding if any). The scale represents the number of fractional bits
/// in this type. When HasUnsignedPadding is true and this type is unsigned,
/// the first bit in the value this represents is treated as padding.
class FixedPointSemantics {
public:
  /// Bit-field width reserved for storing the scaled-integer bit width.
  static constexpr unsigned WidthBitWidth = 16;
  /// Bit-field width reserved for storing the LSB weight.
  static constexpr unsigned LsbWeightBitWidth = 13;
  /// Used to differentiate between constructors with Width and Lsb from the
  /// default Width and scale
  struct Lsb {
    /// Power-of-two weight of the least-significant bit (negative for fractional bits).
    int LsbWeight;
  };
  /// Construct semantics from a bit width, fractional scale, and flags.
  ///
  /// \param Width Total bit width of the scaled integer (including padding).
  /// \param Scale Number of fractional bits; LSB weight is \c -Scale.
  /// \param IsSigned Whether values use two's-complement signed representation.
  /// \param IsSaturated Whether arithmetic saturates instead of wrapping.
  /// \param HasUnsignedPadding Whether the high bit of unsigned values is padding.
  FixedPointSemantics(unsigned Width, unsigned Scale, bool IsSigned,
                      bool IsSaturated, bool HasUnsignedPadding)
      : FixedPointSemantics(Width, Lsb{-static_cast<int>(Scale)}, IsSigned,
                            IsSaturated, HasUnsignedPadding) {}
  /// Construct semantics from a bit width, explicit LSB weight, and flags.
  ///
  /// \param Width Total bit width of the scaled integer (including padding).
  /// \param Weight LSB weight wrapper; see \c Lsb::LsbWeight.
  /// \param IsSigned Whether values use two's-complement signed representation.
  /// \param IsSaturated Whether arithmetic saturates instead of wrapping.
  /// \param HasUnsignedPadding Whether the high bit of unsigned values is padding.
  FixedPointSemantics(unsigned Width, Lsb Weight, bool IsSigned,
                      bool IsSaturated, bool HasUnsignedPadding)
      : Width(Width), LsbWeight(Weight.LsbWeight), IsSigned(IsSigned),
        IsSaturated(IsSaturated), HasUnsignedPadding(HasUnsignedPadding) {
    assert(isUInt<WidthBitWidth>(Width) && isInt<LsbWeightBitWidth>(Weight.LsbWeight));
    assert(!(IsSigned && HasUnsignedPadding) &&
           "Cannot have unsigned padding on a signed type.");
  }

  /// Check if the Semantic follow the requirements of an older more limited
  /// version of this class
  /// @return True if the LSB weight is non-positive and the width covers the fractional range.
  bool isValidLegacySema() const {
    return LsbWeight <= 0 && static_cast<int>(Width) >= -LsbWeight;
  }
  /// Return the total bit width of the scaled integer.
  /// @return Total bit width of the scaled integer.
  unsigned getWidth() const { return Width; }
  /// Return the number of fractional bits (\c -LsbWeight) for legacy semantics.
  /// @return Number of fractional bits for legacy semantics.
  unsigned getScale() const { assert(isValidLegacySema()); return -LsbWeight; }
  /// Return the power-of-two weight of the least-significant bit.
  /// @return Power-of-two weight of the least-significant bit.
  int getLsbWeight() const { return LsbWeight; }
  /// Return the power-of-two weight of the most-significant value bit.
  /// @return Power-of-two weight of the most-significant value bit.
  int getMsbWeight() const {
    return LsbWeight + Width - 1 /*Both lsb and msb are both part of width*/;
  }
  /// Return true if values use a signed (two's-complement) representation.
  /// @return True if values use a signed representation.
  bool isSigned() const { return IsSigned; }
  /// Return true if arithmetic saturates on overflow instead of wrapping.
  /// @return True if arithmetic saturates on overflow.
  bool isSaturated() const { return IsSaturated; }
  /// Return true if unsigned values reserve the high bit as padding.
  /// @return True if unsigned values reserve the high bit as padding.
  bool hasUnsignedPadding() const { return HasUnsignedPadding; }

  /// Set whether arithmetic saturates on overflow.
  ///
  /// \param Saturated New saturation flag.
  void setSaturated(bool Saturated) { IsSaturated = Saturated; }

  /// return true if the first bit doesn't have a strictly positive weight
  /// @return True if the first bit is a sign or padding bit.
  bool hasSignOrPaddingBit() const { return IsSigned || HasUnsignedPadding; }

  /// Return the number of integral bits represented by these semantics. These
  /// are separate from the fractional bits and do not include the sign or
  /// padding bit.
  /// @return Number of integral bits excluding the sign or padding bit.
  unsigned getIntegralBits() const {
    return std::max(getMsbWeight() + 1 - hasSignOrPaddingBit(), 0);
  }

  /// Return semantics that can precisely represent both this and \p Other.
  ///
  /// This does not compute the resulting semantics for a given binary
  /// operation.
  ///
  /// \param Other Semantics to combine with.
  /// @return Semantics that can represent both this and \p Other.
  LLVM_ABI FixedPointSemantics
  getCommonSemantics(const FixedPointSemantics &Other) const;

  /// Print semantics for debug purposes.
  ///
  /// \param OS Destination stream.
  LLVM_ABI void print(llvm::raw_ostream &OS) const;

  /// Return true if these value bits fit in \p FloatSema without overflowing.
  ///
  /// Returns true if this fixed-point semantic with its value bits interpreted
  /// as an integer can fit in the given floating point semantic without
  /// overflowing to infinity.
  /// For example, a signed 8-bit fixed-point semantic has a maximum and
  /// minimum integer representation of 127 and -128, respectively. If both of
  /// these values can be represented (possibly inexactly) in the floating
  /// point semantic without overflowing, this returns true.
  ///
  /// \param FloatSema Floating-point format to test against.
  /// @return True if the value bits fit in \p FloatSema without overflowing.
  LLVM_ABI bool fitsInFloatSemantics(const fltSemantics &FloatSema) const;

  /// Return the FixedPointSemantics for an integer type.
  ///
  /// \param Width Total bit width of the integer.
  /// \param IsSigned Whether the integer is signed.
  /// @return Semantics describing an integer of the given width and signedness.
  static FixedPointSemantics GetIntegerSemantics(unsigned Width,
                                                 bool IsSigned) {
    return FixedPointSemantics(Width, /*Scale=*/0, IsSigned,
                               /*IsSaturated=*/false,
                               /*HasUnsignedPadding=*/false);
  }

  /// Return true if both semantics have identical width, weight, and flags.
  ///
  /// \param Other Semantics to compare against.
  /// @return True if both semantics have identical width, weight, and flags.
  bool operator==(FixedPointSemantics Other) const {
    return Width == Other.Width && LsbWeight == Other.LsbWeight &&
           IsSigned == Other.IsSigned && IsSaturated == Other.IsSaturated &&
           HasUnsignedPadding == Other.HasUnsignedPadding;
  }
  /// Return true if the semantics differ in width, weight, or flags.
  ///
  /// \param Other Semantics to compare against.
  /// @return True if the semantics differ in width, weight, or flags.
  bool operator!=(FixedPointSemantics Other) const { return !(*this == Other); }

  /// Convert the semantics to a 32-bit unsigned integer.
  ///
  /// The result is dependent on the host endianness and not stable across LLVM
  /// versions. See getFromOpaqueInt() to convert it back to a
  /// FixedPointSemantics object.
  /// @return Opaque 32-bit encoding of these semantics.
  LLVM_ABI uint32_t toOpaqueInt() const;
  /// Create a FixedPointSemantics object from an integer created via
  /// toOpaqueInt().
  ///
  /// \param I Opaque integer from \c toOpaqueInt().
  /// @return Semantics reconstructed from the opaque integer.
  LLVM_ABI static FixedPointSemantics getFromOpaqueInt(uint32_t I);

private:
  unsigned Width          : WidthBitWidth;
  signed int LsbWeight    : LsbWeightBitWidth;
  unsigned IsSigned       : 1;
  unsigned IsSaturated    : 1;
  unsigned HasUnsignedPadding : 1;
};

static_assert(sizeof(FixedPointSemantics) == 4, "");

/// Compute a hash code for fixed-point semantics \p Val.
///
/// \param Val Semantics to hash.
/// @return Hash code for \p Val.
inline hash_code hash_value(const FixedPointSemantics &Val) {
  return hash_value(bit_cast<uint32_t>(Val));
}

/// DenseMapInfo specialization for FixedPointSemantics.
template <> struct DenseMapInfo<FixedPointSemantics> {
  /// Hash fixed-point semantics \p Val.
  ///
  /// \param Val Key value to hash.
  /// @return Hash code for \p Val.
  static unsigned getHashValue(const FixedPointSemantics &Val) {
    return hash_value(Val);
  }

  /// Return true if \p LHS and \p RHS are equal.
  ///
  /// \param LHS Left-hand key.
  /// \param RHS Right-hand key.
  /// @return True if \p LHS and \p RHS are equal.
  static bool isEqual(const char &LHS, const char &RHS) { return LHS == RHS; }
};

/// Arbitrary-precision fixed-point value with associated semantics.
///
/// The APFixedPoint class works similarly to APInt/APSInt in that it is a
/// functional replacement for a scaled integer. It supports a wide range of
/// semantics including the one used by fixed point types proposed in ISO/IEC
/// JTC1 SC22 WG14 N1169. The class carries the value and semantics of
/// a fixed point, and provides different operations that would normally be
/// performed on fixed point types.
class APFixedPoint {
public:
  /// Construct from a scaled integer \p Val with semantics \p Sema.
  ///
  /// \param Val Underlying bits; must match \p Sema.getWidth().
  /// \param Sema Fixed-point layout and saturation/sign flags.
  APFixedPoint(const APInt &Val, const FixedPointSemantics &Sema)
      : Val(Val, !Sema.isSigned()), Sema(Sema) {
    assert(Val.getBitWidth() == Sema.getWidth() &&
           "The value should have a bit width that matches the Sema width");
  }

  /// Construct from a truncated \p Val with semantics \p Sema.
  ///
  /// \param Val Low bits of the scaled integer value.
  /// \param Sema Fixed-point layout and saturation/sign flags.
  APFixedPoint(uint64_t Val, const FixedPointSemantics &Sema)
      : APFixedPoint(APInt(Sema.getWidth(), Val, Sema.isSigned(),
                           /*implicitTrunc=*/true),
                     Sema) {}

  /// Construct a zero value with semantics \p Sema.
  ///
  /// \param Sema Fixed-point layout and saturation/sign flags.
  APFixedPoint(const FixedPointSemantics &Sema) : APFixedPoint(0, Sema) {}

  /// Return the underlying scaled integer with matching signedness.
  /// @return Underlying scaled integer with matching signedness.
  APSInt getValue() const { return APSInt(Val, !Sema.isSigned()); }
  /// Return the bit width of this value's semantics.
  /// @return Bit width of this value's semantics.
  inline unsigned getWidth() const { return Sema.getWidth(); }
  /// Return the fractional scale of this value's semantics.
  /// @return Fractional scale of this value's semantics.
  inline unsigned getScale() const { return Sema.getScale(); }
  /// Return the LSB weight of this value's semantics.
  /// @return LSB weight of this value's semantics.
  int getLsbWeight() const { return Sema.getLsbWeight(); }
  /// Return the MSB weight of this value's semantics.
  /// @return MSB weight of this value's semantics.
  int getMsbWeight() const { return Sema.getMsbWeight(); }
  /// Return true if this value's semantics saturate on overflow.
  /// @return True if this value's semantics saturate on overflow.
  inline bool isSaturated() const { return Sema.isSaturated(); }
  /// Return true if this value uses a signed representation.
  /// @return True if this value uses a signed representation.
  inline bool isSigned() const { return Sema.isSigned(); }
  /// Return true if unsigned semantics reserve a padding bit.
  /// @return True if unsigned semantics reserve a padding bit.
  inline bool hasPadding() const { return Sema.hasUnsignedPadding(); }
  /// Return a copy of this value's fixed-point semantics.
  /// @return Copy of this value's fixed-point semantics.
  FixedPointSemantics getSemantics() const { return Sema; }

  /// Return true if the underlying scaled integer is non-zero.
  /// @return True if the underlying scaled integer is non-zero.
  bool getBoolValue() const { return Val.getBoolValue(); }

  /// Convert this number to match the semantics provided.
  ///
  /// If \p Overflow is non-null, it is set to true when the conversion loses
  /// range (or would wrap without saturation).
  ///
  /// \param DstSema Destination fixed-point layout.
  /// \param Overflow Optional overflow out-parameter.
  /// @return Value converted to \p DstSema.
  LLVM_ABI APFixedPoint convert(const FixedPointSemantics &DstSema,
                                bool *Overflow = nullptr) const;

  /// Add \p Other in common full-precision semantics.
  ///
  /// See \c convert() for the meaning of \p Overflow.
  ///
  /// \param Other Right-hand operand.
  /// \param Overflow Optional overflow out-parameter.
  /// @return Sum of this value and \p Other.
  LLVM_ABI APFixedPoint add(const APFixedPoint &Other,
                            bool *Overflow = nullptr) const;
  /// Subtract \p Other in common full-precision semantics.
  ///
  /// See \c convert() for the meaning of \p Overflow.
  ///
  /// \param Other Right-hand operand.
  /// \param Overflow Optional overflow out-parameter.
  /// @return Difference of this value and \p Other.
  LLVM_ABI APFixedPoint sub(const APFixedPoint &Other,
                            bool *Overflow = nullptr) const;
  /// Multiply by \p Other in common full-precision semantics.
  ///
  /// See \c convert() for the meaning of \p Overflow.
  ///
  /// \param Other Right-hand operand.
  /// \param Overflow Optional overflow out-parameter.
  /// @return Product of this value and \p Other.
  LLVM_ABI APFixedPoint mul(const APFixedPoint &Other,
                            bool *Overflow = nullptr) const;
  /// Divide by \p Other in common full-precision semantics.
  ///
  /// See \c convert() for the meaning of \p Overflow.
  ///
  /// \param Other Right-hand operand.
  /// \param Overflow Optional overflow out-parameter.
  /// @return Quotient of this value divided by \p Other.
  LLVM_ABI APFixedPoint div(const APFixedPoint &Other,
                            bool *Overflow = nullptr) const;

  /// Left-shift the scaled integer by \p Amt bits, keeping these semantics.
  ///
  /// Unlike add/sub/mul/div, the result stays in the original semantic.
  ///
  /// \param Amt Number of bits to shift left.
  /// \param Overflow Optional overflow out-parameter.
  /// @return Value left-shifted by \p Amt bits.
  LLVM_ABI APFixedPoint shl(unsigned Amt, bool *Overflow = nullptr) const;
  /// Right-shift the scaled integer by \p Amt bits, keeping these semantics.
  ///
  /// Right shift cannot overflow; if \p Overflow is non-null it is set false.
  ///
  /// \param Amt Number of bits to shift right.
  /// \param Overflow Optional overflow out-parameter (always set false).
  /// @return Value right-shifted by \p Amt bits.
  APFixedPoint shr(unsigned Amt, bool *Overflow = nullptr) const {
    // Right shift cannot overflow.
    if (Overflow)
      *Overflow = false;
    return APFixedPoint(Val >> Amt, Sema);
  }

  /// Perform a unary negation (-X) on this fixed point type, taking into
  /// account saturation if applicable.
  ///
  /// \param Overflow Optional overflow out-parameter.
  /// @return Negated value of this fixed-point number.
  LLVM_ABI APFixedPoint negate(bool *Overflow = nullptr) const;

  /// Return the integral part of this fixed point number, rounded towards
  /// zero. (-2.5k -> -2)
  /// @return Integral part rounded toward zero.
  APSInt getIntPart() const {
    if (getMsbWeight() < 0)
      return APSInt(APInt::getZero(getWidth()), Val.isUnsigned());
    APSInt ExtVal =
        (getLsbWeight() > 0) ? Val.extend(getWidth() + getLsbWeight()) : Val;
    if (Val < 0 && Val != -Val) // Cover the case when we have the min val
      return -((-ExtVal).relativeShl(getLsbWeight()));
    return ExtVal.relativeShl(getLsbWeight());
  }

  /// Return the integral part rounded toward zero into an APSInt.
  ///
  /// Return the integral part of this fixed point number, rounded towards
  /// zero. The value is stored into an APSInt with the provided width and sign.
  /// If the overflow parameter is provided, and the integral value is not able
  /// to be fully stored in the provided width and sign, the overflow parameter
  /// is set to true.
  ///
  /// \param DstWidth Destination APSInt bit width.
  /// \param DstSign Whether the destination APSInt is signed.
  /// \param Overflow Optional overflow out-parameter.
  /// @return Integral part stored in an APSInt of the given width and sign.
  LLVM_ABI APSInt convertToInt(unsigned DstWidth, bool DstSign,
                               bool *Overflow = nullptr) const;

  /// Convert this fixed point number to a floating point value with the
  /// provided semantics.
  ///
  /// \param FloatSema Destination floating-point format.
  /// @return Floating-point value with semantics \p FloatSema.
  LLVM_ABI APFloat convertToFloat(const fltSemantics &FloatSema) const;

  /// Append a human-readable decimal representation to \p Str.
  ///
  /// \param Str Destination character buffer.
  LLVM_ABI void toString(SmallVectorImpl<char> &Str) const;
  /// Return a human-readable decimal representation as a string.
  /// @return Human-readable decimal representation as a string.
  std::string toString() const {
    SmallString<40> S;
    toString(S);
    return std::string(S);
  }

  /// Write a human-readable representation to \p OS.
  ///
  /// \param OS Destination stream.
  LLVM_ABI void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this value to stderr for debugging.
  LLVM_DUMP_METHOD void dump() const;
#endif

  /// Compare against \p Other: return 1 if greater, 0 if equal, -1 if less.
  ///
  /// \param Other Value to compare against.
  /// @return 1 if greater, 0 if equal, or -1 if less than \p Other.
  LLVM_ABI int compare(const APFixedPoint &Other) const;
  /// Return true if this value equals \p Other.
  ///
  /// \param Other Value to compare against.
  /// @return True if this value equals \p Other.
  bool operator==(const APFixedPoint &Other) const {
    return compare(Other) == 0;
  }
  /// Return true if this value differs from \p Other.
  ///
  /// \param Other Value to compare against.
  /// @return True if this value differs from \p Other.
  bool operator!=(const APFixedPoint &Other) const {
    return compare(Other) != 0;
  }
  /// Return true if this value is greater than \p Other.
  ///
  /// \param Other Value to compare against.
  /// @return True if this value is greater than \p Other.
  bool operator>(const APFixedPoint &Other) const { return compare(Other) > 0; }
  /// Return true if this value is less than \p Other.
  ///
  /// \param Other Value to compare against.
  /// @return True if this value is less than \p Other.
  bool operator<(const APFixedPoint &Other) const { return compare(Other) < 0; }
  /// Return true if this value is greater than or equal to \p Other.
  ///
  /// \param Other Value to compare against.
  /// @return True if this value is greater than or equal to \p Other.
  bool operator>=(const APFixedPoint &Other) const {
    return compare(Other) >= 0;
  }
  /// Return true if this value is less than or equal to \p Other.
  ///
  /// \param Other Value to compare against.
  /// @return True if this value is less than or equal to \p Other.
  bool operator<=(const APFixedPoint &Other) const {
    return compare(Other) <= 0;
  }

  /// Return the maximum representable value for semantics \p Sema.
  ///
  /// \param Sema Fixed-point layout and saturation/sign flags.
  /// @return Maximum representable value for \p Sema.
  LLVM_ABI static APFixedPoint getMax(const FixedPointSemantics &Sema);
  /// Return the minimum representable value for semantics \p Sema.
  ///
  /// \param Sema Fixed-point layout and saturation/sign flags.
  /// @return Minimum representable value for \p Sema.
  LLVM_ABI static APFixedPoint getMin(const FixedPointSemantics &Sema);
  /// Return the smallest positive quantum (epsilon) for semantics \p Sema.
  ///
  /// \param Sema Fixed-point layout and saturation/sign flags.
  /// @return Smallest positive quantum for \p Sema.
  LLVM_ABI static APFixedPoint getEpsilon(const FixedPointSemantics &Sema);

  /// Given a floating point semantic, return the next floating point semantic
  /// with a larger exponent and larger or equal mantissa.
  ///
  /// \param S Floating-point format to promote.
  /// @return Next floating-point semantic with a larger exponent.
  LLVM_ABI static const fltSemantics *
  promoteFloatSemantics(const fltSemantics *S);

  /// Create an APFixedPoint from an integer in the given target semantics.
  ///
  /// Create an APFixedPoint with a value equal to that of the provided integer,
  /// and in the same semantics as the provided target semantics. If the value
  /// is not able to fit in the specified fixed point semantics, and the
  /// overflow parameter is provided, it is set to true.
  ///
  /// \param Value Integer value to convert.
  /// \param DstFXSema Destination fixed-point layout.
  /// \param Overflow Optional overflow out-parameter.
  /// @return Fixed-point value equal to \p Value in \p DstFXSema.
  LLVM_ABI static APFixedPoint
  getFromIntValue(const APSInt &Value, const FixedPointSemantics &DstFXSema,
                  bool *Overflow = nullptr);

  /// Create an APFixedPoint from a float in the given target semantics.
  ///
  /// Create an APFixedPoint with a value equal to that of the provided
  /// floating point value, in the provided target semantics. If the value is
  /// not able to fit in the specified fixed point semantics and the overflow
  /// parameter is specified, it is set to true.
  /// For NaN, the Overflow flag is always set. For +inf and -inf, if the
  /// semantic is saturating, the value saturates. Otherwise, the Overflow flag
  /// is set.
  ///
  /// \param Value Floating-point value to convert.
  /// \param DstFXSema Destination fixed-point layout.
  /// \param Overflow Optional overflow out-parameter.
  /// @return Fixed-point value equal to \p Value in \p DstFXSema.
  LLVM_ABI static APFixedPoint
  getFromFloatValue(const APFloat &Value, const FixedPointSemantics &DstFXSema,
                    bool *Overflow = nullptr);

private:
  APSInt Val;
  FixedPointSemantics Sema;
};

/// Stream a human-readable representation of \p FX to \p OS.
///
/// \param OS Destination stream.
/// \param FX Fixed-point value to print.
/// @return Reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const APFixedPoint &FX) {
  OS << FX.toString();
  return OS;
}

/// Compute a hash code for fixed-point value \p Val.
///
/// \param Val Value to hash.
/// @return Hash code for \p Val.
inline hash_code hash_value(const APFixedPoint &Val) {
  return hash_combine(Val.getSemantics(), Val.getValue());
}

/// DenseMapInfo specialization for APFixedPoint.
template <> struct DenseMapInfo<APFixedPoint> {
  /// Hash fixed-point value \p Val.
  ///
  /// \param Val Key value to hash.
  /// @return Hash code for \p Val.
  static unsigned getHashValue(const APFixedPoint &Val) {
    return hash_value(Val);
  }

  /// Return true if \p LHS and \p RHS have equal semantics and values.
  ///
  /// \param LHS Left-hand key.
  /// \param RHS Right-hand key.
  /// @return True if \p LHS and \p RHS have equal semantics and values.
  static bool isEqual(const APFixedPoint &LHS, const APFixedPoint &RHS) {
    return LHS.getSemantics() == RHS.getSemantics() &&
           LHS.getValue() == RHS.getValue();
  }
};

} // namespace llvm

#endif
