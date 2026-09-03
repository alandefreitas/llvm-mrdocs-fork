//===- ConstantRange.h - Represent a range ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Represent a range of possible values that may occur when the program is run
// for an integral value.  This keeps track of a lower and upper bound for the
// constant, which MAY wrap around the end of the numeric range.  To do this, it
// keeps track of a [lower, upper) bound, which specifies an interval just like
// STL iterators.  When used with boolean values, the following are important
// ranges: :
//
//  [F, F) = {}     = Empty set
//  [T, F) = {T}
//  [F, T) = {F}
//  [T, T) = {F, T} = Full set
//
// The other integral ranges use min/max values for special range values. For
// example, for 8-bit types, it uses:
// [0, 0)     = {}       = Empty set
// [255, 255) = {0..255} = Full Set
//
// Note that ConstantRange can be used to represent either signed or
// unsigned ranges.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CONSTANTRANGE_H
#define LLVM_IR_CONSTANTRANGE_H

#include "llvm/ADT/APInt.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

class MDNode;
class raw_ostream;
class CmpPredicate;
struct KnownBits;

/// This class represents a range of values.
class [[nodiscard]] ConstantRange {
  APInt Lower, Upper;

  /// Create empty constant range with same bitwidth.
  ConstantRange getEmpty() const {
    return ConstantRange(getBitWidth(), false);
  }

  /// Create full constant range with same bitwidth.
  ConstantRange getFull() const {
    return ConstantRange(getBitWidth(), true);
  }

public:
  /// Initialize a full or empty set for the specified bit width.
  /// \param BitWidth The bit width of the range.
  /// \param isFullSet Whether to create a full set (\c true) or empty set.
  LLVM_ABI explicit ConstantRange(uint32_t BitWidth, bool isFullSet);

  /// Initialize a range to hold the single specified value.
  /// \param Value The sole value contained in the range.
  LLVM_ABI ConstantRange(APInt Value);

  /// Initialize a range of values explicitly.
  ///
  /// This will assert out if Lower==Upper and Lower != Min or Max value for its
  /// type. It will also assert out if the two APInt's are not the same bit
  /// width.
  /// \param Lower The inclusive lower bound of the range.
  /// \param Upper The exclusive upper bound of the range.
  LLVM_ABI ConstantRange(APInt Lower, APInt Upper);

  /// Create empty constant range with the given bit width.
  /// \param BitWidth The bit width of the empty range.
  /// \return An empty ConstantRange of the given bit width.
  static ConstantRange getEmpty(uint32_t BitWidth) {
    return ConstantRange(BitWidth, false);
  }

  /// Create full constant range with the given bit width.
  /// \param BitWidth The bit width of the full range.
  /// \return A full ConstantRange of the given bit width.
  static ConstantRange getFull(uint32_t BitWidth) {
    return ConstantRange(BitWidth, true);
  }

  /// Create non-empty constant range with the given bounds. If Lower and
  /// Upper are the same, a full range is returned.
  /// \param Lower The inclusive lower bound of the range.
  /// \param Upper The exclusive upper bound of the range.
  /// \return A non-empty ConstantRange with the given bounds, or a full range
  /// if equal.
  static ConstantRange getNonEmpty(APInt Lower, APInt Upper) {
    if (Lower == Upper)
      return getFull(Lower.getBitWidth());
    return ConstantRange(std::move(Lower), std::move(Upper));
  }

  /// Initialize a range based on a known bits constraint. The IsSigned flag
  /// indicates whether the constant range should not wrap in the signed or
  /// unsigned domain.
  /// \param Known The known bits constraint to encode as a range.
  /// \param IsSigned Whether the range should be non-wrapping in the signed
  ///        domain (\c true) or the unsigned domain (\c false).
  /// \return A ConstantRange encoding the known-bits constraint.
  LLVM_ABI static ConstantRange fromKnownBits(const KnownBits &Known,
                                              bool IsSigned);

  /// Split the ConstantRange into positive and negative components, ignoring
  /// zero values.
  /// \return A pair of the positive and negative subranges.
  LLVM_ABI std::pair<ConstantRange, ConstantRange> splitPosNeg() const;

  /// Produce the smallest range containing all values that may satisfy Pred.
  ///
  /// Produce the smallest range such that all values that may satisfy the given
  /// predicate with any value contained within Other is contained in the
  /// returned range. Formally, this returns a superset of
  /// 'union over all y in Other . { x : icmp op x y is true }'.  If the exact
  /// answer is not representable as a ConstantRange, the return value will be a
  /// proper superset of the above.
  ///
  /// Example: Pred = ult and Other = i8 [2, 5) returns Result = [0, 4)
  /// \param Pred The icmp predicate.
  /// \param Other The constant range of right-hand-side values.
  /// \return A range containing all values that may satisfy \p Pred with
  /// \p Other.
  LLVM_ABI static ConstantRange
  makeAllowedICmpRegion(CmpInst::Predicate Pred, const ConstantRange &Other);

