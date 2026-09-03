//===- llvm/Support/KnownFPClass.h - Stores known fpclass -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a class for representing known fpclasses used by
// computeKnownFPClass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_KNOWNFPCLASS_H
#define LLVM_SUPPORT_KNOWNFPCLASS_H

#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {
class APFloat;
class APInt;
struct fltSemantics;
struct KnownBits;

/// Tracks which floating-point classes a value may belong to.
struct KnownFPClass {
  /// Floating-point classes the value could be one of.
  FPClassTest KnownFPClasses = fcAllFlags;

  /// std::nullopt if the sign bit is unknown, true if the sign bit is
  /// definitely set or false if the sign bit is definitely unset.
  std::optional<bool> SignBit;

  /// Construct from an optional class mask and optional known sign bit.
  ///
  /// \param Known Possible floating-point classes; defaults to all classes.
  /// \param Sign Known sign bit, or nullopt if unknown.
  KnownFPClass(FPClassTest Known = fcAllFlags, std::optional<bool> Sign = {})
      : KnownFPClasses(Known), SignBit(Sign) {}

  /// Construct from a constant floating-point value.
  ///
  /// \param C Constant whose exact class and sign are taken.
  LLVM_ABI KnownFPClass(const APFloat &C);

  /// Return true if this and \p Other have the same known classes and sign.
  ///
  /// \param Other Other known-class state to compare against.
  /// \return True if this and \p Other are equal.
  bool operator==(KnownFPClass Other) const {
    return KnownFPClasses == Other.KnownFPClasses && SignBit == Other.SignBit;
  }

  /// Return true if it's known this can never be one of the mask entries.
  ///
  /// \param Mask Class bits that must not be possible.
  /// \return True if no class in \p Mask is possible.
  bool isKnownNever(FPClassTest Mask) const {
    return (KnownFPClasses & Mask) == fcNone;
  }

  /// Return true if every possible class is covered by \p Mask.
  ///
  /// \param Mask Class bits that must cover all possible values.
  /// \return True if every possible class is in \p Mask.
  bool isKnownAlways(FPClassTest Mask) const { return isKnownNever(~Mask); }

  /// Return true if nothing is known about the class or sign bit.
  ///
  /// \return True if nothing is known about the class or sign bit.
  bool isUnknown() const { return KnownFPClasses == fcAllFlags && !SignBit; }

  /// Return true if it's known this can never be a nan.
  ///
  /// \return True if NaN is impossible.
  bool isKnownNeverNaN() const { return isKnownNever(fcNan); }

  /// Return true if it's known this must always be a nan.
  ///
  /// \return True if the value must be NaN.
  bool isKnownAlwaysNaN() const { return isKnownAlways(fcNan); }

  /// Return true if it's known this can never be an infinity.
  ///
  /// \return True if infinity is impossible.
  bool isKnownNeverInfinity() const { return isKnownNever(fcInf); }

  /// Return true if it's known this can never be an infinity or nan
  ///
  /// \return True if infinity and NaN are impossible.
  bool isKnownNeverInfOrNaN() const { return isKnownNever(fcInf | fcNan); }

  /// Return true if it's known this can never be +infinity.
  ///
  /// \return True if +infinity is impossible.
  bool isKnownNeverPosInfinity() const { return isKnownNever(fcPosInf); }

  /// Return true if it's known this can never be -infinity.
  ///
  /// \return True if -infinity is impossible.
  bool isKnownNeverNegInfinity() const { return isKnownNever(fcNegInf); }

  /// Return true if it's known this can never be a subnormal
  ///
  /// \return True if subnormals are impossible.
  bool isKnownNeverSubnormal() const { return isKnownNever(fcSubnormal); }

  /// Return true if it's known this can never be a positive subnormal
  ///
  /// \return True if positive subnormals are impossible.
  bool isKnownNeverPosSubnormal() const { return isKnownNever(fcPosSubnormal); }

