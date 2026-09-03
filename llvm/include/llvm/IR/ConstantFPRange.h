//===- ConstantFPRange.h - Represent a range for floating-point -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Represent a range of possible values that may occur when the program is run
// for a floating-point value. This keeps track of a lower and upper bound for
// the constant.
//
// Range = [Lower, Upper] U (MayBeQNaN ? QNaN : {}) U (MayBeSNaN ? SNaN : {})
// Specifically, [inf, -inf] represents an empty set.
// Note:
// 1. Bounds are inclusive.
// 2. -0 is considered to be less than 0. That is, range [0, 0] doesn't contain
// -0.
// 3. Currently wrapping ranges are not supported.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CONSTANTFPRANGE_H
#define LLVM_IR_CONSTANTFPRANGE_H

#include "llvm/ADT/APFloat.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {

class raw_ostream;
struct KnownFPClass;

/// This class represents a range of floating-point values.
class [[nodiscard]] ConstantFPRange {
  APFloat Lower, Upper;
  bool MayBeQNaN : 1;
  bool MayBeSNaN : 1;

  /// Create empty constant range with same semantics.
  ConstantFPRange getEmpty() const {
    return ConstantFPRange(getSemantics(), /*IsFullSet=*/false);
  }

  /// Create full constant range with same semantics.
  ConstantFPRange getFull() const {
    return ConstantFPRange(getSemantics(), /*IsFullSet=*/true);
  }

  void makeEmpty();
  void makeFull();

  /// Initialize a full or empty set for the specified semantics.
  LLVM_ABI explicit ConstantFPRange(const fltSemantics &Sem, bool IsFullSet);

public:
  /// Initialize a range to hold the single specified value.
  /// \param Value The floating-point value that this range represents.
  LLVM_ABI explicit ConstantFPRange(const APFloat &Value);

  /// Initialize a range of values explicitly.
  /// Note: If \p LowerVal is greater than \p UpperVal, please use the canonical
  /// form [Inf, -Inf].
  /// \param LowerVal The inclusive lower bound of the range.
  /// \param UpperVal The inclusive upper bound of the range.
  /// \param MayBeQNaN Whether quiet NaN may be in the range.
  /// \param MayBeSNaN Whether signaling NaN may be in the range.
  LLVM_ABI ConstantFPRange(APFloat LowerVal, APFloat UpperVal, bool MayBeQNaN,
                           bool MayBeSNaN);

  /// Create empty constant range with the given semantics.
  /// \param Sem The floating-point semantics for the empty range.
  /// \return An empty \c ConstantFPRange for \p Sem.
  static ConstantFPRange getEmpty(const fltSemantics &Sem) {
    return ConstantFPRange(Sem, /*IsFullSet=*/false);
  }

  /// Create full constant range with the given semantics.
  /// \param Sem The floating-point semantics for the full range.
  /// \return A full \c ConstantFPRange for \p Sem.
  static ConstantFPRange getFull(const fltSemantics &Sem) {
    return ConstantFPRange(Sem, /*IsFullSet=*/true);
  }

  /// Helper for (-inf, inf) to represent all finite values.
  /// \param Sem The floating-point semantics for the finite range.
  /// \return A range covering all finite values for \p Sem.
  LLVM_ABI static ConstantFPRange getFinite(const fltSemantics &Sem);

  /// Helper for [-inf, inf] to represent all non-NaN values.
  /// \param Sem The floating-point semantics for the non-NaN range.
  /// \return A range covering all non-NaN values for \p Sem.
  LLVM_ABI static ConstantFPRange getNonNaN(const fltSemantics &Sem);

  /// Create a range which doesn't contain NaNs.
  /// \param LowerVal The inclusive lower bound of the range.
  /// \param UpperVal The inclusive upper bound of the range.
  /// \return A non-NaN \c ConstantFPRange from \p LowerVal to \p UpperVal.
  static ConstantFPRange getNonNaN(APFloat LowerVal, APFloat UpperVal) {
    return ConstantFPRange(std::move(LowerVal), std::move(UpperVal),
                           /*MayBeQNaN=*/false, /*MayBeSNaN=*/false);
  }

  /// Create a range which may contain NaNs.
  /// \param LowerVal The inclusive lower bound of the range.
  /// \param UpperVal The inclusive upper bound of the range.
  /// \return A \c ConstantFPRange from \p LowerVal to \p UpperVal that may
  /// contain NaNs.
  static ConstantFPRange getMayBeNaN(APFloat LowerVal, APFloat UpperVal) {
    return ConstantFPRange(std::move(LowerVal), std::move(UpperVal),
                           /*MayBeQNaN=*/true, /*MayBeSNaN=*/true);
  }

  /// Create a range which only contains NaNs.
  /// \param Sem The floating-point semantics for the NaN-only range.
  /// \param MayBeQNaN Whether quiet NaN may be in the range.
  /// \param MayBeSNaN Whether signaling NaN may be in the range.
  /// \return A NaN-only \c ConstantFPRange for \p Sem.
  LLVM_ABI static ConstantFPRange getNaNOnly(const fltSemantics &Sem,
                                             bool MayBeQNaN, bool MayBeSNaN);

