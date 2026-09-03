//===- DynamicAPInt.h - DynamicAPInt Class ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a simple class to represent arbitrary precision signed integers.
// Unlike APInt, one does not have to specify a fixed maximum size, and the
// integer can take on any arbitrary values. This is optimized for small-values
// by providing fast-paths for the cases when the value stored fits in 64-bits.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_DYNAMICAPINT_H
#define LLVM_ADT_DYNAMICAPINT_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SlowDynamicAPInt.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include <numeric>

namespace llvm {

class raw_ostream;

/// This class provides support for dynamic arbitrary-precision arithmetic.
///
/// Unlike APInt, this extends the precision as necessary to prevent overflows
/// and supports operations between objects with differing internal precisions.
///
/// This is optimized for small-values by providing fast-paths for the cases
/// when the value stored fits in 64-bits. We annotate all fastpaths by using
/// the LLVM_LIKELY/LLVM_UNLIKELY annotations. Removing these would result in
/// a 1.2x performance slowdown.
///
/// We always_inline all operations; removing these results in a 1.5x
/// performance slowdown.
///
/// When isLarge returns true, a SlowMPInt is held in the union. If isSmall
/// returns true, the int64_t is held. We don't have a separate field for
/// indicating this, and instead "steal" memory from ValLarge when it is not in
/// use because we know that the memory layout of APInt is such that BitWidth
/// doesn't overlap with ValSmall (see static_assert_layout). Using std::variant
/// instead would lead to significantly worse performance.
class DynamicAPInt {
  union {
    int64_t ValSmall;
    detail::SlowDynamicAPInt ValLarge;
  };