  /// Return true if it's known this can never be a negative subnormal
  ///
  /// \return True if negative subnormals are impossible.
  bool isKnownNeverNegSubnormal() const { return isKnownNever(fcNegSubnormal); }

  /// Return true if it's known this can never be a zero. This means a literal
  /// [+-]0, and does not include denormal inputs implicitly treated as [+-]0.
  ///
  /// \return True if literal zero is impossible.
  bool isKnownNeverZero() const { return isKnownNever(fcZero); }

  /// Return true if it's known this can never be a literal positive zero.
  ///
  /// \return True if literal +0 is impossible.
  bool isKnownNeverPosZero() const { return isKnownNever(fcPosZero); }

  /// Return true if it's known this can never be a negative zero. This means a
  /// literal -0 and does not include denormal inputs implicitly treated as -0.
  ///
  /// \return True if literal -0 is impossible.
  bool isKnownNeverNegZero() const { return isKnownNever(fcNegZero); }

  /// Return true if it's known this can never be interpreted as a zero.
  ///
  /// This extends isKnownNeverZero to cover the case where the assumed
  /// floating-point mode for the function interprets denormals as zero.
  ///
  /// \param Mode Denormal handling mode used to interpret logical zeros.
  /// \return True if a logical zero is impossible under \p Mode.
  LLVM_ABI bool isKnownNeverLogicalZero(DenormalMode Mode) const;

  /// Return true if it's known this can never be interpreted as a negative
  /// zero.
  ///
  /// \param Mode Denormal handling mode used to interpret logical zeros.
  /// \return True if a logical -0 is impossible under \p Mode.
  LLVM_ABI bool isKnownNeverLogicalNegZero(DenormalMode Mode) const;

  /// Return true if it's known this can never be interpreted as a positive
  /// zero.
  ///
  /// \param Mode Denormal handling mode used to interpret logical zeros.
  /// \return True if a logical +0 is impossible under \p Mode.
  LLVM_ABI bool isKnownNeverLogicalPosZero(DenormalMode Mode) const;

  /// Mask of classes that are ordered less than zero.
  static constexpr FPClassTest OrderedLessThanZeroMask =
      fcNegSubnormal | fcNegNormal | fcNegInf;

  /// Mask of classes that are ordered greater than zero.
  static constexpr FPClassTest OrderedGreaterThanZeroMask =
      fcPosSubnormal | fcPosNormal | fcPosInf;

  /// Return true if we can prove that the analyzed floating-point value is
  /// either NaN or never less than -0.0.
  ///
  ///      NaN --> true
  ///       +0 --> true
  ///       -0 --> true
  ///   x > +0 --> true
  ///   x < -0 --> false
  ///
  /// \return True if the value is NaN or never ordered less than -0.0.
  bool cannotBeOrderedLessThanZero() const {
    return isKnownNever(OrderedLessThanZeroMask);
  }

  /// Return true if the value is NaN or never ordered greater than -0.0.
  ///
  ///      NaN --> true
  ///       +0 --> true
  ///       -0 --> true
  ///   x > +0 --> false
  ///   x < -0 --> true
  ///
  /// \return True if the value is NaN or never ordered greater than -0.0.
  bool cannotBeOrderedGreaterThanZero() const {
    return isKnownNever(OrderedGreaterThanZeroMask);
  }

  /// Return true if it's known this can never be a positive value or a logical
  /// 0.
  ///
  ///      NaN --> true
  ///  x <= +0 --> false
  ///     psub --> true if mode is ieee, false otherwise.
  ///   x > +0 --> true
  ///
  /// \param Mode Denormal handling mode used for logical-zero checks.
  /// \return True if a positive value or logical 0 is impossible.
  bool cannotBeOrderedLessEqZero(DenormalMode Mode) const {
    return isKnownNever(fcNegative) && isKnownNeverLogicalPosZero(Mode);
  }