  /// Produce the smallest range containing all values that may satisfy Pred.
  ///
  /// Produce the smallest range such that all values that may satisfy the given
  /// predicate with any value contained within Other is contained in the
  /// returned range. This overload takes a CmpPredicate, which may carry
  /// samesign information for tighter ranges on unsigned predicates.
  /// \param Pred The comparison predicate, possibly with samesign info.
  /// \param Other The constant range of right-hand-side values.
  /// \return A range containing all values that may satisfy \p Pred with
  /// \p Other.
  LLVM_ABI static ConstantRange
  makeAllowedICmpRegion(CmpPredicate Pred, const ConstantRange &Other);

  /// Produce the largest range of values that all satisfy Pred with Other.
  ///
  /// Produce the largest range such that all values in the returned range
  /// satisfy the given predicate with all values contained within Other.
  /// Formally, this returns a subset of
  /// 'intersection over all y in Other . { x : icmp op x y is true }'.  If the
  /// exact answer is not representable as a ConstantRange, the return value
  /// will be a proper subset of the above.
  ///
  /// Example: Pred = ult and Other = i8 [2, 5) returns [0, 2)
  /// \param Pred The icmp predicate.
  /// \param Other The constant range of right-hand-side values.
  /// \return The largest range of values that all satisfy \p Pred with
  /// \p Other.
  LLVM_ABI static ConstantRange
  makeSatisfyingICmpRegion(CmpInst::Predicate Pred, const ConstantRange &Other);

  /// Produce the exact range of values that satisfy Pred with Other.
  ///
  /// Produce the exact range such that all values in the returned range satisfy
  /// the given predicate with any value contained within Other. Formally, this
  /// returns the exact answer when the superset of 'union over all y in Other
  /// is exactly same as the subset of intersection over all y in Other.
  /// { x : icmp op x y is true}'.
  ///
  /// Example: Pred = ult and Other = i8 3 returns [0, 3)
  /// \param Pred The icmp predicate.
  /// \param Other The constant right-hand-side value.
  /// \return The exact range of values that satisfy \p Pred with \p Other.
  LLVM_ABI static ConstantRange makeExactICmpRegion(CmpInst::Predicate Pred,
                                                    const APInt &Other);

  /// Does the predicate \p Pred hold between ranges this and \p Other?
  /// NOTE: false does not mean that inverse predicate holds!
  /// \param Pred The icmp predicate to evaluate.
  /// \param Other The other constant range in the comparison.
  /// \return True if \p Pred holds between this range and \p Other.
  LLVM_ABI bool icmp(CmpInst::Predicate Pred, const ConstantRange &Other) const;

  /// Return true iff CR1 ult CR2 is equivalent to CR1 slt CR2.
  /// Does not depend on strictness/direction of the predicate.
  /// \param CR1 The first constant range.
  /// \param CR2 The second constant range.
  /// \return True if unsigned and signed predicates are equivalent for these
  /// ranges.
  LLVM_ABI static bool
  areInsensitiveToSignednessOfICmpPredicate(const ConstantRange &CR1,
                                            const ConstantRange &CR2);

  /// Return true iff CR1 ult CR2 is equivalent to CR1 sge CR2.
  /// Does not depend on strictness/direction of the predicate.
  /// \param CR1 The first constant range.
  /// \param CR2 The second constant range.
  /// \return True if unsigned and inverted signed predicates are equivalent
  /// for these ranges.
  LLVM_ABI static bool
  areInsensitiveToSignednessOfInvertedICmpPredicate(const ConstantRange &CR1,
                                                    const ConstantRange &CR2);

  /// Return Pred with flipped signedness when the comparison is insensitive.
  ///
  /// If the comparison between constant ranges this and Other is insensitive to
  /// the signedness of the comparison predicate, return a predicate equivalent
  /// to \p Pred, with flipped signedness (i.e. unsigned instead of signed or
  /// vice versa), and maybe inverted, otherwise returns
  /// CmpInst::Predicate::BAD_ICMP_PREDICATE.
  /// \param Pred The original icmp predicate.
  /// \param CR1 The first constant range.
  /// \param CR2 The second constant range.
  /// \return An equivalent flipped-signedness predicate, or BAD_ICMP_PREDICATE.
  LLVM_ABI static CmpInst::Predicate
  getEquivalentPredWithFlippedSignedness(CmpInst::Predicate Pred,
                                         const ConstantRange &CR1,
                                         const ConstantRange &CR2);

