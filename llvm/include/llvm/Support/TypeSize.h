//===- TypeSize.h - Wrapper around type sizes -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a struct that can be used to query the size of IR types
// which may be scalable vectors. It provides convenience operators so that
// it can be used in much the same way as a single scalar value.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TYPESIZE_H
#define LLVM_SUPPORT_TYPESIZE_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <cstdint>
#include <type_traits>

namespace llvm {

/// StackOffset holds a fixed and a scalable offset in bytes.
class StackOffset {
  int64_t Fixed = 0;
  int64_t Scalable = 0;

  StackOffset(int64_t Fixed, int64_t Scalable)
      : Fixed(Fixed), Scalable(Scalable) {}

public:
  /// Construct a zero fixed and scalable stack offset.
  StackOffset() = default;
  /// Construct a stack offset with only a fixed component.
  ///
  /// \param Fixed Fixed offset in bytes.
  /// \return Stack offset with the given fixed component and zero scalable.
  static StackOffset getFixed(int64_t Fixed) { return {Fixed, 0}; }
  /// Construct a stack offset with only a scalable (vscale-dependent) component.
  ///
  /// \param Scalable Scalable offset in bytes (coefficient of vscale).
  /// \return Stack offset with the given scalable component and zero fixed.
  static StackOffset getScalable(int64_t Scalable) { return {0, Scalable}; }
  /// Construct a stack offset from fixed and scalable components.
  ///
  /// \param Fixed Fixed offset in bytes.
  /// \param Scalable Scalable offset in bytes (coefficient of vscale).
  /// \return Stack offset combining the fixed and scalable components.
  static StackOffset get(int64_t Fixed, int64_t Scalable) {
    return {Fixed, Scalable};
  }

  /// Returns the fixed component of the stack.
  ///
  /// \return Fixed offset in bytes.
  int64_t getFixed() const { return Fixed; }

  /// Returns the scalable component of the stack.
  ///
  /// \return Scalable offset in bytes (coefficient of vscale).
  int64_t getScalable() const { return Scalable; }

  // Arithmetic operations.
  /// Add fixed and scalable components of \p RHS to this offset.
  ///
  /// \param RHS Offset whose components are added.
  /// \return New offset with summed fixed and scalable components.
  StackOffset operator+(const StackOffset &RHS) const {
    return {Fixed + RHS.Fixed, Scalable + RHS.Scalable};
  }
  /// Subtract fixed and scalable components of \p RHS from this offset.
  ///
  /// \param RHS Offset whose components are subtracted.
  /// \return New offset with differenced fixed and scalable components.
  StackOffset operator-(const StackOffset &RHS) const {
    return {Fixed - RHS.Fixed, Scalable - RHS.Scalable};
  }
  /// Add fixed and scalable components of \p RHS into this offset.
  ///
  /// \param RHS Offset whose components are added in place.
  /// \return Reference to this offset after the addition.
  StackOffset &operator+=(const StackOffset &RHS) {
    Fixed += RHS.Fixed;
    Scalable += RHS.Scalable;
    return *this;
  }
  /// Subtract fixed and scalable components of \p RHS from this offset in place.
  ///
  /// \param RHS Offset whose components are subtracted in place.
  /// \return Reference to this offset after the subtraction.
  StackOffset &operator-=(const StackOffset &RHS) {
    Fixed -= RHS.Fixed;
    Scalable -= RHS.Scalable;
    return *this;
  }
  /// Negate both fixed and scalable components of this offset.
  ///
  /// \return Offset with both components negated.
  StackOffset operator-() const { return {-Fixed, -Scalable}; }

  // Equality comparisons.
  /// Return true if both fixed and scalable components equal those of \p RHS.
  ///
  /// \param RHS Offset to compare against.
  /// \return True if both components match \p RHS.
  bool operator==(const StackOffset &RHS) const {
    return Fixed == RHS.Fixed && Scalable == RHS.Scalable;
  }
  /// Return true if either component differs from that of \p RHS.
  ///
  /// \param RHS Offset to compare against.
  /// \return True if either component differs from \p RHS.
  bool operator!=(const StackOffset &RHS) const {
    return Fixed != RHS.Fixed || Scalable != RHS.Scalable;
  }