  /// Return true if it's know this can never be a negative value or a logical
  /// 0.
  ///
  ///      NaN --> true
  ///  x >= -0 --> false
  ///     nsub --> true if mode is ieee, false otherwise.
  ///   x < -0 --> true
  ///
  /// \param Mode Denormal handling mode used for logical-zero checks.
  /// \return True if a negative value or logical 0 is impossible.
  bool cannotBeOrderedGreaterEqZero(DenormalMode Mode) const {
    return isKnownNever(fcPositive) && isKnownNeverLogicalNegZero(Mode);
  }

  /// Return the intersection of this and \p RHS known-class information.
  ///
  /// \param RHS Other known-class state to intersect with.
  /// \return The intersection of this and \p RHS.
  KnownFPClass intersectWith(const KnownFPClass &RHS) const {
    return KnownFPClass(KnownFPClasses | RHS.KnownFPClasses,
                        SignBit == RHS.SignBit ? SignBit : std::nullopt);
  }

  /// Return the union of this and \p RHS known-class information.
  ///
  /// \param RHS Other known-class state to union with.
  /// \return The union of this and \p RHS.
  KnownFPClass unionWith(const KnownFPClass &RHS) const {
    std::optional<bool> MergedSignBit;
    if (SignBit && !RHS.SignBit)
      MergedSignBit = SignBit;
    else if (!SignBit && RHS.SignBit)
      MergedSignBit = RHS.SignBit;

    return KnownFPClass(KnownFPClasses & RHS.KnownFPClasses, MergedSignBit);
  }

  /// Merge \p RHS into this by taking the union of possible classes.
  ///
  /// \param RHS Other known-class state to merge in.
  /// \return A reference to this after merging \p RHS.
  KnownFPClass &operator|=(const KnownFPClass &RHS) {
    KnownFPClasses = KnownFPClasses | RHS.KnownFPClasses;

    if (SignBit != RHS.SignBit)
      SignBit = std::nullopt;
    return *this;
  }

  /// Rule out the floating-point classes in \p RuleOut.
  ///
  /// \param RuleOut Class bits that are no longer possible.
  void knownNot(FPClassTest RuleOut) {
    KnownFPClasses = KnownFPClasses & ~RuleOut;
    if (isKnownNever(fcNan) && !SignBit) {
      if (isKnownNever(fcNegative))
        SignBit = false;
      else if (isKnownNever(fcPositive))
        SignBit = true;
    }
  }

  /// Negate this value in place, flipping known classes and the sign bit.
  void fneg() {
    KnownFPClasses = llvm::fneg(KnownFPClasses);
    if (SignBit)
      SignBit = !*SignBit;
  }

  /// Return the floating-point negation of \p Src.
  ///
  /// \param Src Known-class state to negate.
  /// \return The negation of \p Src.
  static KnownFPClass fneg(const KnownFPClass &Src) {
    KnownFPClass Known = Src;
    Known.fneg();
    return Known;
  }

  /// Replace this with the absolute value, forcing a non-negative sign.
  void fabs() {
    if (KnownFPClasses & fcNegZero)
      KnownFPClasses |= fcPosZero;

    if (KnownFPClasses & fcNegInf)
      KnownFPClasses |= fcPosInf;

    if (KnownFPClasses & fcNegSubnormal)
      KnownFPClasses |= fcPosSubnormal;

    if (KnownFPClasses & fcNegNormal)
      KnownFPClasses |= fcPosNormal;

    signBitMustBeZero();
  }

  /// Return the absolute value of \p Src.
  ///
  /// \param Src Known-class state to take the absolute value of.
  /// \return The absolute value of \p Src.
  static KnownFPClass fabs(const KnownFPClass &Src) {
    KnownFPClass Known = Src;
    Known.fabs();
    return Known;
  }

  /// Kind of IEEE min/max intrinsic without depending on IR.
  enum class MinMaxKind {
    /// IEEE-754 minimum.
    minimum,
    /// IEEE-754 maximum.
    maximum,
    /// IEEE-754 minimumNumber.
    minimumnum,
    /// IEEE-754 maximumNumber.
    maximumnum,
    /// LLVM minnum.
    minnum,
    /// LLVM maxnum.
    maxnum
  };