  LLVM_ATTRIBUTE_ALWAYS_INLINE void initSmall(int64_t O) {
    if (LLVM_UNLIKELY(isLarge()))
      ValLarge.detail::SlowDynamicAPInt::~SlowDynamicAPInt();
    ValSmall = O;
    ValLarge.Val.BitWidth = 0;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE void
  initLarge(const detail::SlowDynamicAPInt &O) {
    if (LLVM_LIKELY(isSmall())) {
      // The data in memory could be in an arbitrary state, not necessarily
      // corresponding to any valid state of ValLarge; we cannot call any member
      // functions, e.g. the assignment operator on it, as they may access the
      // invalid internal state. We instead construct a new object using
      // placement new.
      new (&ValLarge) detail::SlowDynamicAPInt(O);
    } else {
      // In this case, we need to use the assignment operator, because if we use
      // placement-new as above we would lose track of allocated memory
      // and leak it.
      ValLarge = O;
    }
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE explicit DynamicAPInt(
      const detail::SlowDynamicAPInt &Val)
      : ValLarge(Val) {}
  LLVM_ATTRIBUTE_ALWAYS_INLINE constexpr bool isSmall() const {
    return ValLarge.Val.BitWidth == 0;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE constexpr bool isLarge() const {
    return !isSmall();
  }
  /// Get the stored value. For getSmall/Large,
  /// the stored value should be small/large.
  LLVM_ATTRIBUTE_ALWAYS_INLINE int64_t getSmall() const {
    assert(isSmall() &&
           "getSmall should only be called when the value stored is small!");
    return ValSmall;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE int64_t &getSmall() {
    assert(isSmall() &&
           "getSmall should only be called when the value stored is small!");
    return ValSmall;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE const detail::SlowDynamicAPInt &
  getLarge() const {
    assert(isLarge() &&
           "getLarge should only be called when the value stored is large!");
    return ValLarge;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE detail::SlowDynamicAPInt &getLarge() {
    assert(isLarge() &&
           "getLarge should only be called when the value stored is large!");
    return ValLarge;
  }
  explicit operator detail::SlowDynamicAPInt() const {
    if (isSmall())
      return detail::SlowDynamicAPInt(getSmall());
    return getLarge();
  }

public:
  LLVM_ATTRIBUTE_ALWAYS_INLINE explicit DynamicAPInt(int64_t Val)
      : ValSmall(Val) {
    ValLarge.Val.BitWidth = 0;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE explicit DynamicAPInt(const APInt &Val) {
    if (Val.getBitWidth() <= 64) {
      ValSmall = Val.getSExtValue();
      ValLarge.Val.BitWidth = 0;
    } else {
      new (&ValLarge) detail::SlowDynamicAPInt(Val);
    }
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt() : DynamicAPInt(0) {}
  LLVM_ATTRIBUTE_ALWAYS_INLINE ~DynamicAPInt() {
    if (LLVM_UNLIKELY(isLarge()))
      ValLarge.detail::SlowDynamicAPInt::~SlowDynamicAPInt();
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt(const DynamicAPInt &O)
      : ValSmall(O.ValSmall) {
    ValLarge.Val.BitWidth = 0;
    if (LLVM_UNLIKELY(O.isLarge()))
      initLarge(O.ValLarge);
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator=(const DynamicAPInt &O) {
    if (LLVM_LIKELY(O.isSmall())) {
      initSmall(O.ValSmall);
      return *this;
    }
    initLarge(O.ValLarge);
    return *this;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator=(int X) {
    initSmall(X);
    return *this;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE explicit operator int64_t() const {
    if (isSmall())
      return getSmall();
    return static_cast<int64_t>(getLarge());
  }

  bool operator==(const DynamicAPInt &O) const;
  bool operator!=(const DynamicAPInt &O) const;
  bool operator>(const DynamicAPInt &O) const;
  bool operator<(const DynamicAPInt &O) const;
  bool operator<=(const DynamicAPInt &O) const;
  /// Returns true if this value is greater than or equal to \p O.
  /// @return True if this value is greater than or equal to \p O.
  bool operator>=(const DynamicAPInt &O) const;
  DynamicAPInt operator+(const DynamicAPInt &O) const;
  DynamicAPInt operator-(const DynamicAPInt &O) const;
  DynamicAPInt operator*(const DynamicAPInt &O) const;
  DynamicAPInt operator/(const DynamicAPInt &O) const;
  DynamicAPInt operator%(const DynamicAPInt &O) const;
  DynamicAPInt &operator+=(const DynamicAPInt &O);
  DynamicAPInt &operator-=(const DynamicAPInt &O);
  DynamicAPInt &operator*=(const DynamicAPInt &O);
  DynamicAPInt &operator/=(const DynamicAPInt &O);
  DynamicAPInt &operator%=(const DynamicAPInt &O);
  DynamicAPInt operator-() const;
  DynamicAPInt &operator++();
  DynamicAPInt &operator--();

  /// Divides by a divisor known to be positive.
  ///
  /// Slightly more efficient than operator/ because it skips an overflow check.
  /// @return Quotient of this value divided by \p O.
  DynamicAPInt divByPositive(const DynamicAPInt &O) const;
  /// Divides by a positive divisor in place.
  ///
  /// Slightly more efficient than operator/= because it skips an overflow check.
  /// @return Reference to this value.
  DynamicAPInt &divByPositiveInPlace(const DynamicAPInt &O);

  /// Returns the absolute value of \p X.
  /// @return Absolute value of \p X.
  friend DynamicAPInt abs(const DynamicAPInt &X);
  /// Returns the ceiling of \p LHS divided by \p RHS.
  /// @return Ceiling of \p LHS divided by \p RHS.
  friend DynamicAPInt ceilDiv(const DynamicAPInt &LHS, const DynamicAPInt &RHS);
  /// Returns the floor of \p LHS divided by \p RHS.
  /// @return Floor of \p LHS divided by \p RHS.
  friend DynamicAPInt floorDiv(const DynamicAPInt &LHS,
                               const DynamicAPInt &RHS);
  /// Returns the greatest common divisor of \p A and \p B.
  ///
  /// Both operands must be non-negative.
  /// @return Greatest common divisor of \p A and \p B.
  friend DynamicAPInt gcd(const DynamicAPInt &A, const DynamicAPInt &B);
  friend DynamicAPInt lcm(const DynamicAPInt &A, const DynamicAPInt &B);
  friend DynamicAPInt mod(const DynamicAPInt &LHS, const DynamicAPInt &RHS);

  // ---------------------------------------------------------------------------
  // Convenience operator overloads for int64_t.
  // ---------------------------------------------------------------------------
  /// Adds \p B to \p A in place.
  /// @return Reference to the updated \p A.
  friend DynamicAPInt &operator+=(DynamicAPInt &A, int64_t B);
  /// Subtracts \p B from \p A in place.
  /// @return Reference to the updated \p A.
  friend DynamicAPInt &operator-=(DynamicAPInt &A, int64_t B);
  /// Multiplies \p A by \p B in place.
  /// @return Reference to the updated \p A.
  friend DynamicAPInt &operator*=(DynamicAPInt &A, int64_t B);
  /// Divides \p A by \p B in place.
  /// @return Reference to the updated \p A.
  friend DynamicAPInt &operator/=(DynamicAPInt &A, int64_t B);
  /// Stores the remainder of \p A divided by \p B in \p A.
  /// @return Reference to the updated \p A.
  friend DynamicAPInt &operator%=(DynamicAPInt &A, int64_t B);

  /// Returns true if \p A is equal to \p B.
  /// @return True if \p A equals \p B.
  friend bool operator==(const DynamicAPInt &A, int64_t B);
  /// Returns true if \p A is not equal to \p B.
  /// @return True if \p A is not equal to \p B.
  friend bool operator!=(const DynamicAPInt &A, int64_t B);
  /// Returns true if \p A is greater than \p B.
  /// @return True if \p A is greater than \p B.
  friend bool operator>(const DynamicAPInt &A, int64_t B);
  /// Returns true if \p A is less than \p B.
  /// @return True if \p A is less than \p B.
  friend bool operator<(const DynamicAPInt &A, int64_t B);
  /// Returns true if \p A is less than or equal to \p B.
  /// @return True if \p A is less than or equal to \p B.
  friend bool operator<=(const DynamicAPInt &A, int64_t B);
  /// Returns true if \p A is greater than or equal to \p B.
  /// @return True if \p A is greater than or equal to \p B.
  friend bool operator>=(const DynamicAPInt &A, int64_t B);
  /// Returns \p A plus \p B.
  /// @return Sum of \p A and \p B.
  friend DynamicAPInt operator+(const DynamicAPInt &A, int64_t B);
  /// Returns \p A minus \p B.
  /// @return Difference of \p A and \p B.
  friend DynamicAPInt operator-(const DynamicAPInt &A, int64_t B);
  /// Returns \p A multiplied by \p B.
  /// @return Product of \p A and \p B.
  friend DynamicAPInt operator*(const DynamicAPInt &A, int64_t B);
  /// Returns \p A divided by \p B.
  /// @return Quotient of \p A divided by \p B.
  friend DynamicAPInt operator/(const DynamicAPInt &A, int64_t B);
  /// Returns the remainder of \p A divided by \p B.
  /// @return Remainder of \p A divided by \p B.
  friend DynamicAPInt operator%(const DynamicAPInt &A, int64_t B);

  /// Returns true if \p A is equal to \p B.
  /// @return True if \p A equals \p B.
  friend bool operator==(int64_t A, const DynamicAPInt &B);
  /// Returns true if \p A is not equal to \p B.
  /// @return True if \p A is not equal to \p B.
  friend bool operator!=(int64_t A, const DynamicAPInt &B);
  /// Returns true if \p A is greater than \p B.
  /// @return True if \p A is greater than \p B.
  friend bool operator>(int64_t A, const DynamicAPInt &B);
  /// Returns true if \p A is less than \p B.
  /// @return True if \p A is less than \p B.
  friend bool operator<(int64_t A, const DynamicAPInt &B);
  /// Returns true if \p A is less than or equal to \p B.
  /// @return True if \p A is less than or equal to \p B.
  friend bool operator<=(int64_t A, const DynamicAPInt &B);
  /// Returns true if \p A is greater than or equal to \p B.
  /// @return True if \p A is greater than or equal to \p B.
  friend bool operator>=(int64_t A, const DynamicAPInt &B);
  /// Returns \p A plus \p B.
  /// @return Sum of \p A and \p B.
  friend DynamicAPInt operator+(int64_t A, const DynamicAPInt &B);
  /// Returns \p A minus \p B.
  /// @return Difference of \p A and \p B.
  friend DynamicAPInt operator-(int64_t A, const DynamicAPInt &B);
  /// Returns \p A multiplied by \p B.
  /// @return Product of \p A and \p B.
  friend DynamicAPInt operator*(int64_t A, const DynamicAPInt &B);
  /// Returns \p A divided by \p B.
  /// @return Quotient of \p A divided by \p B.
  friend DynamicAPInt operator/(int64_t A, const DynamicAPInt &B);
  /// Returns the remainder of \p A divided by \p B.
  /// @return Remainder of \p A divided by \p B.
  friend DynamicAPInt operator%(int64_t A, const DynamicAPInt &B);

  /// Computes a hash code for \p x.
  ///
  /// \param x Value to hash.
  /// @return Hash code for \p x.
  LLVM_ABI friend hash_code hash_value(const DynamicAPInt &x); // NOLINT

  LLVM_ABI void static_assert_layout(); // NOLINT

  LLVM_ABI raw_ostream &print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() const;
#endif
};

/// Prints \p X to \p OS.
///
/// \param OS Stream to write to.
/// \param X Value to print.
/// @return Reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const DynamicAPInt &X) {
  X.print(OS);
  return OS;
}

/// Computes a hash code for \p x.
///
/// Redeclaration of the friend above to make it discoverable by lookups.
///
/// \param x Value to hash.
/// @return Hash code for \p x.
LLVM_ABI hash_code hash_value(const DynamicAPInt &x); // NOLINT

/// This just calls through to the operator int64_t, but it's useful when a
/// function pointer is required. (Although this is marked inline, it is still
/// possible to obtain and use a function pointer to this.)
static inline int64_t int64fromDynamicAPInt(const DynamicAPInt &X) {
  return int64_t(X);
}
/// Constructs a DynamicAPInt from an \c int64_t value.
///
/// Useful when a function pointer is required.
///
/// \param X Value to convert.
/// @return DynamicAPInt holding \p X.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt dynamicAPIntFromInt64(int64_t X) {
  return DynamicAPInt(X);
}

/// Returns the remainder of \p LHS divided by \p RHS.
///
/// The RHS is always expected to be positive, and the result is always
/// non-negative.
///
/// \param LHS Dividend.
/// \param RHS Positive divisor.
/// @return Non-negative remainder of \p LHS divided by \p RHS.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt mod(const DynamicAPInt &LHS,
                                              const DynamicAPInt &RHS);

// We define the operations here in the header to facilitate inlining.

// ---------------------------------------------------------------------------
// Comparison operators.
// ---------------------------------------------------------------------------
/// Returns true if the value is equal to \p O.
///
/// \param O Value to compare against.
/// @return True if the value equals \p O.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool
DynamicAPInt::operator==(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return getSmall() == O.getSmall();
  return detail::SlowDynamicAPInt(*this) == detail::SlowDynamicAPInt(O);
}
/// Returns true if the value is not equal to \p O.
///
/// \param O Value to compare against.
/// @return True if the value is not equal to \p O.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool
DynamicAPInt::operator!=(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return getSmall() != O.getSmall();
  return detail::SlowDynamicAPInt(*this) != detail::SlowDynamicAPInt(O);
}
/// Returns true if the value is greater than \p O.
///
/// \param O Value to compare against.
/// @return True if the value is greater than \p O.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool
DynamicAPInt::operator>(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return getSmall() > O.getSmall();
  return detail::SlowDynamicAPInt(*this) > detail::SlowDynamicAPInt(O);
}
/// Returns true if the value is less than \p O.
///
/// \param O Value to compare against.
/// @return True if the value is less than \p O.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool
DynamicAPInt::operator<(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return getSmall() < O.getSmall();
  return detail::SlowDynamicAPInt(*this) < detail::SlowDynamicAPInt(O);
}
/// Returns true if the value is less than or equal to \p O.
///
/// \param O Value to compare against.
/// @return True if the value is less than or equal to \p O.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool
DynamicAPInt::operator<=(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return getSmall() <= O.getSmall();
  return detail::SlowDynamicAPInt(*this) <= detail::SlowDynamicAPInt(O);
}
/// Returns true if the value is greater than or equal to \p O.
///
/// \param O Value to compare against.
/// @return True if the value is greater than or equal to \p O.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool
DynamicAPInt::operator>=(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return getSmall() >= O.getSmall();
  return detail::SlowDynamicAPInt(*this) >= detail::SlowDynamicAPInt(O);
}

// ---------------------------------------------------------------------------
// Arithmetic operators.
// ---------------------------------------------------------------------------

/// Returns the sum of this value and \p O.
///
/// \param O Value to add.
/// @return Sum of this value and \p O.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt
DynamicAPInt::operator+(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    DynamicAPInt Result;
    bool Overflow = AddOverflow(getSmall(), O.getSmall(), Result.getSmall());
    if (LLVM_LIKELY(!Overflow))
      return Result;
    return DynamicAPInt(detail::SlowDynamicAPInt(*this) +
                        detail::SlowDynamicAPInt(O));
  }
  return DynamicAPInt(detail::SlowDynamicAPInt(*this) +
                      detail::SlowDynamicAPInt(O));
}
/// Returns the difference of this value and \p O.
///
/// \param O Value to subtract.
/// @return Difference of this value and \p O.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt
DynamicAPInt::operator-(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    DynamicAPInt Result;
    bool Overflow = SubOverflow(getSmall(), O.getSmall(), Result.getSmall());
    if (LLVM_LIKELY(!Overflow))
      return Result;
    return DynamicAPInt(detail::SlowDynamicAPInt(*this) -
                        detail::SlowDynamicAPInt(O));
  }
  return DynamicAPInt(detail::SlowDynamicAPInt(*this) -
                      detail::SlowDynamicAPInt(O));
}
/// Returns the product of this value and \p O.
///
/// \param O Value to multiply by.
/// @return Product of this value and \p O.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt
DynamicAPInt::operator*(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    DynamicAPInt Result;
    bool Overflow = MulOverflow(getSmall(), O.getSmall(), Result.getSmall());
    if (LLVM_LIKELY(!Overflow))
      return Result;
    return DynamicAPInt(detail::SlowDynamicAPInt(*this) *
                        detail::SlowDynamicAPInt(O));
  }
  return DynamicAPInt(detail::SlowDynamicAPInt(*this) *
                      detail::SlowDynamicAPInt(O));
}

/// Divides by a divisor known to be positive.
///
/// Slightly more efficient than operator/ because it skips an overflow check.
///
/// \param O Positive divisor.
/// @return Quotient of this value divided by \p O.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt
DynamicAPInt::divByPositive(const DynamicAPInt &O) const {
  assert(O > 0);
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return DynamicAPInt(getSmall() / O.getSmall());
  return DynamicAPInt(detail::SlowDynamicAPInt(*this) /
                      detail::SlowDynamicAPInt(O));
}

/// Divides by \p O using signed truncating division.
///
/// Division overflows only occur when negating the minimal possible value.
///
/// \param O Divisor.
/// @return Quotient of this value divided by \p O.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt
DynamicAPInt::operator/(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    // Division overflows only occur when negating the minimal possible value.
    if (LLVM_UNLIKELY(divideSignedWouldOverflow(getSmall(), O.getSmall())))
      return -*this;
    return DynamicAPInt(getSmall() / O.getSmall());
  }
  return DynamicAPInt(detail::SlowDynamicAPInt(*this) /
                      detail::SlowDynamicAPInt(O));
}

/// Returns the absolute value of \p X.
///
/// \param X Value whose absolute value is returned.
/// @return Absolute value of \p X.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt abs(const DynamicAPInt &X) {
  return DynamicAPInt(X >= 0 ? X : -X);
}
/// Returns the ceiling of \p LHS divided by \p RHS.
///
/// \param LHS Dividend.
/// \param RHS Divisor.
/// @return Ceiling of \p LHS divided by \p RHS.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt ceilDiv(const DynamicAPInt &LHS,
                                                  const DynamicAPInt &RHS) {
  if (LLVM_LIKELY(LHS.isSmall() && RHS.isSmall())) {
    if (LLVM_UNLIKELY(
            divideSignedWouldOverflow(LHS.getSmall(), RHS.getSmall())))
      return -LHS;
    return DynamicAPInt(divideCeilSigned(LHS.getSmall(), RHS.getSmall()));
  }
  return DynamicAPInt(
      ceilDiv(detail::SlowDynamicAPInt(LHS), detail::SlowDynamicAPInt(RHS)));
}
/// Returns the floor of \p LHS divided by \p RHS.
///
/// \param LHS Dividend.
/// \param RHS Divisor.
/// @return Floor of \p LHS divided by \p RHS.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt floorDiv(const DynamicAPInt &LHS,
                                                   const DynamicAPInt &RHS) {
  if (LLVM_LIKELY(LHS.isSmall() && RHS.isSmall())) {
    if (LLVM_UNLIKELY(
            divideSignedWouldOverflow(LHS.getSmall(), RHS.getSmall())))
      return -LHS;
    return DynamicAPInt(divideFloorSigned(LHS.getSmall(), RHS.getSmall()));
  }
  return DynamicAPInt(
      floorDiv(detail::SlowDynamicAPInt(LHS), detail::SlowDynamicAPInt(RHS)));
}
/// Returns the remainder of \p LHS divided by \p RHS.
///
/// The RHS is always expected to be positive, and the result is always
/// non-negative.
///
/// \param LHS Dividend.
/// \param RHS Positive divisor.
/// @return Non-negative remainder of \p LHS divided by \p RHS.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt mod(const DynamicAPInt &LHS,
                                              const DynamicAPInt &RHS) {
  if (LLVM_LIKELY(LHS.isSmall() && RHS.isSmall()))
    return DynamicAPInt(mod(LHS.getSmall(), RHS.getSmall()));
  return DynamicAPInt(
      mod(detail::SlowDynamicAPInt(LHS), detail::SlowDynamicAPInt(RHS)));
}

/// Returns the greatest common divisor of \p A and \p B.
///
/// Both operands must be non-negative.
///
/// \param A First non-negative operand.
/// \param B Second non-negative operand.
/// @return Greatest common divisor of \p A and \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt gcd(const DynamicAPInt &A,
                                              const DynamicAPInt &B) {
  assert(A >= 0 && B >= 0 && "operands must be non-negative!");
  if (LLVM_LIKELY(A.isSmall() && B.isSmall()))
    return DynamicAPInt(std::gcd(A.getSmall(), B.getSmall()));
  return DynamicAPInt(
      gcd(detail::SlowDynamicAPInt(A), detail::SlowDynamicAPInt(B)));
}

/// Returns the least common multiple of \p A and \p B.
///
/// \param A First operand.
/// \param B Second operand.
/// @return Least common multiple of \p A and \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt lcm(const DynamicAPInt &A,
                                              const DynamicAPInt &B) {
  DynamicAPInt X = abs(A);
  DynamicAPInt Y = abs(B);
  return (X * Y) / gcd(X, Y);
}