  /// Produce the smallest range of values that may satisfy a floating-point
  /// comparison with Other.
  ///
  /// Produce the smallest range such that all values that may satisfy the given
  /// predicate with any value contained within Other is contained in the
  /// returned range.  Formally, this returns a superset of
  /// 'union over all y in Other . { x : fcmp op x y is true }'.  If the exact
  /// answer is not representable as a ConstantFPRange, the return value will be
  /// a proper superset of the above.
  ///
  /// Example: Pred = ole and Other = float [2, 5] returns Result = [-inf, 5]
  /// \param Pred The floating-point comparison predicate.
  /// \param Other The range of values compared against.
  /// \return The smallest range that may satisfy \p Pred with \p Other.
  LLVM_ABI static ConstantFPRange
  makeAllowedFCmpRegion(FCmpInst::Predicate Pred, const ConstantFPRange &Other);

  /// Produce the largest range of values that always satisfy a floating-point
  /// comparison with Other.
  ///
  /// Produce the largest range such that all values in the returned range
  /// satisfy the given predicate with all values contained within Other.
  /// Formally, this returns a subset of
  /// 'intersection over all y in Other . { x : fcmp op x y is true }'.  If the
  /// exact answer is not representable as a ConstantFPRange, the return value
  /// will be a proper subset of the above.
  ///
  /// Example: Pred = ole and Other = float [2, 5] returns [-inf, 2]
  /// \param Pred The floating-point comparison predicate.
  /// \param Other The range of values compared against.
  /// \return The largest range that always satisfies \p Pred with \p Other.
  LLVM_ABI static ConstantFPRange
  makeSatisfyingFCmpRegion(FCmpInst::Predicate Pred,
                           const ConstantFPRange &Other);

  /// Produce the exact range of values that satisfy a floating-point comparison
  /// with Other.
  ///
  /// Produce the exact range such that all values in the returned range satisfy
  /// the given predicate with any value contained within Other. Formally, this
  /// returns { x : fcmp op x Other is true }.
  ///
  /// Example: Pred = olt and Other = float 3 returns [-inf, 3)
  /// If the exact answer is not representable as a ConstantFPRange, returns
  /// std::nullopt.
  /// \param Pred The floating-point comparison predicate.
  /// \param Other The floating-point value compared against.
  /// \return The exact satisfying range, or \c std::nullopt if not
  /// representable.
  LLVM_ABI static std::optional<ConstantFPRange>
  makeExactFCmpRegion(FCmpInst::Predicate Pred, const APFloat &Other);

  /// Does the predicate \p Pred hold between ranges this and \p Other?
  /// NOTE: false does not mean that inverse predicate holds!
  /// \param Pred The floating-point comparison predicate.
  /// \param Other The other range to compare against.
  /// \return True if \p Pred holds between this range and \p Other.
  LLVM_ABI bool fcmp(FCmpInst::Predicate Pred,
                     const ConstantFPRange &Other) const;

  /// Return the lower value for this range.
  /// \return The inclusive lower bound of this range.
  const APFloat &getLower() const { return Lower; }

  /// Return the upper value for this range.
  /// \return The inclusive upper bound of this range.
  const APFloat &getUpper() const { return Upper; }

  /// Return true if this range may contain a NaN.
  /// \return True if this range may contain a quiet or signaling NaN.
  bool containsNaN() const { return MayBeQNaN || MayBeSNaN; }
  /// Return true if this range may contain a quiet NaN.
  /// \return True if this range may contain a quiet NaN.
  bool containsQNaN() const { return MayBeQNaN; }
  /// Return true if this range may contain a signaling NaN.
  /// \return True if this range may contain a signaling NaN.
  bool containsSNaN() const { return MayBeSNaN; }
  /// Return true if this range contains only NaN values.
  /// \return True if this range contains only NaN values.
  LLVM_ABI bool isNaNOnly() const;

  /// Get the semantics of this ConstantFPRange.
  /// \return The floating-point semantics of this range.
  const fltSemantics &getSemantics() const { return Lower.getSemantics(); }

  /// Return true if this set contains all of the elements possible
  /// for this data-type.
  /// \return True if this range is a full set.
  LLVM_ABI bool isFullSet() const;

  /// Return true if this set contains no members.
  /// \return True if this range is empty.
  LLVM_ABI bool isEmptySet() const;

  /// Return true if the specified value is in the set.
  /// \param Val The floating-point value to test for membership.
  /// \return True if \p Val is contained in this range.
  LLVM_ABI bool contains(const APFloat &Val) const;

  /// Return true if the other range is a subset of this one.
  /// \param CR The other range to test for containment.
  /// \return True if \p CR is a subset of this range.
  LLVM_ABI bool contains(const ConstantFPRange &CR) const;