  /// Report known classes for a min/max-like operation on \p LHS and \p RHS.
  ///
  /// \param LHS Left-hand operand known classes.
  /// \param RHS Right-hand operand known classes.
  /// \param Kind Which min/max intrinsic semantics to apply.
  /// \param DenormMode Denormal handling mode for the operation.
  /// \return Known classes for the min/max-like result.
  LLVM_ABI static KnownFPClass
  minMaxLike(const KnownFPClass &LHS, const KnownFPClass &RHS, MinMaxKind Kind,
             DenormalMode DenormMode = DenormalMode::getDynamic());

  /// Apply the canonicalize intrinsic to this value.
  ///
  /// This is essentially a stronger form of propagateCanonicalizingSrc.
  ///
  /// \param Src Source known classes before canonicalization.
  /// \param DenormMode Denormal handling mode for the operation.
  /// \return Known classes after canonicalization.
  LLVM_ABI static KnownFPClass
  canonicalize(const KnownFPClass &Src,
               DenormalMode DenormMode = DenormalMode::getDynamic());

  /// Report known values for a bitcast into a float with provided semantics.
  ///
  /// \param FltSemantics Floating-point semantics of the destination type.
  /// \param Bits Known bits of the integer source of the bitcast.
  /// \return Known classes of the bitcast float.
  LLVM_ABI static KnownFPClass bitcast(const fltSemantics &FltSemantics,
                                       const KnownBits &Bits);

  /// Report known bits for a float with provided semantics.
  ///
  /// \param FltSemantics Floating-point semantics of this value's type.
  /// \return Known bits of this float value.
  LLVM_ABI KnownBits toKnownBits(const fltSemantics &FltSemantics) const;

  /// Report known values for fadd.
  ///
  /// \param LHS Left-hand operand known classes.
  /// \param RHS Right-hand operand known classes.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the fadd result.
  LLVM_ABI static KnownFPClass
  fadd(const KnownFPClass &LHS, const KnownFPClass &RHS,
       DenormalMode Mode = DenormalMode::getDynamic());

  /// Report known values for fadd x, x.
  ///
  /// \param Src Operand known classes (used for both addends).
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the fadd-self result.
  LLVM_ABI static KnownFPClass
  fadd_self(const KnownFPClass &Src,
            DenormalMode Mode = DenormalMode::getDynamic());

  /// Report known values for fsub.
  ///
  /// \param LHS Left-hand operand known classes.
  /// \param RHS Right-hand operand known classes.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the fsub result.
  LLVM_ABI static KnownFPClass
  fsub(const KnownFPClass &LHS, const KnownFPClass &RHS,
       DenormalMode Mode = DenormalMode::getDynamic());

  /// Report known values for fmul.
  ///
  /// \param LHS Left-hand operand known classes.
  /// \param RHS Right-hand operand known classes.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the fmul result.
  LLVM_ABI static KnownFPClass
  fmul(const KnownFPClass &LHS, const KnownFPClass &RHS,
       DenormalMode Mode = DenormalMode::getDynamic());

  /// Report known values for squaring a floating-point value.
  ///
  /// Special case of fmul x, x.
  ///
  /// \param Src Operand known classes to square.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the squared result.
  static KnownFPClass square(const KnownFPClass &Src,
                             DenormalMode Mode = DenormalMode::getDynamic()) {
    KnownFPClass Known = fmul(Src, Src, Mode);

    // X * X is always non-negative or a NaN.
    Known.knownNot(fcNegative);
    Known.propagateNonNaN(Src);
    return Known;
  }