  /// Produce the largest range of X that never wraps with any Y in Other.
  ///
  /// Produce the largest range containing all X such that "X BinOp Y" is
  /// guaranteed not to wrap (overflow) for *all* Y in Other. However, there may
  /// be *some* Y in Other for which additional X not contained in the result
  /// also do not overflow.
  ///
  /// NoWrapKind must be one of OBO::NoUnsignedWrap or OBO::NoSignedWrap.
  ///
  /// Examples:
  ///  typedef OverflowingBinaryOperator OBO;
  ///  #define MGNR makeGuaranteedNoWrapRegion
  ///  MGNR(Add, [i8 1, 2), OBO::NoSignedWrap) == [-128, 127)
  ///  MGNR(Add, [i8 1, 2), OBO::NoUnsignedWrap) == [0, -1)
  ///  MGNR(Add, [i8 0, 1), OBO::NoUnsignedWrap) == Full Set
  ///  MGNR(Add, [i8 -1, 6), OBO::NoSignedWrap) == [INT_MIN+1, INT_MAX-4)
  ///  MGNR(Sub, [i8 1, 2), OBO::NoSignedWrap) == [-127, 128)
  ///  MGNR(Sub, [i8 1, 2), OBO::NoUnsignedWrap) == [1, 0)
  /// \param BinOp The binary operator.
  /// \param Other The constant range of right-hand-side values.
  /// \param NoWrapKind Either \c OBO::NoUnsignedWrap or \c OBO::NoSignedWrap.
  /// \return The largest range of X that never wraps with any Y in \p Other.
  LLVM_ABI static ConstantRange
  makeGuaranteedNoWrapRegion(Instruction::BinaryOps BinOp,
                             const ConstantRange &Other, unsigned NoWrapKind);

  /// Produce the range that contains X if and only if "X BinOp Other" does
  /// not wrap.
  /// \param BinOp The binary operator.
  /// \param Other The constant right-hand-side value.
  /// \param NoWrapKind Either \c OBO::NoUnsignedWrap or \c OBO::NoSignedWrap.
  /// \return The exact range of X for which X BinOp Other does not wrap.
  LLVM_ABI static ConstantRange
  makeExactNoWrapRegion(Instruction::BinaryOps BinOp, const APInt &Other,
                        unsigned NoWrapKind);

  /// Initialize a range of values X satisfying `(X & Mask) != C`.
  ///
  /// Initialize a range containing all values X that satisfy `(X & Mask)
  /// != C`. Note that the range returned may contain values where `(X & Mask)
  /// == C` holds, making it less precise, but still conservative.
  /// \param Mask The bit mask applied to candidate values.
  /// \param C The constant that masked values must not equal.
  /// \return A conservative range of values X with (X & Mask) != C.
  LLVM_ABI static ConstantRange makeMaskNotEqualRange(const APInt &Mask,
                                                      const APInt &C);

  /// Returns true if ConstantRange calculations are supported for intrinsic
  /// with \p IntrinsicID.
  /// \param IntrinsicID The intrinsic to query.
  /// \return True if range analysis supports \p IntrinsicID.
  LLVM_ABI static bool isIntrinsicSupported(Intrinsic::ID IntrinsicID);

  /// Compute range of intrinsic result for the given operand ranges.
  /// \param IntrinsicID The intrinsic to evaluate.
  /// \param Ops The operand ranges of the intrinsic.
  /// \return The range of possible results of the intrinsic.
  LLVM_ABI static ConstantRange intrinsic(Intrinsic::ID IntrinsicID,
                                          ArrayRef<ConstantRange> Ops);

  /// Set up \p Pred and \p RHS such that
  /// ConstantRange::makeExactICmpRegion(Pred, RHS) == *this.  Return true if
  /// successful.
  /// \param Pred Set to the equivalent icmp predicate on success.
  /// \param RHS Set to the equivalent icmp right-hand side on success.
  /// \return True if an equivalent icmp was found.
  LLVM_ABI bool getEquivalentICmp(CmpInst::Predicate &Pred, APInt &RHS) const;

  /// Set up \p Pred, \p RHS and \p Offset such that (V + Offset) Pred RHS
  /// is true iff V is in the range. Prefers using Offset == 0 if possible.
  /// \param Pred Set to the equivalent icmp predicate.
  /// \param RHS Set to the equivalent icmp right-hand side.
  /// \param Offset Set to the additive offset applied to the compared value.
  LLVM_ABI void getEquivalentICmp(CmpInst::Predicate &Pred, APInt &RHS,
                                  APInt &Offset) const;

  /// Return the lower value for this range.
  /// \return The inclusive lower bound of this range.
  const APInt &getLower() const { return Lower; }

  /// Return the upper value for this range.
  /// \return The exclusive upper bound of this range.
  const APInt &getUpper() const { return Upper; }

  /// Get the bit width of this ConstantRange.
  /// \return The bit width of values in this range.
  uint32_t getBitWidth() const { return Lower.getBitWidth(); }

  /// Return true if this set contains all of the elements possible
  /// for this data-type.
  /// \return True if this range is a full set.
  LLVM_ABI bool isFullSet() const;

  /// Return true if this set contains no members.
  /// \return True if this range is empty.
  LLVM_ABI bool isEmptySet() const;

  /// Return true if this set wraps around the unsigned domain. Special cases:
  ///  * Empty set: Not wrapped.
  ///  * Full set: Not wrapped.
  ///  * [X, 0) == [X, Max]: Not wrapped.
  /// \return True if this set wraps around the unsigned domain.
  LLVM_ABI bool isWrappedSet() const;

  /// Return true if the exclusive upper bound wraps around the unsigned
  /// domain. Special cases:
  ///  * Empty set: Not wrapped.
  ///  * Full set: Not wrapped.
  ///  * [X, 0): Wrapped.
  /// \return True if the exclusive upper bound wraps the unsigned domain.
  LLVM_ABI bool isUpperWrapped() const;