  /// Return true if either the fixed or scalable component is non-zero.
  ///
  /// \return True if this offset is non-zero.
  explicit operator bool() const { return Fixed != 0 || Scalable != 0; }
};

/// Internal helpers for fixed- versus vscale-sized quantities.
namespace details {

/// Common representation for a quantity that may be fixed or scaled by
/// runtime \c vscale, shared by \c ElementCount and \c TypeSize.
template <typename LeafTy, typename ValueTy> class FixedOrScalableQuantity {
public:
  /// Scalar type used to store the fixed or scalable quantity.
  using ScalarTy = ValueTy;

protected:
  /// Known-minimum quantity (coefficient of 1 or of vscale).
  ScalarTy Quantity = 0;
  /// True when \c Quantity is scaled by the runtime \c vscale factor.
  bool Scalable = false;

  /// Construct a zero fixed quantity.
  constexpr FixedOrScalableQuantity() = default;
  /// Construct with known-minimum \p Quantity, optionally scaled by vscale.
  ///
  /// \param Quantity Known-minimum coefficient.
  /// \param Scalable Whether the quantity is scaled by vscale.
  constexpr FixedOrScalableQuantity(ScalarTy Quantity, bool Scalable)
      : Quantity(Quantity), Scalable(Scalable) {}

  /// Add \p RHS into \p LHS when both have compatible fixed/scalable kinds.
  ///
  /// \param LHS Quantity updated in place.
  /// \param RHS Quantity added into \p LHS.
  /// \return Reference to \p LHS after the addition.
  friend constexpr LeafTy &operator+=(LeafTy &LHS, const LeafTy &RHS) {
    assert((LHS.Quantity == 0 || RHS.Quantity == 0 ||
            LHS.Scalable == RHS.Scalable) &&
           "Incompatible types");
    LHS.Quantity += RHS.Quantity;
    if (!RHS.isZero())
      LHS.Scalable = RHS.Scalable;
    return LHS;
  }

  /// Subtract \p RHS from \p LHS when both have compatible fixed/scalable
  /// kinds; asserts if the operands cannot be combined.
  ///
  /// \param LHS Quantity updated in place.
  /// \param RHS Quantity subtracted from \p LHS.
  /// \return Reference to \p LHS after the subtraction.
  friend constexpr LeafTy &operator-=(LeafTy &LHS, const LeafTy &RHS) {
    assert((LHS.Quantity == 0 || RHS.Quantity == 0 ||
            LHS.Scalable == RHS.Scalable) &&
           "Incompatible types");
    LHS.Quantity -= RHS.Quantity;
    if (!RHS.isZero())
      LHS.Scalable = RHS.Scalable;
    return LHS;
  }

  /// Multiply \p LHS by the scalar \p RHS in place, preserving its
  /// fixed/scalable kind.
  ///
  /// \param LHS Quantity updated in place.
  /// \param RHS Scalar multiplier.
  /// \return Reference to \p LHS after the multiplication.
  friend constexpr LeafTy &operator*=(LeafTy &LHS, ScalarTy RHS) {
    LHS.Quantity *= RHS;
    return LHS;
  }

  /// Add \p RHS to \p LHS when both have compatible fixed/scalable kinds.
  ///
  /// \param LHS Left-hand quantity.
  /// \param RHS Right-hand quantity.
  /// \return Sum of \p LHS and \p RHS.
  friend constexpr LeafTy operator+(const LeafTy &LHS, const LeafTy &RHS) {
    LeafTy Copy = LHS;
    return Copy += RHS;
  }

  /// Subtract \p RHS from \p LHS when both have compatible fixed/scalable
  /// kinds; asserts if the operands cannot be combined.
  ///
  /// \param LHS Left-hand quantity.
  /// \param RHS Right-hand quantity.
  /// \return Difference of \p LHS and \p RHS.
  friend constexpr LeafTy operator-(const LeafTy &LHS, const LeafTy &RHS) {
    LeafTy Copy = LHS;
    return Copy -= RHS;
  }

  /// Multiply \p LHS by the scalar \p RHS, preserving its fixed/scalable kind.
  ///
  /// \param LHS Quantity to scale.
  /// \param RHS Scalar multiplier.
  /// \return \p LHS scaled by \p RHS.
  friend constexpr LeafTy operator*(const LeafTy &LHS, ScalarTy RHS) {
    LeafTy Copy = LHS;
    return Copy *= RHS;
  }

  /// Negate \p LHS when the scalar type is signed.
  ///
  /// \param LHS Quantity to negate.
  /// \return Negated copy of \p LHS.
  template <typename U = ScalarTy>
  friend constexpr std::enable_if_t<std::is_signed_v<U>, LeafTy>
  operator-(const LeafTy &LHS) {
    LeafTy Copy = LHS;
    return Copy *= -1;
  }

public:
  /// Return true if quantity and scalable kind equal those of \p RHS.
  ///
  /// \param RHS Quantity to compare against.
  /// \return True if quantity and scalable kind match \p RHS.
  constexpr bool operator==(const FixedOrScalableQuantity &RHS) const {
    return Quantity == RHS.Quantity && Scalable == RHS.Scalable;
  }

  /// Return true if quantity or scalable kind differs from \p RHS.
  ///
  /// \param RHS Quantity to compare against.
  /// \return True if quantity or scalable kind differs from \p RHS.
  constexpr bool operator!=(const FixedOrScalableQuantity &RHS) const {
    return Quantity != RHS.Quantity || Scalable != RHS.Scalable;
  }

  /// Return true if the known-minimum coefficient is zero.
  ///
  /// \return True if the known-minimum coefficient is zero.
  constexpr bool isZero() const { return Quantity == 0; }

  /// Return true if the underlying coefficient is non-zero.
  ///
  /// \return True if the known-minimum coefficient is non-zero.
  constexpr bool isNonZero() const { return Quantity != 0; }

  /// Return true if the quantity is non-zero.
  ///
  /// \return True if the known-minimum coefficient is non-zero.
  explicit operator bool() const { return isNonZero(); }

  /// Add \p RHS to the underlying quantity.
  ///
  /// \param RHS Scalar added to the known-minimum coefficient.
  /// \return Copy of this quantity with \p RHS added to the coefficient.
  constexpr LeafTy getWithIncrement(ScalarTy RHS) const {
    return LeafTy::get(Quantity + RHS, Scalable);
  }

  /// Returns the minimum value this quantity can represent.
  ///
  /// \return Known-minimum coefficient (exact when fixed).
  constexpr ScalarTy getKnownMinValue() const { return Quantity; }

  /// Returns whether the quantity is scaled by a runtime quantity (vscale).
  ///
  /// \return True if the quantity is scaled by vscale.
  constexpr bool isScalable() const { return Scalable; }

  /// Returns true if the quantity is not scaled by vscale.
  ///
  /// \return True if the quantity is fixed-width.
  constexpr bool isFixed() const { return !Scalable; }

  /// Return true if the known-min coefficient is even.
  ///
  /// A return value of true indicates we know at compile time that the number
  /// of elements (vscale * Min) is definitely even. However, returning false
  /// does not guarantee that the total number of elements is odd.
  ///
  /// \return True if the known-min coefficient is even.
  constexpr bool isKnownEven() const { return (getKnownMinValue() & 0x1) == 0; }

  /// Return true if this quantity is a known multiple of scalar \p RHS.
  ///
  /// This function tells the caller whether the element count is known at
  /// compile time to be a multiple of the scalar value RHS.
  ///
  /// \param RHS Scalar divisor to test against.
  /// \return True if the known-min coefficient is a multiple of \p RHS.
  constexpr bool isKnownMultipleOf(ScalarTy RHS) const {
    return RHS != 0 && getKnownMinValue() % RHS == 0;
  }

  /// Return true if this quantity is a known multiple of \p RHS.
  ///
  /// \param RHS Quantity whose known-min coefficient is the divisor.
  /// \return True if this quantity is a compile-time multiple of \p RHS.
  constexpr bool isKnownMultipleOf(const FixedOrScalableQuantity &RHS) const {
    // x % y == 0 => x % y == 0
    // x % y == 0 => (vscale * x) % y == 0
    // x % y == 0 => (vscale * x) % (vscale * y) == 0
    // but
    // x % y == 0 !=> x % (vscale * y) == 0
    if (!isScalable() && RHS.isScalable())
      return false;
    return RHS.getKnownMinValue() != 0 &&
           getKnownMinValue() % RHS.getKnownMinValue() == 0;
  }

  // Return the minimum value with the assumption that the count is exact.
  // Use in places where a scalable count doesn't make sense (e.g. non-vector
  // types, or vectors in backends which don't support scalable vectors).
  /// Return the compile-time quantity when \p this is fixed or zero; asserts
  /// if called on a non-zero scalable quantity.
  ///
  /// \return Exact fixed coefficient, or zero when the quantity is zero.
  constexpr ScalarTy getFixedValue() const {
    assert((!isScalable() || isZero()) &&
           "Request for a fixed element count on a scalable object");
    return getKnownMinValue();
  }

  // For some cases, quantity ordering between scalable and fixed quantity types
  // cannot be determined at compile time, so such comparisons aren't allowed.
  //
  // e.g. <vscale x 2 x i16> could be bigger than <4 x i32> with a runtime
  // vscale >= 5, equal sized with a vscale of 4, and smaller with
  // a vscale <= 3.
  //
  // All the functions below make use of the fact vscale is always >= 1, which
  // means that <vscale x 4 x i32> is guaranteed to be >= <4 x i32>, etc.

  /// Return true when \p LHS is definitely less than \p RHS given that
  /// \c vscale >= 1; returns false when the relation cannot be proved.
  ///
  /// \param LHS Left-hand quantity.
  /// \param RHS Right-hand quantity.
  /// \return True if \p LHS is known less than \p RHS at compile time.
  static constexpr bool isKnownLT(const FixedOrScalableQuantity &LHS,
                                  const FixedOrScalableQuantity &RHS) {
    if (!LHS.isScalable() || RHS.isScalable())
      return LHS.getKnownMinValue() < RHS.getKnownMinValue();
    return false;
  }

  /// Return true when \p LHS is definitely greater than \p RHS given that
  /// \c vscale >= 1; returns false when the relation cannot be proved.
  ///
  /// \param LHS Left-hand quantity.
  /// \param RHS Right-hand quantity.
  /// \return True if \p LHS is known greater than \p RHS at compile time.
  static constexpr bool isKnownGT(const FixedOrScalableQuantity &LHS,
                                  const FixedOrScalableQuantity &RHS) {
    if (LHS.isScalable() || !RHS.isScalable())
      return LHS.getKnownMinValue() > RHS.getKnownMinValue();
    return false;
  }

  /// Return true when \p LHS is definitely less than or equal to \p RHS given
  /// that \c vscale >= 1; returns false when the relation cannot be proved.
  ///
  /// \param LHS Left-hand quantity.
  /// \param RHS Right-hand quantity.
  /// \return True if \p LHS is known less than or equal to \p RHS.
  static constexpr bool isKnownLE(const FixedOrScalableQuantity &LHS,
                                  const FixedOrScalableQuantity &RHS) {
    if (!LHS.isScalable() || RHS.isScalable())
      return LHS.getKnownMinValue() <= RHS.getKnownMinValue();
    return false;
  }

  /// Return true when \p LHS is definitely greater than or equal to \p RHS
  /// given that \c vscale >= 1; returns false when the relation cannot be proved.
  ///
  /// \param LHS Left-hand quantity.
  /// \param RHS Right-hand quantity.
  /// \return True if \p LHS is known greater than or equal to \p RHS.
  static constexpr bool isKnownGE(const FixedOrScalableQuantity &LHS,
                                  const FixedOrScalableQuantity &RHS) {
    if (LHS.isScalable() || !RHS.isScalable())
      return LHS.getKnownMinValue() >= RHS.getKnownMinValue();
    return false;
  }

  /// Divide the known-min coefficient by \p RHS.
  ///
  /// We do not provide the '/' operator here because division for polynomial
  /// types does not work in the same way as for normal integer types. We can
  /// only divide the minimum value (or coefficient) by RHS, which is not the
  /// same as
  ///   (Min * Vscale) / RHS
  /// The caller is recommended to use this function in combination with
  /// isKnownMultipleOf(RHS), which lets the caller know if it's possible to
  /// perform a lossless divide by RHS.
  ///
  /// \param RHS Scalar divisor for the known-min coefficient.
  /// \return Quantity with the known-min coefficient divided by \p RHS.
  constexpr LeafTy divideCoefficientBy(ScalarTy RHS) const {
    return LeafTy::get(getKnownMinValue() / RHS, isScalable());
  }

  /// Multiply the known-min coefficient by \p RHS, preserving fixed/scalable kind.
  ///
  /// \param RHS Scalar multiplier for the known-min coefficient.
  /// \return Quantity with the known-min coefficient multiplied by \p RHS.
  constexpr LeafTy multiplyCoefficientBy(ScalarTy RHS) const {
    return LeafTy::get(getKnownMinValue() * RHS, isScalable());
  }

  /// Round the known-min coefficient up to the next power of two.
  ///
  /// \return Quantity with the known-min coefficient rounded up to a power of two.
  constexpr LeafTy coefficientNextPowerOf2() const {
    return LeafTy::get(
        static_cast<ScalarTy>(llvm::NextPowerOf2(getKnownMinValue())),
        isScalable());
  }

  /// Returns true if there exists a value X where RHS.multiplyCoefficientBy(X)
  /// will result in a value whose quantity matches our own.
  ///
  /// \param RHS Candidate scalar factor quantity.
  /// \return True if \p RHS is a known scalar factor of this quantity.
  constexpr bool
  hasKnownScalarFactor(const FixedOrScalableQuantity &RHS) const {
    return isScalable() == RHS.isScalable() &&
           getKnownMinValue() % RHS.getKnownMinValue() == 0;
  }

  /// Returns a value X where RHS.multiplyCoefficientBy(X) will result in a
  /// value whose quantity matches our own.
  ///
  /// \param RHS Quantity that divides this known-min coefficient.
  /// \return Scalar factor X such that RHS.multiplyCoefficientBy(X) equals this.
  constexpr ScalarTy
  getKnownScalarFactor(const FixedOrScalableQuantity &RHS) const {
    assert(hasKnownScalarFactor(RHS) && "Expected RHS to be a known factor!");
    return getKnownMinValue() / RHS.getKnownMinValue();
  }

  /// Print this quantity to \p OS.
  ///
  /// \param OS Stream to write to.
  void print(raw_ostream &OS) const {
    if (isScalable())
      OS << "vscale x ";
    OS << getKnownMinValue();
  }
};

} // namespace details

/// Describes how many elements a type holds, either fixed or scaled by vscale.
///
/// Examples:
///  - ElementCount::getFixed(1) : A scalar value.
///  - ElementCount::getFixed(2) : A vector type holding 2 values.
///  - ElementCount::getScalable(4) : A scalable vector type holding 4 values.
class ElementCount
    : public details::FixedOrScalableQuantity<ElementCount, unsigned> {
  constexpr ElementCount(ScalarTy MinVal, bool Scalable)
      : FixedOrScalableQuantity(MinVal, Scalable) {}

  constexpr ElementCount(
      const FixedOrScalableQuantity<ElementCount, unsigned> &V)
      : FixedOrScalableQuantity(V) {}

public:
  /// Construct a zero fixed element count.
  constexpr ElementCount() : FixedOrScalableQuantity() {}

  /// Return a fixed (non-vscale) element count of \p MinVal.
  ///
  /// \param MinVal Exact number of elements.
  /// \return Fixed element count of \p MinVal.
  static constexpr ElementCount getFixed(ScalarTy MinVal) {
    return ElementCount(MinVal, false);
  }
  /// Return a scalable element count with known-minimum \p MinVal.
  ///
  /// \param MinVal Known-minimum number of elements (coefficient of vscale).
  /// \return Scalable element count with known-minimum \p MinVal.
  static constexpr ElementCount getScalable(ScalarTy MinVal) {
    return ElementCount(MinVal, true);
  }
  /// Return an element count with minimum value \p MinVal, optionally scalable.
  ///
  /// \param MinVal Known-minimum number of elements.
  /// \param Scalable Whether the count is scaled by vscale.
  /// \return Element count with the given minimum and scalability.
  static constexpr ElementCount get(ScalarTy MinVal, bool Scalable) {
    return ElementCount(MinVal, Scalable);
  }

  /// Exactly one element.
  ///
  /// \return True if this is a fixed count of exactly one element.
  constexpr bool isScalar() const {
    return !isScalable() && getKnownMinValue() == 1;
  }
  /// One or more elements.
  ///
  /// \return True if this describes a vector (scalable non-zero or fixed > 1).
  constexpr bool isVector() const {
    return (isScalable() && getKnownMinValue() != 0) || getKnownMinValue() > 1;
  }
};

/// Represents the size of a type as an exact size or a known minimum.
///
/// If the type is of fixed size, it will represent the exact size. If the type
/// is a scalable vector, it will represent the known minimum size.
class TypeSize : public details::FixedOrScalableQuantity<TypeSize, uint64_t> {
  TypeSize(const FixedOrScalableQuantity<TypeSize, uint64_t> &V)
      : FixedOrScalableQuantity(V) {}

public:
  /// Construct a type size with known-minimum \p Quantity, optionally scaled by vscale.
  ///
  /// \param Quantity Known-minimum size in bits or bytes (caller convention).
  /// \param Scalable Whether the size is scaled by vscale.
  constexpr TypeSize(ScalarTy Quantity, bool Scalable)
      : FixedOrScalableQuantity(Quantity, Scalable) {}