/// Returns the remainder of this value divided by \p O.
///
/// This operation cannot overflow.
///
/// \param O Divisor.
/// @return Remainder of this value divided by \p O.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt
DynamicAPInt::operator%(const DynamicAPInt &O) const {
  if (LLVM_LIKELY(isSmall() && O.isSmall()))
    return DynamicAPInt(getSmall() % O.getSmall());
  return DynamicAPInt(detail::SlowDynamicAPInt(*this) %
                      detail::SlowDynamicAPInt(O));
}

/// Returns the negation of this value.
/// @return Negation of this value.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt DynamicAPInt::operator-() const {
  if (LLVM_LIKELY(isSmall())) {
    if (LLVM_LIKELY(getSmall() != std::numeric_limits<int64_t>::min()))
      return DynamicAPInt(-getSmall());
    return DynamicAPInt(-detail::SlowDynamicAPInt(*this));
  }
  return DynamicAPInt(-detail::SlowDynamicAPInt(*this));
}

// ---------------------------------------------------------------------------
// Assignment operators, preincrement, predecrement.
// ---------------------------------------------------------------------------
/// Adds \p O to this value in place.
///
/// \param O Value to add.
/// @return Reference to this value.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &
DynamicAPInt::operator+=(const DynamicAPInt &O) {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    int64_t Result = getSmall();
    bool Overflow = AddOverflow(getSmall(), O.getSmall(), Result);
    if (LLVM_LIKELY(!Overflow)) {
      getSmall() = Result;
      return *this;
    }
    // Note: this return is not strictly required but
    // removing it leads to a performance regression.
    return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) +
                                detail::SlowDynamicAPInt(O));
  }
  return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) +
                              detail::SlowDynamicAPInt(O));
}
/// Subtracts \p O from this value in place.
///
/// \param O Value to subtract.
/// @return Reference to this value.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &
DynamicAPInt::operator-=(const DynamicAPInt &O) {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    int64_t Result = getSmall();
    bool Overflow = SubOverflow(getSmall(), O.getSmall(), Result);
    if (LLVM_LIKELY(!Overflow)) {
      getSmall() = Result;
      return *this;
    }
    // Note: this return is not strictly required but
    // removing it leads to a performance regression.
    return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) -
                                detail::SlowDynamicAPInt(O));
  }
  return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) -
                              detail::SlowDynamicAPInt(O));
}
/// Multiplies this value by \p O in place.
///
/// \param O Value to multiply by.
/// @return Reference to this value.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &
DynamicAPInt::operator*=(const DynamicAPInt &O) {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    int64_t Result = getSmall();
    bool Overflow = MulOverflow(getSmall(), O.getSmall(), Result);
    if (LLVM_LIKELY(!Overflow)) {
      getSmall() = Result;
      return *this;
    }
    // Note: this return is not strictly required but
    // removing it leads to a performance regression.
    return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) *
                                detail::SlowDynamicAPInt(O));
  }
  return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) *
                              detail::SlowDynamicAPInt(O));
}
/// Divides this value by \p O in place.
///
/// Division overflows only occur when negating the minimal possible value.
///
/// \param O Divisor.
/// @return Reference to this value.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &
DynamicAPInt::operator/=(const DynamicAPInt &O) {
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    // Division overflows only occur when negating the minimal possible value.
    if (LLVM_UNLIKELY(divideSignedWouldOverflow(getSmall(), O.getSmall())))
      return *this = -*this;
    getSmall() /= O.getSmall();
    return *this;
  }
  return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) /
                              detail::SlowDynamicAPInt(O));
}