  /// If this set contains a single element, return it, otherwise return null.
  /// If \p ExcludesNaN is true, return the non-NaN single element.
  /// \param ExcludesNaN If true, ignore NaN when looking for a single element.
  /// \return A pointer to the single element, or null if there is not exactly
  /// one.
  LLVM_ABI const APFloat *getSingleElement(bool ExcludesNaN = false) const;

  /// Return true if this set contains exactly one member.
  /// If \p ExcludesNaN is true, return true if this set contains exactly one
  /// non-NaN member.
  /// \param ExcludesNaN If true, ignore NaN when checking for a single element.
  /// \return True if this range contains exactly one element.
  bool isSingleElement(bool ExcludesNaN = false) const {
    return getSingleElement(ExcludesNaN) != nullptr;
  }

  /// Return true if the sign bit of all values in this range is 1.
  /// Return false if the sign bit of all values in this range is 0.
  /// Otherwise, return std::nullopt.
  /// \return \c true if all sign bits are 1, \c false if all are 0, or
  /// \c std::nullopt if the sign bit is not constant.
  LLVM_ABI std::optional<bool> getSignBit() const;

  /// Return true if this range is equal to another range.
  /// \param CR The other range to compare against.
  /// \return True if this range equals \p CR.
  LLVM_ABI bool operator==(const ConstantFPRange &CR) const;
  /// Return true if this range is not equal to another range.
  /// \param CR The other range to compare against.
  /// \return True if this range is not equal to \p CR.
  bool operator!=(const ConstantFPRange &CR) const { return !operator==(CR); }

  /// Return the FPClassTest which will return true for the value.
  /// \return An \c FPClassTest that is true for every value in this range.
  LLVM_ABI FPClassTest classify() const;

  /// Print out the bounds to a stream.
  /// \param OS The stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Allow printing from a debugger easily.
  LLVM_ABI void dump() const;

  /// Return the range that results from the intersection of this range with
  /// another range.
  /// \param CR The other range to intersect with.
  /// \return The intersection of this range and \p CR.
  LLVM_ABI ConstantFPRange intersectWith(const ConstantFPRange &CR) const;

  /// Return the smallest range that results from the union of this range with
  /// another.
  ///
  /// The resultant range is guaranteed to include the elements of both sets,
  /// but may contain more.
  /// \param CR The other range to union with.
  /// \return The smallest range covering the union of this range and \p CR.
  LLVM_ABI ConstantFPRange unionWith(const ConstantFPRange &CR) const;

  /// Calculate absolute value range.
  /// \return The absolute-value range of this range.
  LLVM_ABI ConstantFPRange abs() const;

  /// Calculate range of negated values.
  /// \return The range of negated values of this range.
  LLVM_ABI ConstantFPRange negate() const;

  /// Get the range without NaNs. It is useful when we apply nnan flag to range
  /// of operands/results.
  /// \return A copy of this range with NaNs removed.
  ConstantFPRange getWithoutNaN() const {
    return ConstantFPRange(Lower, Upper, false, false);
  }

  /// Get the range without infinities. It is useful when we apply ninf flag to
  /// range of operands/results.
  /// \return A copy of this range with infinities removed.
  LLVM_ABI ConstantFPRange getWithoutInf() const;

  /// Return a new range in the specified format with the specified rounding
  /// mode.
  /// \param DstSem The destination floating-point semantics.
  /// \param RM The rounding mode used for the cast.
  /// \return This range cast to \p DstSem using \p RM.
  LLVM_ABI ConstantFPRange
  cast(const fltSemantics &DstSem,
       APFloat::roundingMode RM = APFloat::rmNearestTiesToEven) const;

  /// Return a new range representing the possible values resulting
  /// from an addition of a value in this range and a value in \p Other.
  /// \param Other The other range to add.
  /// \return The range of possible results of adding this range and \p Other.
  LLVM_ABI ConstantFPRange add(const ConstantFPRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a subtraction of a value in this range and a value in \p Other.
  /// \param Other The other range to subtract.
  /// \return The range of possible results of subtracting \p Other from this
  /// range.
  LLVM_ABI ConstantFPRange sub(const ConstantFPRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a multiplication of a value in this range and a value in \p Other.
  /// \param Other The other range to multiply by.
  /// \return The range of possible results of multiplying this range and
  /// \p Other.
  LLVM_ABI ConstantFPRange mul(const ConstantFPRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a division of a value in this range and a value in
  /// \p Other.
  /// \param Other The other range to divide by.
  /// \return The range of possible results of dividing this range by \p Other.
  LLVM_ABI ConstantFPRange div(const ConstantFPRange &Other) const;

  /// Flush denormal values to zero according to the specified mode.
  /// For dynamic mode, we return the union of all possible results.
  /// \param Mode The denormal flushing mode to apply.
  LLVM_ABI void flushDenormals(DenormalMode::DenormalModeKind Mode);
};

/// Print \p CR to \p OS.
/// \param OS The stream to print to.
/// \param CR The constant floating-point range to print.
/// \return The output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const ConstantFPRange &CR) {
  CR.print(OS);
  return OS;
}

} // end namespace llvm

#endif // LLVM_IR_CONSTANTFPRANGE_H