  /// Return a type size with known-minimum \p Quantity, optionally scaled by vscale.
  ///
  /// \param Quantity Known-minimum size.
  /// \param Scalable Whether the size is scaled by vscale.
  /// \return TypeSize with the given known-minimum and scalability.
  static constexpr TypeSize get(ScalarTy Quantity, bool Scalable) {
    return TypeSize(Quantity, Scalable);
  }
  /// Return an exact fixed-width type size.
  ///
  /// \param ExactSize Exact fixed size.
  /// \return Fixed TypeSize of \p ExactSize.
  static constexpr TypeSize getFixed(ScalarTy ExactSize) {
    return TypeSize(ExactSize, false);
  }
  /// Return a scalable type size with known-minimum \p MinimumSize.
  ///
  /// \param MinimumSize Known-minimum size (coefficient of vscale).
  /// \return Scalable TypeSize with known-minimum \p MinimumSize.
  static constexpr TypeSize getScalable(ScalarTy MinimumSize) {
    return TypeSize(MinimumSize, true);
  }
  /// Return a zero-sized fixed TypeSize.
  ///
  /// \return Fixed TypeSize of zero.
  static constexpr TypeSize getZero() { return TypeSize(0, false); }

  // All code for this class below this point is needed because of the
  // temporary implicit conversion to uint64_t. The operator overloads are
  // needed because otherwise the conversion of the parent class
  // UnivariateLinearPolyBase -> TypeSize is ambiguous.
  // TODO: Remove the implicit conversion.