/// Divides by a positive divisor in place.
///
/// Slightly more efficient than operator/= because it skips an overflow check.
///
/// \param O Positive divisor.
/// @return Reference to this value.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &
DynamicAPInt::divByPositiveInPlace(const DynamicAPInt &O) {
  assert(O > 0);
  if (LLVM_LIKELY(isSmall() && O.isSmall())) {
    getSmall() /= O.getSmall();
    return *this;
  }
  return *this = DynamicAPInt(detail::SlowDynamicAPInt(*this) /
                              detail::SlowDynamicAPInt(O));
}

/// Stores the remainder of this value divided by \p O.
///
/// \param O Divisor.
/// @return Reference to this value.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &
DynamicAPInt::operator%=(const DynamicAPInt &O) {
  return *this = *this % O;
}
/// Pre-increments this value by one.
/// @return Reference to this value.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &DynamicAPInt::operator++() {
  return *this += 1;
}
/// Pre-decrements this value by one.
/// @return Reference to this value.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &DynamicAPInt::operator--() {
  return *this -= 1;
}

// ----------------------------------------------------------------------------
// Convenience operator overloads for int64_t.
// ----------------------------------------------------------------------------
/// Adds \p B to \p A in place.
///
/// \param A Value to update.
/// \param B Value to add.
/// @return Reference to the updated \p A.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator+=(DynamicAPInt &A,
                                                      int64_t B) {
  return A = A + B;
}
/// Subtracts \p B from \p A in place.
///
/// \param A Value to update.
/// \param B Value to subtract.
/// @return Reference to the updated \p A.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator-=(DynamicAPInt &A,
                                                      int64_t B) {
  return A = A - B;
}
/// Multiplies \p A by \p B in place.
///
/// \param A Value to update.
/// \param B Value to multiply by.
/// @return Reference to the updated \p A.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator*=(DynamicAPInt &A,
                                                      int64_t B) {
  return A = A * B;
}
/// Divides \p A by \p B in place.
///
/// \param A Value to update.
/// \param B Divisor.
/// @return Reference to the updated \p A.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator/=(DynamicAPInt &A,
                                                      int64_t B) {
  return A = A / B;
}
/// Stores the remainder of \p A divided by \p B in \p A.
///
/// \param A Value to update.
/// \param B Divisor.
/// @return Reference to the updated \p A.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt &operator%=(DynamicAPInt &A,
                                                      int64_t B) {
  return A = A % B;
}
/// Returns \p A plus \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return Sum of \p A and \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator+(const DynamicAPInt &A,
                                                    int64_t B) {
  return A + DynamicAPInt(B);
}
/// Returns \p A minus \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return Difference of \p A and \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator-(const DynamicAPInt &A,
                                                    int64_t B) {
  return A - DynamicAPInt(B);
}
/// Returns \p A multiplied by \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return Product of \p A and \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator*(const DynamicAPInt &A,
                                                    int64_t B) {
  return A * DynamicAPInt(B);
}
/// Returns \p A divided by \p B.
///
/// \param A Dividend.
/// \param B Divisor.
/// @return Quotient of \p A divided by \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator/(const DynamicAPInt &A,
                                                    int64_t B) {
  return A / DynamicAPInt(B);
}
/// Returns the remainder of \p A divided by \p B.
///
/// \param A Dividend.
/// \param B Divisor.
/// @return Remainder of \p A divided by \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator%(const DynamicAPInt &A,
                                                    int64_t B) {
  return A % DynamicAPInt(B);
}
/// Returns \p A plus \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return Sum of \p A and \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator+(int64_t A,
                                                    const DynamicAPInt &B) {
  return DynamicAPInt(A) + B;
}
/// Returns \p A minus \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return Difference of \p A and \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator-(int64_t A,
                                                    const DynamicAPInt &B) {
  return DynamicAPInt(A) - B;
}
/// Returns \p A multiplied by \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return Product of \p A and \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator*(int64_t A,
                                                    const DynamicAPInt &B) {
  return DynamicAPInt(A) * B;
}
/// Returns \p A divided by \p B.
///
/// \param A Dividend.
/// \param B Divisor.
/// @return Quotient of \p A divided by \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator/(int64_t A,
                                                    const DynamicAPInt &B) {
  return DynamicAPInt(A) / B;
}
/// Returns the remainder of \p A divided by \p B.
///
/// \param A Dividend.
/// \param B Divisor.
/// @return Remainder of \p A divided by \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE DynamicAPInt operator%(int64_t A,
                                                    const DynamicAPInt &B) {
  return DynamicAPInt(A) % B;
}