  /// Return true if this set wraps around the signed domain.
  ///
  /// Special cases:
  ///  * Empty set: Not wrapped.
  ///  * Full set: Not wrapped.
  ///  * [X, SignedMin) == [X, SignedMax]: Not wrapped.
  /// \return True if this set wraps around the signed domain.
  LLVM_ABI bool isSignWrappedSet() const;

  /// Return true if the exclusive upper bound wraps the signed domain.
  ///
  /// Special cases:
  ///  * Empty set: Not wrapped.
  ///  * Full set: Not wrapped.
  ///  * [X, SignedMin): Wrapped.
  /// \return True if the exclusive upper bound wraps the signed domain.
  LLVM_ABI bool isUpperSignWrapped() const;

  /// Return true if the specified value is in the set.
  /// \param Val The value to test for membership.
  /// \return True if \p Val is contained in this range.
  LLVM_ABI bool contains(const APInt &Val) const;

  /// Return true if the other range is a subset of this one.
  /// \param CR The candidate subset range.
  /// \return True if \p CR is a subset of this range.
  LLVM_ABI bool contains(const ConstantRange &CR) const;

  /// If this set contains a single element, return it, otherwise return null.
  /// \return Pointer to the sole element, or nullptr.
  const APInt *getSingleElement() const {
    if (Upper == Lower + 1)
      return &Lower;
    return nullptr;
  }

  /// If this set contains all but a single element, return it, otherwise return
  /// null.
  /// \return Pointer to the sole missing element, or nullptr.
  const APInt *getSingleMissingElement() const {
    if (Lower == Upper + 1)
      return &Upper;
    return nullptr;
  }

  /// Return true if this set contains exactly one member.
  /// \return True if this range contains exactly one value.
  bool isSingleElement() const { return getSingleElement() != nullptr; }

  /// Compare set size of this range with the range CR.
  /// \param CR The range whose size is compared against this one.
  /// \return True if this range has strictly fewer elements than \p CR.
  LLVM_ABI bool isSizeStrictlySmallerThan(const ConstantRange &CR) const;

  /// Compare set size of this range with Value.
  /// \param MaxSize The size threshold to compare against.
  /// \return True if this range contains more than \p MaxSize elements.
  LLVM_ABI bool isSizeLargerThan(uint64_t MaxSize) const;

  /// Return true if all values in this range are negative.
  /// \return True if every value in this range is negative.
  LLVM_ABI bool isAllNegative() const;

  /// Return true if all values in this range are non-negative.
  /// \return True if every value in this range is non-negative.
  LLVM_ABI bool isAllNonNegative() const;

  /// Return true if all values in this range are positive.
  /// \return True if every value in this range is positive.
  LLVM_ABI bool isAllPositive() const;

  /// Return the largest unsigned value contained in the ConstantRange.
  /// \return The largest unsigned value in this range.
  LLVM_ABI APInt getUnsignedMax() const;

  /// Return the smallest unsigned value contained in the ConstantRange.
  /// \return The smallest unsigned value in this range.
  LLVM_ABI APInt getUnsignedMin() const;

  /// Return the largest signed value contained in the ConstantRange.
  /// \return The largest signed value in this range.
  LLVM_ABI APInt getSignedMax() const;

  /// Return the smallest signed value contained in the ConstantRange.
  /// \return The smallest signed value in this range.
  LLVM_ABI APInt getSignedMin() const;

  /// Return true if this range is equal to another range.
  /// \param CR The range to compare against.
  /// \return True if this range equals \p CR.
  bool operator==(const ConstantRange &CR) const {
    return Lower == CR.Lower && Upper == CR.Upper;
  }
  /// Return true if this range is not equal to another range.
  /// \param CR The range to compare against.
  /// \return True if this range differs from \p CR.
  bool operator!=(const ConstantRange &CR) const {
    return !operator==(CR);
  }

  /// Compute the maximal number of active bits needed to represent every value
  /// in this range.
  /// \return The maximum active-bit count over values in this range.
  LLVM_ABI unsigned getActiveBits() const;

  /// Compute the maximal number of bits needed to represent every value
  /// in this signed range.
  /// \return The maximum signed bit width needed for any value in this range.
  LLVM_ABI unsigned getMinSignedBits() const;

  /// Subtract the specified constant from the endpoints of this constant range.
  /// \param CI The constant subtracted from both endpoints.
  /// \return This range with \p CI subtracted from both endpoints.
  LLVM_ABI ConstantRange subtract(const APInt &CI) const;

  /// Subtract the specified range from this range (aka relative complement of
  /// the sets).
  /// \param CR The range to subtract from this one.
  /// \return The relative complement of \p CR in this range.
  LLVM_ABI ConstantRange difference(const ConstantRange &CR) const;