  // Casts to a uint64_t if this is a fixed-width size.
  //
  // This interface is deprecated and will be removed in a future version
  // of LLVM in favour of upgrading uses that rely on this implicit conversion
  // to uint64_t. Calls to functions that return a TypeSize should use the
  // proper interfaces to TypeSize.
  // In practice this is mostly calls to MVT/EVT::getSizeInBits().
  //
  // To determine how to upgrade the code:
  //
  //   if (<algorithm works for both scalable and fixed-width vectors>)
  //     use getKnownMinValue()
  //   else if (<algorithm works only for fixed-width vectors>) {
  //     if <algorithm can be adapted for both scalable and fixed-width vectors>
  //       update the algorithm and use getKnownMinValue()
  //     else
  //       bail out early for scalable vectors and use getFixedValue()
  //   }
  /// Implicitly convert a fixed-width size to its scalar coefficient; fatal on scalable sizes.
  ///
  /// \return Exact fixed scalar size; reports a fatal error if scalable.
  operator ScalarTy() const {
    if (isScalable()) {
      reportFatalInternalError(
          "Cannot implicitly convert a scalable size to a fixed-width size in "
          "`TypeSize::operator ScalarTy()`");
    }
    return getFixedValue();
  }

  // Additional operators needed to avoid ambiguous parses
  // because of the implicit conversion hack.
  /// Multiply \p LHS by the signed int scalar \p RHS.
  ///
  /// \param LHS Type size to scale.
  /// \param RHS Signed int multiplier.
  /// \return \p LHS scaled by \p RHS.
  friend constexpr TypeSize operator*(const TypeSize &LHS, const int RHS) {
    return LHS * (ScalarTy)RHS;
  }
  /// Multiply \p LHS by the unsigned int scalar \p RHS.
  ///
  /// \param LHS Type size to scale.
  /// \param RHS Unsigned int multiplier.
  /// \return \p LHS scaled by \p RHS.
  friend constexpr TypeSize operator*(const TypeSize &LHS, const unsigned RHS) {
    return LHS * (ScalarTy)RHS;
  }
  /// Multiply \p LHS by the signed 64-bit scalar \p RHS.
  ///
  /// \param LHS Type size to scale.
  /// \param RHS Signed 64-bit multiplier.
  /// \return \p LHS scaled by \p RHS.
  friend constexpr TypeSize operator*(const TypeSize &LHS, const int64_t RHS) {
    return LHS * (ScalarTy)RHS;
  }
  /// Multiply signed integer \p LHS by TypeSize \p RHS.
  ///
  /// \param LHS Signed int multiplier.
  /// \param RHS Type size to scale.
  /// \return \p RHS scaled by \p LHS.
  friend constexpr TypeSize operator*(const int LHS, const TypeSize &RHS) {
    return RHS * LHS;
  }
  /// Multiply unsigned integer \p LHS by TypeSize \p RHS.
  ///
  /// \param LHS Unsigned int multiplier.
  /// \param RHS Type size to scale.
  /// \return \p RHS scaled by \p LHS.
  friend constexpr TypeSize operator*(const unsigned LHS, const TypeSize &RHS) {
    return RHS * LHS;
  }
  /// Multiply signed 64-bit integer \p LHS by TypeSize \p RHS.
  ///
  /// \param LHS Signed 64-bit multiplier.
  /// \param RHS Type size to scale.
  /// \return \p RHS scaled by \p LHS.
  friend constexpr TypeSize operator*(const int64_t LHS, const TypeSize &RHS) {
    return RHS * LHS;
  }
  /// Multiply unsigned 64-bit integer \p LHS by TypeSize \p RHS.
  ///
  /// \param LHS Unsigned 64-bit multiplier.
  /// \param RHS Type size to scale.
  /// \return \p RHS scaled by \p LHS.
  friend constexpr TypeSize operator*(const uint64_t LHS, const TypeSize &RHS) {
    return RHS * LHS;
  }
};

//===----------------------------------------------------------------------===//
// Utilities
//===----------------------------------------------------------------------===//

/// Round \p Size up to a multiple of \p Align.
///
/// Returns a TypeSize with a known minimum size that is the next integer
/// (mod 2**64) that is greater than or equal to \p Size and is a multiple
/// of \p Align. \p Align must be non-zero.
///
/// Similar to the alignTo functions in MathExtras.h
///
/// \param Size Quantity whose known-min size is rounded up.
/// \param Align Non-zero alignment in the same units as \p Size.
/// \return TypeSize with known-min rounded up to a multiple of \p Align.
inline constexpr TypeSize alignTo(TypeSize Size, uint64_t Align) {
  assert(Align != 0u && "Align must be non-zero");
  return {(Size.getKnownMinValue() + Align - 1) / Align * Align,
          Size.isScalable()};
}

/// Stream operator function for `FixedOrScalableQuantity`.
///
/// \param OS Stream to write to.
/// \param PS Quantity to print.
/// \return Reference to \p OS after printing.
template <typename LeafTy, typename ScalarTy>
inline raw_ostream &
operator<<(raw_ostream &OS,
           const details::FixedOrScalableQuantity<LeafTy, ScalarTy> &PS) {
  PS.print(OS);
  return OS;
}

/// DenseMapInfo specialization for ElementCount keys.
template <> struct DenseMapInfo<ElementCount, void> {
  /// Compute a DenseMap hash for \p EltCnt.
  ///
  /// \param EltCnt Element count to hash.
  /// \return Hash value suitable for DenseMap.
  static unsigned getHashValue(const ElementCount &EltCnt) {
    unsigned HashVal = EltCnt.getKnownMinValue() * 37U;
    if (EltCnt.isScalable())
      return (HashVal - 1U);

    return HashVal;
  }
  /// Return true if \p LHS and \p RHS are equal.
  ///
  /// \param LHS Left-hand element count.
  /// \param RHS Right-hand element count.
  /// \return True if \p LHS and \p RHS are equal.
  static bool isEqual(const ElementCount &LHS, const ElementCount &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

#endif // LLVM_SUPPORT_TYPESIZE_H