// We provide special implementations of the comparison operators rather than
// calling through as above, as this would result in a 1.2x slowdown.
/// Returns true if \p A is equal to \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if \p A equals \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator==(const DynamicAPInt &A, int64_t B) {
  if (LLVM_LIKELY(A.isSmall()))
    return A.getSmall() == B;
  return A.getLarge() == B;
}
/// Returns true if \p A is not equal to \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if \p A is not equal to \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator!=(const DynamicAPInt &A, int64_t B) {
  if (LLVM_LIKELY(A.isSmall()))
    return A.getSmall() != B;
  return A.getLarge() != B;
}
/// Returns true if \p A is greater than \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if \p A is greater than \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator>(const DynamicAPInt &A, int64_t B) {
  if (LLVM_LIKELY(A.isSmall()))
    return A.getSmall() > B;
  return A.getLarge() > B;
}
/// Returns true if \p A is less than \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if \p A is less than \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator<(const DynamicAPInt &A, int64_t B) {
  if (LLVM_LIKELY(A.isSmall()))
    return A.getSmall() < B;
  return A.getLarge() < B;
}
/// Returns true if \p A is less than or equal to \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if \p A is less than or equal to \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator<=(const DynamicAPInt &A, int64_t B) {
  if (LLVM_LIKELY(A.isSmall()))
    return A.getSmall() <= B;
  return A.getLarge() <= B;
}
/// Returns true if \p A is greater than or equal to \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if \p A is greater than or equal to \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator>=(const DynamicAPInt &A, int64_t B) {
  if (LLVM_LIKELY(A.isSmall()))
    return A.getSmall() >= B;
  return A.getLarge() >= B;
}
/// Returns true if \p A is equal to \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if \p A equals \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator==(int64_t A, const DynamicAPInt &B) {
  if (LLVM_LIKELY(B.isSmall()))
    return A == B.getSmall();
  return A == B.getLarge();
}
/// Returns true if \p A is not equal to \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if \p A is not equal to \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator!=(int64_t A, const DynamicAPInt &B) {
  if (LLVM_LIKELY(B.isSmall()))
    return A != B.getSmall();
  return A != B.getLarge();
}
/// Returns true if \p A is greater than \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if \p A is greater than \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator>(int64_t A, const DynamicAPInt &B) {
  if (LLVM_LIKELY(B.isSmall()))
    return A > B.getSmall();
  return A > B.getLarge();
}
/// Returns true if \p A is less than \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if \p A is less than \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator<(int64_t A, const DynamicAPInt &B) {
  if (LLVM_LIKELY(B.isSmall()))
    return A < B.getSmall();
  return A < B.getLarge();
}
/// Returns true if \p A is less than or equal to \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if \p A is less than or equal to \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator<=(int64_t A, const DynamicAPInt &B) {
  if (LLVM_LIKELY(B.isSmall()))
    return A <= B.getSmall();
  return A <= B.getLarge();
}
/// Returns true if \p A is greater than or equal to \p B.
///
/// \param A Left-hand operand.
/// \param B Right-hand operand.
/// @return True if \p A is greater than or equal to \p B.
LLVM_ATTRIBUTE_ALWAYS_INLINE bool operator>=(int64_t A, const DynamicAPInt &B) {
  if (LLVM_LIKELY(B.isSmall()))
    return A >= B.getSmall();
  return A >= B.getLarge();
}
} // namespace llvm

#endif // LLVM_ADT_DYNAMICAPINT_H