  /// Report known values for fmul by a constant.
  ///
  /// \param LHS Left-hand operand known classes.
  /// \param RHS Constant right-hand operand.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the fmul-by-constant result.
  LLVM_ABI static KnownFPClass
  fmul(const KnownFPClass &LHS, const APFloat &RHS,
       DenormalMode Mode = DenormalMode::getDynamic());

  /// Report known values for fdiv.
  ///
  /// \param LHS Left-hand operand known classes.
  /// \param RHS Right-hand operand known classes.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the fdiv result.
  LLVM_ABI static KnownFPClass
  fdiv(const KnownFPClass &LHS, const KnownFPClass &RHS,
       DenormalMode Mode = DenormalMode::getDynamic());

  /// Report known values for fdiv x, x.
  ///
  /// \param Src Operand known classes (used for both operands).
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the fdiv-self result.
  LLVM_ABI static KnownFPClass
  fdiv_self(const KnownFPClass &Src,
            DenormalMode Mode = DenormalMode::getDynamic());

  /// Report known values for frem.
  ///
  /// \param LHS Left-hand operand known classes.
  /// \param RHS Right-hand operand known classes.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the frem result.
  LLVM_ABI static KnownFPClass
  frem(const KnownFPClass &LHS, const KnownFPClass &RHS,
       DenormalMode Mode = DenormalMode::getDynamic());

  /// Report known values for frem x, x.
  ///
  /// \param Src Operand known classes (used for both operands).
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the frem-self result.
  LLVM_ABI static KnownFPClass
  frem_self(const KnownFPClass &Src,
            DenormalMode Mode = DenormalMode::getDynamic());

  /// Report known values for fma.
  ///
  /// \param LHS First multiply operand known classes.
  /// \param RHS Second multiply operand known classes.
  /// \param Addend Addend known classes.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the fma result.
  LLVM_ABI static KnownFPClass
  fma(const KnownFPClass &LHS, const KnownFPClass &RHS,
      const KnownFPClass &Addend,
      DenormalMode Mode = DenormalMode::getDynamic());

  /// Report known values for fma of a squared value plus an addend.
  ///
  /// \param Squared Operand known classes that are squared in the FMA.
  /// \param Addend Addend known classes.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the squared-fma result.
  LLVM_ABI static KnownFPClass
  fma_square(const KnownFPClass &Squared, const KnownFPClass &Addend,
             DenormalMode Mode = DenormalMode::getDynamic());

  /// Propagate known class for sqrt.
  ///
  /// \param Src Operand known classes.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the sqrt result.
  LLVM_ABI static KnownFPClass
  sqrt(const KnownFPClass &Src, DenormalMode Mode = DenormalMode::getDynamic());

  /// Propagate known class for log/log2/log10.
  ///
  /// \param Src Operand known classes.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the log result.
  LLVM_ABI static KnownFPClass
  log(const KnownFPClass &Src, DenormalMode Mode = DenormalMode::getDynamic());

  /// Report known values for exp, exp2 and exp10.
  ///
  /// \param Src Operand known classes.
  /// \return Known classes for the exp result.
  LLVM_ABI static KnownFPClass exp(const KnownFPClass &Src);

  /// Report known values for sin.
  ///
  /// \param Src Operand known classes.
  /// \return Known classes for the sin result.
  LLVM_ABI static KnownFPClass sin(const KnownFPClass &Src);

  /// Report known values for cos.
  ///
  /// \param Src Operand known classes.
  /// \return Known classes for the cos result.
  LLVM_ABI static KnownFPClass cos(const KnownFPClass &Src);

  /// Report known values for tan.
  ///
  /// \param Src Operand known classes.
  /// \return Known classes for the tan result.
  LLVM_ABI static KnownFPClass tan(const KnownFPClass &Src);

  /// Report known values for sinh.
  ///
  /// \param Src Operand known classes.
  /// \return Known classes for the sinh result.
  LLVM_ABI static KnownFPClass sinh(const KnownFPClass &Src);

  /// Report known values for cosh.
  ///
  /// \param Src Operand known classes.
  /// \return Known classes for the cosh result.
  LLVM_ABI static KnownFPClass cosh(const KnownFPClass &Src);