  /// Preferred representation when a range operation has multiple valid results.
  ///
  /// If represented precisely, the result of some range operations may consist
  /// of multiple disjoint ranges. As only a single range may be returned, any
  /// range covering these disjoint ranges constitutes a valid result, but some
  /// may be more useful than others depending on context. The preferred range
  /// type specifies whether a range that is non-wrapping in the unsigned or
  /// signed domain, or has the smallest size, is preferred. If a signedness is
  /// preferred but all ranges are non-wrapping or all wrapping, then the
  /// smallest set size is preferred. If there are multiple smallest sets, any
  /// one of them may be returned.
  enum PreferredRangeType {
    /// Prefer the result with the smallest set size.
    Smallest,
    /// Prefer a range that is non-wrapping in the unsigned domain.
    Unsigned,
    /// Prefer a range that is non-wrapping in the signed domain.
    Signed
  };

  /// Return the intersection of this range with another range.
  ///
  /// Return the range that results from the intersection of this range with
  /// another range. If the intersection is disjoint, such that two results
  /// are possible, the preferred range is determined by the PreferredRangeType.
  /// \param CR The range to intersect with.
  /// \param Type How to prefer among multiple valid intersection results.
  /// \return The intersection of this range with \p CR.
  LLVM_ABI ConstantRange intersectWith(
      const ConstantRange &CR, PreferredRangeType Type = Smallest) const;

  /// Return the union of this range with another range.
  ///
  /// Return the range that results from the union of this range with another
  /// range. The resultant range is guaranteed to include the elements of both
  /// sets, but may contain more. For example, [3, 9) union [12,15) is [3, 15),
  /// which includes 9, 10, and 11, which were not included in either set
  /// before.
  /// \param CR The range to union with.
  /// \param Type How to prefer among multiple valid union results.
  /// \return The union of this range with \p CR.
  LLVM_ABI ConstantRange unionWith(const ConstantRange &CR,
                                   PreferredRangeType Type = Smallest) const;

  /// Intersect the two ranges and return the result if it can be represented
  /// exactly, otherwise return std::nullopt.
  /// \param CR The range to intersect with.
  /// \return The exact intersection, or std::nullopt if it cannot be
  /// represented exactly.
  LLVM_ABI std::optional<ConstantRange>
  exactIntersectWith(const ConstantRange &CR) const;

  /// Union the two ranges and return the result if it can be represented
  /// exactly, otherwise return std::nullopt.
  /// \param CR The range to union with.
  /// \return The exact union, or std::nullopt if it cannot be represented
  /// exactly.
  LLVM_ABI std::optional<ConstantRange>
  exactUnionWith(const ConstantRange &CR) const;

  /// Return the range of values resulting from applying CastOp to this range.
  ///
  /// Return a new range representing the possible values resulting from an
  /// application of the specified cast operator to this range. \p BitWidth is
  /// the target bitwidth of the cast. For casts which don't change bitwidth, it
  /// must be the same as the source bitwidth. For casts which do change
  /// bitwidth, the bitwidth must be consistent with the requested cast and
  /// source bitwidth.
  /// \param CastOp The cast operator to apply.
  /// \param BitWidth The target bit width of the cast.
  /// \return The range of values after applying \p CastOp to this range.
  LLVM_ABI ConstantRange castOp(Instruction::CastOps CastOp,
                                uint32_t BitWidth) const;

  /// Return this range zero-extended to a strictly larger integer type.
  ///
  /// Return a new range in the specified integer type, which must be strictly
  /// larger than the current type. The returned range will correspond to the
  /// possible range of values if the source range had been zero extended to
  /// BitWidth.
  /// \param BitWidth The target bit width, strictly larger than the current.
  /// \return This range zero-extended to \p BitWidth.
  LLVM_ABI ConstantRange zeroExtend(uint32_t BitWidth) const;

  /// Return this range sign-extended to a strictly larger integer type.
  ///
  /// Return a new range in the specified integer type, which must be strictly
  /// larger than the current type. The returned range will correspond to the
  /// possible range of values if the source range had been sign extended to
  /// BitWidth.
  /// \param BitWidth The target bit width, strictly larger than the current.
  /// \return This range sign-extended to \p BitWidth.
  LLVM_ABI ConstantRange signExtend(uint32_t BitWidth) const;

  /// Return this range truncated to a strictly smaller integer type.
  ///
  /// Return a new range in the specified integer type, which must be strictly
  /// smaller than the current type. The returned range will correspond to the
  /// possible range of values if the source range had been truncated to the
  /// specified type with wrap type \p NoWrapKind. Note that the result of trunc
  /// nuw is exact.
  /// \param BitWidth The target bit width, strictly smaller than the current.
  /// \param NoWrapKind Wrap constraints for the truncation, or 0 for none.
  /// \return This range truncated to \p BitWidth.
  LLVM_ABI ConstantRange truncate(uint32_t BitWidth,
                                  unsigned NoWrapKind = 0) const;

  /// Make this range have the bit width given by \p BitWidth. The
  /// value is zero extended, truncated, or left alone to make it that width.
  /// \param BitWidth The desired bit width of the result.
  /// \return This range zero-extended or truncated to \p BitWidth.
  LLVM_ABI ConstantRange zextOrTrunc(uint32_t BitWidth) const;