  /// Report known values for tanh.
  ///
  /// \param Src Operand known classes.
  /// \return Known classes for the tanh result.
  LLVM_ABI static KnownFPClass tanh(const KnownFPClass &Src);

  /// Report known values for asin.
  ///
  /// \param Src Operand known classes.
  /// \return Known classes for the asin result.
  LLVM_ABI static KnownFPClass asin(const KnownFPClass &Src);

  /// Report known values for acos.
  ///
  /// \param Src Operand known classes.
  /// \return Known classes for the acos result.
  LLVM_ABI static KnownFPClass acos(const KnownFPClass &Src);

  /// Report known values for atan.
  ///
  /// \param Src Operand known classes.
  /// \return Known classes for the atan result.
  LLVM_ABI static KnownFPClass atan(const KnownFPClass &Src);

  /// Report known values for atan2.
  ///
  /// \param LHS Y-coordinate known classes.
  /// \param RHS X-coordinate known classes.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the atan2 result.
  LLVM_ABI static KnownFPClass
  atan2(const KnownFPClass &LHS, const KnownFPClass &RHS,
        DenormalMode Mode = DenormalMode::getDynamic());

  /// Return true if the sign bit must be 0, ignoring the sign of nans.
  ///
  /// \return True if the sign bit must be 0, ignoring NaN signs.
  bool signBitIsZeroOrNaN() const { return isKnownNever(fcNegative); }

  /// Assume the sign bit is zero.
  void signBitMustBeZero() {
    KnownFPClasses &= (fcPositive | fcNan);
    SignBit = false;
  }

  /// Assume the sign bit is one.
  void signBitMustBeOne() {
    KnownFPClasses &= (fcNegative | fcNan);
    SignBit = true;
  }

  /// Copy the sign of \p Sign onto this magnitude in place.
  ///
  /// \param Sign Known-class state providing the result sign.
  void copysign(const KnownFPClass &Sign) {
    // Don't know anything about the sign of the source. Expand the possible set
    // to its opposite sign pair.
    if (KnownFPClasses & fcZero)
      KnownFPClasses |= fcZero;
    if (KnownFPClasses & fcSubnormal)
      KnownFPClasses |= fcSubnormal;
    if (KnownFPClasses & fcNormal)
      KnownFPClasses |= fcNormal;
    if (KnownFPClasses & fcInf)
      KnownFPClasses |= fcInf;

    // Sign bit is exactly preserved even for nans.
    SignBit = Sign.SignBit;

    // Clear sign bits based on the input sign mask.
    if (Sign.isKnownNever(fcPositive | fcNan) || (SignBit && *SignBit))
      KnownFPClasses &= (fcNegative | fcNan);
    if (Sign.isKnownNever(fcNegative | fcNan) || (SignBit && !*SignBit))
      KnownFPClasses &= (fcPositive | fcNan);
  }

  /// Return \p KnownMag with the sign taken from \p KnownSign.
  ///
  /// \param KnownMag Known classes of the magnitude operand.
  /// \param KnownSign Known classes of the sign operand.
  /// \return \p KnownMag with the sign taken from \p KnownSign.
  static KnownFPClass copysign(const KnownFPClass &KnownMag,
                               const KnownFPClass &KnownSign) {
    KnownFPClass Known = KnownMag;
    Known.copysign(KnownSign);
    return Known;
  }

  /// Propagate that an operation cannot introduce a signaling NaN from \p Src.
  ///
  /// \param Src Operand whose non-SNaN property may transfer to the result.
  void propagateNonSNaN(const KnownFPClass &Src) {
    if (Src.isKnownNever(fcSNan))
      knownNot(fcSNan);
  }

  /// Propagate that an operation cannot introduce a signaling NaN from either
  /// operand.
  ///
  /// \param LHS Left-hand operand known classes.
  /// \param RHS Right-hand operand known classes.
  void propagateNonSNaN(const KnownFPClass &LHS, const KnownFPClass &RHS) {
    if (LHS.isKnownNever(fcSNan) && RHS.isKnownNever(fcSNan))
      knownNot(fcSNan);
  }

  /// Propagate that a non-NaN source implies a non-NaN result.
  ///
  /// For unconstrained operations, signaling nans are not guaranteed to be
  /// quieted but cannot be introduced.
  ///
  /// \param Src Operand whose non-NaN property may transfer to the result.
  /// \param PreserveSign If true, also copy \p Src's known sign bit when
  /// non-NaN.
  void propagateNonNaN(const KnownFPClass &Src, bool PreserveSign = false) {
    propagateNonSNaN(Src);
    if (Src.isKnownNever(fcNan)) {
      knownNot(fcNan);
      if (PreserveSign)
        SignBit = Src.SignBit;
    }
  }

  /// Propagate that both non-NaN operands imply a non-NaN result.
  ///
  /// \param LHS Left-hand operand known classes.
  /// \param RHS Right-hand operand known classes.
  void propagateNonNaN(const KnownFPClass &LHS, const KnownFPClass &RHS) {
    propagateNonSNaN(LHS, RHS);
    if (LHS.isKnownNeverNaN() && RHS.isKnownNeverNaN())
      knownNot(fcNan);
  }

  /// Propagate sign knowledge for operations whose result sign is the xor of
  /// the operand signs.
  ///
  /// This only rules out possible non-NaN sign classes. NaNs do not have a
  /// constrained sign class here. Applies to multiply and divide.
  ///
  /// \param LHS Left-hand operand known classes.
  /// \param RHS Right-hand operand known classes.
  void propagateXorSign(const KnownFPClass &LHS, const KnownFPClass &RHS) {
    if ((LHS.isKnownNever(fcNegative) && RHS.isKnownNever(fcNegative)) ||
        (LHS.isKnownNever(fcPositive) && RHS.isKnownNever(fcPositive)))
      knownNot(fcNegative);

    if ((LHS.isKnownNever(fcPositive) && RHS.isKnownNever(fcNegative)) ||
        (LHS.isKnownNever(fcNegative) && RHS.isKnownNever(fcPositive)))
      knownNot(fcPositive);
  }

  /// Propagate knowledge from a source that could be a denormal or zero.
  ///
  /// We have to be conservative since output flushing is not guaranteed, so
  /// known-never-zero may not hold.
  ///
  /// This assumes a copy-like operation and will replace any currently known
  /// information.
  ///
  /// \param Src Source known classes that may be denormal or zero.
  /// \param Mode Denormal handling mode for the operation.
  LLVM_ABI void propagateDenormal(const KnownFPClass &Src, DenormalMode Mode);

  /// Report known classes for a potentially canonicalizing operation on \p Src.
  ///
  /// We can assume signaling nans will not be introduced, but cannot assume a
  /// denormal will be flushed under FTZ/DAZ.
  ///
  /// This assumes a copy-like operation and will replace any currently known
  /// information.
  ///
  /// \param Src Source known classes before the canonicalizing operation.
  /// \param Mode Denormal handling mode for the operation.
  LLVM_ABI void propagateCanonicalizingSrc(const KnownFPClass &Src,
                                           DenormalMode Mode);

  /// Propagate known class for fpext.
  ///
  /// \param KnownSrc Source known classes before the extension.
  /// \param DstTy Floating-point semantics of the destination type.
  /// \param SrcTy Floating-point semantics of the source type.
  /// \return Known classes after the floating-point extension.
  LLVM_ABI static KnownFPClass fpext(const KnownFPClass &KnownSrc,
                                     const fltSemantics &DstTy,
                                     const fltSemantics &SrcTy);

  /// Propagate known class for fptrunc.
  ///
  /// \param KnownSrc Source known classes before the truncation.
  /// \return Known classes after the floating-point truncation.
  LLVM_ABI static KnownFPClass fptrunc(const KnownFPClass &KnownSrc);