  /// Make this range have the bit width given by \p BitWidth. The
  /// value is sign extended, truncated, or left alone to make it that width.
  /// \param BitWidth The desired bit width of the result.
  /// \return This range sign-extended or truncated to \p BitWidth.
  LLVM_ABI ConstantRange sextOrTrunc(uint32_t BitWidth) const;

  /// Return the range of values resulting from applying BinOp to this and Other.
  ///
  /// Return a new range representing the possible values resulting from an
  /// application of the specified binary operator to an left hand side of this
  /// range and a right hand side of \p Other.
  /// \param BinOp The binary operator to apply.
  /// \param Other The right-hand-side operand range.
  /// \return The range of results of the binary operation.
  LLVM_ABI ConstantRange binaryOp(Instruction::BinaryOps BinOp,
                                  const ConstantRange &Other) const;

  /// Return the range from applying an overflowing BinOp to this and Other.
  ///
  /// Return a new range representing the possible values resulting from an
  /// application of the specified overflowing binary operator to a left hand
  /// side of this range and a right hand side of \p Other given the provided
  /// knowledge about lack of wrapping \p NoWrapKind.
  /// \param BinOp The overflowing binary operator to apply.
  /// \param Other The right-hand-side operand range.
  /// \param NoWrapKind Known wrapping constraints for the operation.
  /// \return The range of results of the overflowing binary operation.
  LLVM_ABI ConstantRange overflowingBinaryOp(Instruction::BinaryOps BinOp,
                                             const ConstantRange &Other,
                                             unsigned NoWrapKind) const;

  /// Return a new range representing the possible values resulting
  /// from an addition of a value in this range and a value in \p Other.
  /// \param Other The right-hand-side operand range.
  /// \return The range of addition results.
  LLVM_ABI ConstantRange add(const ConstantRange &Other) const;

  /// Return the range of values from an addition with wrap type NoWrapKind.
  ///
  /// Return a new range representing the possible values resulting from an
  /// addition with wrap type \p NoWrapKind of a value in this range and a value
  /// in \p Other. If the result range is disjoint, the preferred range is
  /// determined by the \p PreferredRangeType.
  /// \param Other The right-hand-side operand range.
  /// \param NoWrapKind Known wrapping constraints for the addition.
  /// \param RangeType How to prefer among multiple valid result ranges.
  /// \return The range of addition results under the given wrap constraints.
  LLVM_ABI ConstantRange
  addWithNoWrap(const ConstantRange &Other, unsigned NoWrapKind,
                PreferredRangeType RangeType = Smallest) const;

  /// Return a new range representing the possible values resulting
  /// from a subtraction of a value in this range and a value in \p Other.
  /// \param Other The right-hand-side operand range.
  /// \return The range of subtraction results.
  LLVM_ABI ConstantRange sub(const ConstantRange &Other) const;

  /// Return the range of values from a subtraction with wrap type NoWrapKind.
  ///
  /// Return a new range representing the possible values resulting from an
  /// subtraction with wrap type \p NoWrapKind of a value in this range and a
  /// value in \p Other. If the result range is disjoint, the preferred range is
  /// determined by the \p PreferredRangeType.
  /// \param Other The right-hand-side operand range.
  /// \param NoWrapKind Known wrapping constraints for the subtraction.
  /// \param RangeType How to prefer among multiple valid result ranges.
  /// \return The range of subtraction results under the given wrap constraints.
  LLVM_ABI ConstantRange
  subWithNoWrap(const ConstantRange &Other, unsigned NoWrapKind,
                PreferredRangeType RangeType = Smallest) const;

  /// Return the range of values from multiplying this range by Other.
  ///
  /// Return a new range representing the possible values resulting from a
  /// multiplication of a value in this range and a value in \p Other. If
  /// \p NoWrapKind is set, assume that corresponding wrapping can not occur.
  /// \param Other The right-hand-side operand range.
  /// \param NoWrapKind Known wrapping constraints for the multiplication.
  /// \return The range of multiplication results.
  LLVM_ABI ConstantRange multiply(const ConstantRange &Other,
                                  unsigned NoWrapKind = 0) const;