  /// Propagate known class for rounding intrinsics.
  ///
  /// Covers trunc, floor, ceil, rint, nearbyint, round, and roundeven. This is
  /// trunc if \p IsTrunc. \p IsMultiUnitFPType if this is for a multi-unit
  /// floating-point type.
  ///
  /// \param Src Operand known classes.
  /// \param IsTrunc True when the intrinsic is trunc.
  /// \param IsMultiUnitFPType True for multi-unit floating-point types.
  /// \return Known classes after the rounding operation.
  LLVM_ABI static KnownFPClass roundToIntegral(const KnownFPClass &Src,
                                               bool IsTrunc,
                                               bool IsMultiUnitFPType);

  /// Propagate known class for mantissa component of frexp.
  ///
  /// \param Src Operand known classes.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the frexp mantissa.
  LLVM_ABI static KnownFPClass
  frexp_mant(const KnownFPClass &Src,
             DenormalMode Mode = DenormalMode::getDynamic());

  /// Propagate known class for ldexp with a bounded exponent range.
  ///
  /// Assumes the exponent is known to be within [\p ConstantRangeMin,
  /// \p ConstantRangeMax].
  ///
  /// \param Src Operand known classes.
  /// \param ConstantRangeMin Inclusive lower bound on the exponent.
  /// \param ConstantRangeMax Inclusive upper bound on the exponent.
  /// \param Flt Floating-point semantics of the result type.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the ldexp result.
  // TODO: This really ought to use ConstantRange, but it's in IR not Support.
  LLVM_ABI static KnownFPClass
  ldexp(const KnownFPClass &Src, const APInt &ConstantRangeMin,
        const APInt &ConstantRangeMax, const fltSemantics &Flt,
        DenormalMode Mode = DenormalMode::getDynamic());

  /// Propagate known class for ldexp with a known-bits exponent.
  ///
  /// \param Src Operand known classes.
  /// \param ExpBits Known bits of the exponent operand.
  /// \param Flt Floating-point semantics of the result type.
  /// \param Mode Denormal handling mode for the operation.
  /// \return Known classes for the ldexp result.
  LLVM_ABI static KnownFPClass
  ldexp(const KnownFPClass &Src, const KnownBits &ExpBits,
        const fltSemantics &Flt,
        DenormalMode Mode = DenormalMode::getDynamic());

  /// Propagate known class for pow.
  ///
  /// \param LHS Base known classes.
  /// \param RHS Exponent known classes.
  /// \return Known classes for the pow result.
  LLVM_ABI static KnownFPClass pow(const KnownFPClass &LHS,
                                   const KnownFPClass &RHS);

  /// Propagate known class for powi.
  ///
  /// \param Src Base known classes.
  /// \param N Known bits of the integer exponent.
  /// \return Known classes for the powi result.
  LLVM_ABI static KnownFPClass powi(const KnownFPClass &Src,
                                    const KnownBits &N);

  /// Reset all known-class and sign information to unknown.
  void resetAll() { *this = KnownFPClass(); }
};

/// Return the union of known-class information from \p LHS and \p RHS.
///
/// \param LHS Left-hand known-class state (taken by value).
/// \param RHS Right-hand known-class state.
/// \return The union of \p LHS and \p RHS.
inline KnownFPClass operator|(KnownFPClass LHS, const KnownFPClass &RHS) {
  LHS |= RHS;
  return LHS;
}

/// Return the union of known-class information from \p LHS and \p RHS.
///
/// \param LHS Left-hand known-class state.
/// \param RHS Right-hand known-class state (moved from).
/// \return The union of \p LHS and \p RHS.
inline KnownFPClass operator|(const KnownFPClass &LHS, KnownFPClass &&RHS) {
  RHS |= LHS;
  return std::move(RHS);
}

} // namespace llvm

#endif