  /// Return a signed multiplication range, or full if overflow is possible.
  ///
  /// Return range of possible values for a signed multiplication of this and
  /// \p Other. However, if overflow is possible always return a full range
  /// rather than trying to determine a more precise result.
  /// \param Other The right-hand-side operand range.
  /// \return The range of signed multiplication results, or a full set if
  /// overflow is possible.
  LLVM_ABI ConstantRange smul_fast(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a signed maximum of a value in this range and a value in \p Other.
  /// \param Other The right-hand-side operand range.
  /// \return The range of signed maximum results.
  LLVM_ABI ConstantRange smax(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an unsigned maximum of a value in this range and a value in \p Other.
  /// \param Other The right-hand-side operand range.
  /// \return The range of unsigned maximum results.
  LLVM_ABI ConstantRange umax(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a signed minimum of a value in this range and a value in \p Other.
  /// \param Other The right-hand-side operand range.
  /// \return The range of signed minimum results.
  LLVM_ABI ConstantRange smin(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an unsigned minimum of a value in this range and a value in \p Other.
  /// \param Other The right-hand-side operand range.
  /// \return The range of unsigned minimum results.
  LLVM_ABI ConstantRange umin(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an unsigned division of a value in this range and a value in
  /// \p Other.
  /// \param Other The right-hand-side operand range.
  /// \return The range of unsigned division results.
  LLVM_ABI ConstantRange udiv(const ConstantRange &Other) const;

  /// Return the range of values from a signed division by Other.
  ///
  /// Return a new range representing the possible values resulting from a
  /// signed division of a value in this range and a value in \p Other. Division
  /// by zero and division of SignedMin by -1 are considered undefined behavior,
  /// in line with IR, and do not contribute towards the result.
  /// \param Other The right-hand-side operand range.
  /// \return The range of signed division results.
  LLVM_ABI ConstantRange sdiv(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an unsigned remainder operation of a value in this range and a
  /// value in \p Other.
  /// \param Other The right-hand-side operand range.
  /// \return The range of unsigned remainder results.
  LLVM_ABI ConstantRange urem(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a signed remainder operation of a value in this range and a
  /// value in \p Other.
  /// \param Other The right-hand-side operand range.
  /// \return The range of signed remainder results.
  LLVM_ABI ConstantRange srem(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting from
  /// a binary-xor of a value in this range by an all-one value,
  /// aka bitwise complement operation.
  /// \return The range of bitwise complement results.
  LLVM_ABI ConstantRange binaryNot() const;

  /// Return a new range representing the possible values resulting
  /// from a binary-and of a value in this range by a value in \p Other.
  /// \param Other The right-hand-side operand range.
  /// \return The range of bitwise AND results.
  LLVM_ABI ConstantRange binaryAnd(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a binary-or of a value in this range by a value in \p Other.
  /// \param Other The right-hand-side operand range.
  /// \return The range of bitwise OR results.
  LLVM_ABI ConstantRange binaryOr(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a binary-xor of a value in this range by a value in \p Other.
  /// \param Other The right-hand-side operand range.
  /// \return The range of bitwise XOR results.
  LLVM_ABI ConstantRange binaryXor(const ConstantRange &Other) const;

  /// Return the range of values from a left shift by Other.
  ///
  /// Return a new range representing the possible values resulting from a left
  /// shift of a value in this range by a value in \p Other.
  /// TODO: This isn't fully implemented yet.
  /// \param Other The right-hand-side shift-amount range.
  /// \return The range of left-shift results.
  LLVM_ABI ConstantRange shl(const ConstantRange &Other) const;

  /// Return the range of values from a left shift with wrap type NoWrapKind.
  ///
  /// Return a new range representing the possible values resulting from a left
  /// shift with wrap type \p NoWrapKind of a value in this range and a value in
  /// \p Other. If the result range is disjoint, the preferred range is
  /// determined by the \p PreferredRangeType.
  /// \param Other The right-hand-side shift-amount range.
  /// \param NoWrapKind Known wrapping constraints for the shift.
  /// \param RangeType How to prefer among multiple valid result ranges.
  /// \return The range of left-shift results under the given wrap constraints.
  LLVM_ABI ConstantRange
  shlWithNoWrap(const ConstantRange &Other, unsigned NoWrapKind,
                PreferredRangeType RangeType = Smallest) const;

  /// Return a new range representing the possible values resulting from a
  /// logical right shift of a value in this range and a value in \p Other.
  /// \param Other The right-hand-side shift-amount range.
  /// \return The range of logical right-shift results.
  LLVM_ABI ConstantRange lshr(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting from a
  /// arithmetic right shift of a value in this range and a value in \p Other.
  /// \param Other The right-hand-side shift-amount range.
  /// \return The range of arithmetic right-shift results.
  LLVM_ABI ConstantRange ashr(const ConstantRange &Other) const;

  /// Perform an unsigned saturating addition of two constant ranges.
  /// \param Other The right-hand-side operand range.
  /// \return The range of unsigned saturating addition results.
  LLVM_ABI ConstantRange uadd_sat(const ConstantRange &Other) const;

  /// Perform a signed saturating addition of two constant ranges.
  /// \param Other The right-hand-side operand range.
  /// \return The range of signed saturating addition results.
  LLVM_ABI ConstantRange sadd_sat(const ConstantRange &Other) const;

  /// Perform an unsigned saturating subtraction of two constant ranges.
  /// \param Other The right-hand-side operand range.
  /// \return The range of unsigned saturating subtraction results.
  LLVM_ABI ConstantRange usub_sat(const ConstantRange &Other) const;

  /// Perform a signed saturating subtraction of two constant ranges.
  /// \param Other The right-hand-side operand range.
  /// \return The range of signed saturating subtraction results.
  LLVM_ABI ConstantRange ssub_sat(const ConstantRange &Other) const;

  /// Perform an unsigned saturating multiplication of two constant ranges.
  /// \param Other The right-hand-side operand range.
  /// \return The range of unsigned saturating multiplication results.
  LLVM_ABI ConstantRange umul_sat(const ConstantRange &Other) const;

  /// Perform a signed saturating multiplication of two constant ranges.
  /// \param Other The right-hand-side operand range.
  /// \return The range of signed saturating multiplication results.
  LLVM_ABI ConstantRange smul_sat(const ConstantRange &Other) const;

  /// Perform an unsigned saturating left shift of this constant range by a
  /// value in \p Other.
  /// \param Other The right-hand-side shift-amount range.
  /// \return The range of unsigned saturating left-shift results.
  LLVM_ABI ConstantRange ushl_sat(const ConstantRange &Other) const;

  /// Perform a signed saturating left shift of this constant range by a
  /// value in \p Other.
  /// \param Other The right-hand-side shift-amount range.
  /// \return The range of signed saturating left-shift results.
  LLVM_ABI ConstantRange sshl_sat(const ConstantRange &Other) const;

  /// Return a new range that is the logical not of the current set.
  /// \return The complement of this range.
  LLVM_ABI ConstantRange inverse() const;

  /// Calculate absolute value range.
  ///
  /// If the original range contains signed min, then the resulting range will
  /// contain signed min if and only if \p IntMinIsPoison is false.
  /// \param IntMinIsPoison Whether abs of signed min is poison.
  /// \return The range of absolute values for values in this range.
  LLVM_ABI ConstantRange abs(bool IntMinIsPoison = false) const;

  /// Calculate ctlz range. If \p ZeroIsPoison is set, the range is computed
  /// ignoring a possible zero value contained in the input range.
  /// \param ZeroIsPoison Whether a zero input is treated as poison.
  /// \return The range of leading-zero counts for values in this range.
  LLVM_ABI ConstantRange ctlz(bool ZeroIsPoison = false) const;

  /// Calculate cttz range. If \p ZeroIsPoison is set, the range is computed
  /// ignoring a possible zero value contained in the input range.
  /// \param ZeroIsPoison Whether a zero input is treated as poison.
  /// \return The range of trailing-zero counts for values in this range.
  LLVM_ABI ConstantRange cttz(bool ZeroIsPoison = false) const;

  /// Calculate ctpop range.
  /// \return The range of population-count results for values in this range.
  LLVM_ABI ConstantRange ctpop() const;

  /// Calculate sqrtFloor range.  See APInt::sqrtFloor().
  /// \return The range of floor square-root results for values in this range.
  LLVM_ABI ConstantRange sqrtFloor() const;

  /// Represents whether an operation on the given constant range is known to
  /// always or never overflow.
  enum class OverflowResult {
    /// Always overflows in the direction of signed/unsigned min value.
    AlwaysOverflowsLow,
    /// Always overflows in the direction of signed/unsigned max value.
    AlwaysOverflowsHigh,
    /// May or may not overflow.
    MayOverflow,
    /// Never overflows.
    NeverOverflows,
  };

  /// Return whether unsigned add of the two ranges always/never overflows.
  /// \param Other The right-hand-side operand range.
  /// \return Whether the unsigned addition always, never, or may overflow.
  LLVM_ABI OverflowResult
  unsignedAddMayOverflow(const ConstantRange &Other) const;

  /// Return whether signed add of the two ranges always/never overflows.
  /// \param Other The right-hand-side operand range.
  /// \return Whether the signed addition always, never, or may overflow.
  LLVM_ABI OverflowResult
  signedAddMayOverflow(const ConstantRange &Other) const;

  /// Return whether unsigned sub of the two ranges always/never overflows.
  /// \param Other The right-hand-side operand range.
  /// \return Whether the unsigned subtraction always, never, or may overflow.
  LLVM_ABI OverflowResult
  unsignedSubMayOverflow(const ConstantRange &Other) const;

  /// Return whether signed sub of the two ranges always/never overflows.
  /// \param Other The right-hand-side operand range.
  /// \return Whether the signed subtraction always, never, or may overflow.
  LLVM_ABI OverflowResult
  signedSubMayOverflow(const ConstantRange &Other) const;

  /// Return whether unsigned mul of the two ranges always/never overflows.
  /// \param Other The right-hand-side operand range.
  /// \return Whether the unsigned multiplication always, never, or may
  /// overflow.
  LLVM_ABI OverflowResult
  unsignedMulMayOverflow(const ConstantRange &Other) const;

  /// Return known bits for values in this range.
  /// \return Known bits common to all values in this range.
  LLVM_ABI KnownBits toKnownBits() const;

  /// Print out the bounds to a stream.
  /// \param OS The output stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Allow printing from a debugger easily.
  LLVM_ABI void dump() const;
};

/// Print the constant range \p CR to the output stream \p OS.
/// \param OS The output stream to print to.
/// \param CR The constant range to print.
/// \return The output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const ConstantRange &CR) {
  CR.print(OS);
  return OS;
}

/// Parse out a conservative ConstantRange from !range metadata.
///
/// E.g. if RangeMD is !{i32 0, i32 10, i32 15, i32 20} then return [0, 20).
/// \param RangeMD The !range metadata node to parse.
/// \return A conservative ConstantRange covering all intervals in the metadata.
LLVM_ABI ConstantRange getConstantRangeFromMetadata(const MDNode &RangeMD);

} // end namespace llvm

#endif // LLVM_IR_CONSTANTRANGE_H
