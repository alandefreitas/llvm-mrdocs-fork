//===- llvm/Analysis/ValueTracking.h - Walk computations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains routines that help analyze properties that chains of
// computations have.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_VALUETRACKING_H
#define LLVM_ANALYSIS_VALUETRACKING_H

#include "llvm/Analysis/SimplifyQuery.h"
#include "llvm/Analysis/WithCache.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>

namespace llvm {

class Operator;
class AddOperator;
class AssumptionCache;
class DominatorTree;
class GEPOperator;
class WithOverflowInst;
struct KnownBits;
struct KnownFPClass;
class Loop;
class LoopInfo;
class MDNode;
class StringRef;
class TargetLibraryInfo;
class IntrinsicInst;
template <typename T> class ArrayRef;

/// Maximum recursion depth for analysis queries in this header.
constexpr unsigned MaxAnalysisRecursionDepth = 6;

/// The max limit of the search depth in DecomposeGEPExpression() and
/// getUnderlyingObject().
constexpr unsigned MaxLookupSearchDepth = 10;

/// Determine which bits of V are known to be either zero or one and return
/// them in the KnownZero/KnownOne bit sets.
///
/// This function is defined on values with integer type, values with pointer
/// type, and vectors of integers.  In the case
/// where V is a vector, the known zero and known one values are the
/// same width as the vector element, and the bit is set only if it is true
/// for all of the elements in the vector.
/// @param V Value to analyze.
/// @param Known Output known-zero and known-one bits.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param CxtI Optional context instruction for local analysis.
/// @param DT Optional dominator tree for context-sensitive analysis.
/// @param UseInstrInfo Whether to use instruction-level information.
/// @param Depth Current recursion depth for this query.
LLVM_ABI void computeKnownBits(const Value *V, KnownBits &Known,
                               const DataLayout &DL,
                               AssumptionCache *AC = nullptr,
                               const Instruction *CxtI = nullptr,
                               const DominatorTree *DT = nullptr,
                               bool UseInstrInfo = true, unsigned Depth = 0);

/// Returns the known bits rather than passing by reference.
/// @param V Value to analyze.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param CxtI Optional context instruction for local analysis.
/// @param DT Optional dominator tree for context-sensitive analysis.
/// @param UseInstrInfo Whether to use instruction-level information.
/// @param Depth Current recursion depth for this query.
/// @return Known-zero and known-one bits for \p V.
LLVM_ABI KnownBits computeKnownBits(const Value *V, const DataLayout &DL,
                                    AssumptionCache *AC = nullptr,
                                    const Instruction *CxtI = nullptr,
                                    const DominatorTree *DT = nullptr,
                                    bool UseInstrInfo = true,
                                    unsigned Depth = 0);

/// Returns the known bits rather than passing by reference.
/// @param V Value to analyze.
/// @param DemandedElts Demanded vector elements for the query.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param CxtI Optional context instruction for local analysis.
/// @param DT Optional dominator tree for context-sensitive analysis.
/// @param UseInstrInfo Whether to use instruction-level information.
/// @param Depth Current recursion depth for this query.
/// @return Known-zero and known-one bits for the demanded elements of \p V.
LLVM_ABI KnownBits computeKnownBits(const Value *V, const APInt &DemandedElts,
                                    const DataLayout &DL,
                                    AssumptionCache *AC = nullptr,
                                    const Instruction *CxtI = nullptr,
                                    const DominatorTree *DT = nullptr,
                                    bool UseInstrInfo = true,
                                    unsigned Depth = 0);

/// Compute known bits for the demanded elements of \p V using \p Q.
/// @param V Value to analyze.
/// @param DemandedElts Demanded vector elements for the query.
/// @param Q Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return Known-zero and known-one bits for the demanded elements of \p V.
LLVM_ABI KnownBits computeKnownBits(const Value *V, const APInt &DemandedElts,
                                    const SimplifyQuery &Q, unsigned Depth = 0);

/// Compute known bits for \p V using the simplify query \p Q.
/// @param V Value to analyze.
/// @param Q Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return Known-zero and known-one bits for \p V.
LLVM_ABI KnownBits computeKnownBits(const Value *V, const SimplifyQuery &Q,
                                    unsigned Depth = 0);

/// Compute known bits for \p V into \p Known using the simplify query \p Q.
/// @param V Value to analyze.
/// @param Known Output known-zero and known-one bits.
/// @param Q Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
LLVM_ABI void computeKnownBits(const Value *V, KnownBits &Known,
                               const SimplifyQuery &Q, unsigned Depth = 0);

/// Compute known bits from the range metadata.
/// \p KnownZero the set of bits that are known to be zero
/// \p KnownOne the set of bits that are known to be one
/// @param Ranges Range metadata describing possible values.
/// @param Known Output known-zero and known-one bits.
LLVM_ABI void computeKnownBitsFromRangeMetadata(const MDNode &Ranges,
                                                KnownBits &Known);

/// Merge bits known from context-dependent facts into Known.
/// @param V Value whose context-dependent known bits are merged.
/// @param Known Known bits to update with context facts.
/// @param Q Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
LLVM_ABI void computeKnownBitsFromContext(const Value *V, KnownBits &Known,
                                          const SimplifyQuery &Q,
                                          unsigned Depth = 0);

/// Using KnownBits LHS/RHS produce the known bits for logic op (and/xor/or).
/// @param I And, or, or xor operator being analyzed.
/// @param KnownLHS Known bits of the left-hand operand.
/// @param KnownRHS Known bits of the right-hand operand.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return Known bits of the logic operation result.
LLVM_ABI KnownBits analyzeKnownBitsFromAndXorOr(const Operator *I,
                                                const KnownBits &KnownLHS,
                                                const KnownBits &KnownRHS,
                                                const SimplifyQuery &SQ,
                                                unsigned Depth = 0);

/// Adjust \p Known for the given select \p Arm to include information from the
/// select \p Cond.
/// @param Known Known bits to refine for the select arm.
/// @param Cond Select condition used to refine known bits.
/// @param Arm Selected value for this arm of the select.
/// @param Invert Whether to invert the meaning of \p Cond.
/// @param Q Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
LLVM_ABI void adjustKnownBitsForSelectArm(KnownBits &Known, Value *Cond,
                                          Value *Arm, bool Invert,
                                          const SimplifyQuery &Q,
                                          unsigned Depth = 0);

/// Adjust \p Known for the given select \p Arm to include information from the
/// select \p Cond.
/// @param Known Known FP class to refine for the select arm.
/// @param Cond Select condition used to refine known FP class.
/// @param Arm Selected value for this arm of the select.
/// @param Invert Whether to invert the meaning of \p Cond.
/// @param Q Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
LLVM_ABI void adjustKnownFPClassForSelectArm(KnownFPClass &Known, Value *Cond,
                                             Value *Arm, bool Invert,
                                             const SimplifyQuery &Q,
                                             unsigned Depth = 0);

/// Strength of the no-common-bits-set relationship between two values.
enum class NoCommonBitsSetResult {
  /// Not known to have no common set bits.
  Unknown,

  /// Known to have no common set bits only if undef values are ignored.
  OnlyIfUndefIgnored,

  /// Known to have no common set bits.
  Known,
};

/// Return how strongly LHS and RHS are known to have no common set bits.
/// @param LHSCache Cached left-hand value for the query.
/// @param RHSCache Cached right-hand value for the query.
/// @param SQ Simplify query providing context for the analysis.
/// @return How strongly the values are known to have no common set bits.
LLVM_ABI NoCommonBitsSetResult getNoCommonBitsSetResult(
    const WithCache<const Value *> &LHSCache,
    const WithCache<const Value *> &RHSCache, const SimplifyQuery &SQ);

/// Return true if LHS and RHS have no common bits set.
/// @param LHSCache Cached left-hand value for the query.
/// @param RHSCache Cached right-hand value for the query.
/// @param SQ Simplify query providing context for the analysis.
/// @return True if LHS and RHS have no common bits set.
LLVM_ABI bool haveNoCommonBitsSet(const WithCache<const Value *> &LHSCache,
                                  const WithCache<const Value *> &RHSCache,
                                  const SimplifyQuery &SQ);

/// Return true if the given value is known to have exactly one bit set when
/// defined.
///
/// For vectors return true if every element is known to be a power of two when
/// defined. Supports values with integer or pointer type and vectors of
/// integers. If 'OrZero' is set, then return true if the given value is either
/// a power of two or zero.
/// @param V Value to test for being a power of two.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param OrZero Also accept zero as a successful result.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param CxtI Optional context instruction for local analysis.
/// @param DT Optional dominator tree for context-sensitive analysis.
/// @param UseInstrInfo Whether to use instruction-level information.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is a power of two when defined (or zero if \p OrZero).
LLVM_ABI bool isKnownToBeAPowerOfTwo(const Value *V, const DataLayout &DL,
                                     bool OrZero = false,
                                     AssumptionCache *AC = nullptr,
                                     const Instruction *CxtI = nullptr,
                                     const DominatorTree *DT = nullptr,
                                     bool UseInstrInfo = true,
                                     unsigned Depth = 0);

/// Return true if \p V is known to be a power of two when defined.
/// @param V Value to test for being a power of two.
/// @param OrZero Also accept zero as a successful result.
/// @param Q Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is a power of two when defined (or zero if \p OrZero).
LLVM_ABI bool isKnownToBeAPowerOfTwo(const Value *V, bool OrZero,
                                     const SimplifyQuery &Q,
                                     unsigned Depth = 0);

/// Return true if \p CxtI is only used in comparisons against zero.
/// @param CxtI Instruction whose uses are inspected.
/// @return True if every use compares the value against zero.
LLVM_ABI bool isOnlyUsedInZeroComparison(const Instruction *CxtI);

/// Return true if \p CxtI is only used in equality comparisons against zero.
/// @param CxtI Instruction whose uses are inspected.
/// @return True if every use is an equality comparison against zero.
LLVM_ABI bool isOnlyUsedInZeroEqualityComparison(const Instruction *CxtI);

/// Return true if the given value is known to be non-zero when defined.
///
/// For vectors, return true if every element is known to be non-zero when
/// defined. For pointers, if the context instruction and dominator tree are
/// specified, perform context-sensitive analysis and return true if the
/// pointer couldn't possibly be null at the specified instruction.
/// Supports values with integer or pointer type and vectors of integers.
/// @param V Value to test for being non-zero.
/// @param Q Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is known non-zero when defined.
LLVM_ABI bool isKnownNonZero(const Value *V, const SimplifyQuery &Q,
                             unsigned Depth = 0);

/// Return true if the two given values are negation.
///
/// Currently can recoginze Value pair:
/// 1: <X, Y> if X = sub (0, Y) or Y = sub (0, X)
/// 2: <X, Y> if X = sub (A, B) and Y = sub (B, A)
/// @param X First value of the candidate negation pair.
/// @param Y Second value of the candidate negation pair.
/// @param NeedNSW Require the negation to be NSW.
/// @param AllowPoison Whether poison in the pair is allowed.
/// @return True if \p X and \p Y are known to be negations of each other.
LLVM_ABI bool isKnownNegation(const Value *X, const Value *Y,
                              bool NeedNSW = false, bool AllowPoison = true);

/// Return true iff:
/// 1. X is poison implies Y is poison.
/// 2. X is true implies Y is false.
/// 3. X is false implies Y is true.
/// Otherwise, return false.
/// @param X First boolean value of the candidate inversion pair.
/// @param Y Second boolean value of the candidate inversion pair.
/// @return True if \p X and \p Y are known boolean inverses.
LLVM_ABI bool isKnownInversion(const Value *X, const Value *Y);

/// Returns true if the give value is known to be non-negative.
/// @param V Value to test for being non-negative.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is known non-negative.
LLVM_ABI bool isKnownNonNegative(const Value *V, const SimplifyQuery &SQ,
                                 unsigned Depth = 0);

/// Returns true if the given value is known be positive (i.e. non-negative
/// and non-zero).
/// @param V Value to test for being positive.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is known positive.
LLVM_ABI bool isKnownPositive(const Value *V, const SimplifyQuery &SQ,
                              unsigned Depth = 0);

/// Returns true if the given value is known be negative (i.e. non-positive
/// and non-zero).
/// @param V Value to test for being negative.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is known negative.
LLVM_ABI bool isKnownNegative(const Value *V, const SimplifyQuery &SQ,
                              unsigned Depth = 0);

/// Return true if the given values are known to be non-equal when defined.
/// Supports scalar integer types only.
/// @param V1 First value of the inequality query.
/// @param V2 Second value of the inequality query.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V1 and \p V2 are known unequal when defined.
LLVM_ABI bool isKnownNonEqual(const Value *V1, const Value *V2,
                              const SimplifyQuery &SQ, unsigned Depth = 0);

/// Return true if 'V & Mask' is known to be zero. We use this predicate to
/// simplify operations downstream. Mask is known to be zero for bits that V
/// cannot have.
///
/// This function is defined on values with integer type, values with pointer
/// type, and vectors of integers.  In the case
/// where V is a vector, the mask, known zero, and known one values are the
/// same width as the vector element, and the bit is set only if it is true
/// for all of the elements in the vector.
/// @param V Value being masked.
/// @param Mask Bits of \p V that must be proven zero.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V masked by \p Mask is known to be zero.
LLVM_ABI bool MaskedValueIsZero(const Value *V, const APInt &Mask,
                                const SimplifyQuery &SQ, unsigned Depth = 0);

/// Return the number of times the sign bit of the register is replicated into
/// the other bits.
///
/// We know that at least 1 bit is always equal to the sign bit (itself), but
/// other cases can give us information. For example, immediately after an
/// "ashr X, 2", we know that the top 3 bits are all equal to each other, so we
/// return 3. For vectors, return the number of sign bits for the vector element
/// with the mininum number of known sign bits.
/// @param Op Value whose leading sign bits are counted.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param CxtI Optional context instruction for local analysis.
/// @param DT Optional dominator tree for context-sensitive analysis.
/// @param UseInstrInfo Whether to use instruction-level information.
/// @param Depth Current recursion depth for this query.
/// @return Number of known replicated sign bits, including the sign bit itself.
LLVM_ABI unsigned ComputeNumSignBits(const Value *Op, const DataLayout &DL,
                                     AssumptionCache *AC = nullptr,
                                     const Instruction *CxtI = nullptr,
                                     const DominatorTree *DT = nullptr,
                                     bool UseInstrInfo = true,
                                     unsigned Depth = 0);

/// Get the upper bound on bit size for this Value \p Op as a signed integer.
///
/// i.e.  x == sext(trunc(x to MaxSignificantBits) to bitwidth(x)).
/// Similar to the APInt::getSignificantBits function.
/// @param Op Value whose maximum significant bit width is computed.
/// @param DL Data layout used for type sizes and pointer widths.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param CxtI Optional context instruction for local analysis.
/// @param DT Optional dominator tree for context-sensitive analysis.
/// @param Depth Current recursion depth for this query.
/// @return Upper bound on significant bits needed to represent \p Op.
LLVM_ABI unsigned ComputeMaxSignificantBits(const Value *Op,
                                            const DataLayout &DL,
                                            AssumptionCache *AC = nullptr,
                                            const Instruction *CxtI = nullptr,
                                            const DominatorTree *DT = nullptr,
                                            unsigned Depth = 0);

/// Map a call instruction to an intrinsic ID.  Libcalls which have equivalent
/// intrinsics are treated as-if they were intrinsics.
/// @param CB Call site to map to an intrinsic ID.
/// @param TLI Target library info used to recognize libcalls.
/// @return Matching intrinsic ID, or Intrinsic::not_intrinsic.
LLVM_ABI Intrinsic::ID getIntrinsicForCallSite(const CallBase &CB,
                                               const TargetLibraryInfo *TLI);

/// Given an exploded icmp instruction, return true if the comparison only
/// checks the sign bit.
///
/// If it only checks the sign bit, set TrueIfSigned if the result of the
/// comparison is true when the input value is signed.
/// @param Pred ICmp predicate of the exploded comparison.
/// @param RHS Constant right-hand side of the comparison.
/// @param TrueIfSigned Set to true if a signed input makes the compare true.
/// @return True if the comparison only inspects the sign bit.
LLVM_ABI bool isSignBitCheck(ICmpInst::Predicate Pred, const APInt &RHS,
                             bool &TrueIfSigned);

/// Determine which floating-point classes are valid for \p V, and return them
/// in KnownFPClass bit sets.
///
/// This function is defined on values with floating-point type, values vectors
/// of floating-point type, and arrays of floating-point type.
///
/// \p InterestedClasses is a compile time optimization hint for which floating
/// point classes should be queried. Queries not specified in \p
/// InterestedClasses should be reliable if they are determined during the
/// query.
/// @param V Floating-point value to analyze.
/// @param DemandedElts Demanded vector elements for the query.
/// @param InterestedClasses FP classes the caller is interested in.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return Known floating-point classes for the demanded elements of \p V.
LLVM_ABI KnownFPClass computeKnownFPClass(const Value *V,
                                          const APInt &DemandedElts,
                                          FPClassTest InterestedClasses,
                                          const SimplifyQuery &SQ,
                                          unsigned Depth = 0);

/// Determine known floating-point classes for \p V.
/// @param V Floating-point value to analyze.
/// @param InterestedClasses FP classes the caller is interested in.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return Known floating-point classes for \p V.
LLVM_ABI KnownFPClass computeKnownFPClass(const Value *V,
                                          FPClassTest InterestedClasses,
                                          const SimplifyQuery &SQ,
                                          unsigned Depth = 0);

/// Determine known floating-point classes for \p V using explicit analysis
/// context.
/// @param V Floating-point value to analyze.
/// @param DL Data layout used for type sizes.
/// @param InterestedClasses FP classes the caller is interested in.
/// @param TLI Optional target library info.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param CxtI Optional context instruction for local analysis.
/// @param DT Optional dominator tree for context-sensitive analysis.
/// @param UseInstrInfo Whether to use instruction-level information.
/// @param Depth Current recursion depth for this query.
/// @return Known floating-point classes for \p V.
LLVM_ABI KnownFPClass computeKnownFPClass(
    const Value *V, const DataLayout &DL,
    FPClassTest InterestedClasses = fcAllFlags,
    const TargetLibraryInfo *TLI = nullptr, AssumptionCache *AC = nullptr,
    const Instruction *CxtI = nullptr, const DominatorTree *DT = nullptr,
    bool UseInstrInfo = true, unsigned Depth = 0);

/// Wrapper to account for known fast math flags at the use instruction.
/// @param V Floating-point value to analyze.
/// @param DemandedElts Demanded vector elements for the query.
/// @param FMF Fast-math flags from the use site.
/// @param InterestedClasses FP classes the caller is interested in.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return Known floating-point classes for the demanded elements of \p V.
LLVM_ABI KnownFPClass computeKnownFPClass(
    const Value *V, const APInt &DemandedElts, FastMathFlags FMF,
    FPClassTest InterestedClasses, const SimplifyQuery &SQ, unsigned Depth = 0);

/// Wrapper to account for known fast math flags at the use instruction.
/// @param V Floating-point value to analyze.
/// @param FMF Fast-math flags from the use site.
/// @param InterestedClasses FP classes the caller is interested in.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return Known floating-point classes for \p V.
LLVM_ABI KnownFPClass computeKnownFPClass(const Value *V, FastMathFlags FMF,
                                          FPClassTest InterestedClasses,
                                          const SimplifyQuery &SQ,
                                          unsigned Depth = 0);

/// Return true if we can prove that the specified FP value is never equal to
/// -0.0. Users should use caution when considering PreserveSign
/// denormal-fp-math.
/// @param V Floating-point value to test.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V cannot be negative zero.
LLVM_ABI bool cannotBeNegativeZero(const Value *V, const SimplifyQuery &SQ,
                                   unsigned Depth = 0);

/// Return true if we can prove that the specified FP value is either NaN or
/// never less than -0.0.
///
///      NaN --> true
///       +0 --> true
///       -0 --> true
///   x > +0 --> true
///   x < -0 --> false
/// @param V Floating-point value to test.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is NaN or not ordered-less-than zero.
LLVM_ABI bool cannotBeOrderedLessThanZero(const Value *V,
                                          const SimplifyQuery &SQ,
                                          unsigned Depth = 0);

/// Return true if the floating-point scalar value is not an infinity or if
/// the floating-point vector value has no infinities.
///
/// Return false if a value could ever be infinity.
/// @param V Floating-point value to test.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is known never to be infinity.
LLVM_ABI bool isKnownNeverInfinity(const Value *V, const SimplifyQuery &SQ,
                                   unsigned Depth = 0);

/// Return true if the floating-point value can never contain a NaN or infinity.
/// @param V Floating-point value to test.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is known never to be NaN or infinity.
LLVM_ABI bool isKnownNeverInfOrNaN(const Value *V, const SimplifyQuery &SQ,
                                   unsigned Depth = 0);

/// Return true if the floating-point scalar value is not a NaN or if the
/// floating-point vector value has no NaN elements.
///
/// Return false if a value could ever be NaN.
/// @param V Floating-point value to test.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is known never to be NaN.
LLVM_ABI bool isKnownNeverNaN(const Value *V, const SimplifyQuery &SQ,
                              unsigned Depth = 0);

/// Return the known sign bit of a floating-point value, if provable.
///
/// Return false if we can prove that the specified FP value's sign bit is 0.
/// Return true if we can prove that the specified FP value's sign bit is 1.
/// Otherwise return std::nullopt.
/// @param V Floating-point value whose sign bit is queried.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return Known sign bit, or nullopt if it cannot be proven.
LLVM_ABI std::optional<bool> computeKnownFPSignBit(const Value *V,
                                                   const SimplifyQuery &SQ,
                                                   unsigned Depth = 0);

/// Return true if the sign bit of the FP value can be ignored by the user when
/// the value is zero.
/// @param U Use of the floating-point value being tested.
/// @return True if a zero value's sign bit is unused at \p U.
LLVM_ABI bool canIgnoreSignBitOfZero(const Use &U);

/// Return true if the sign bit of the FP value can be ignored by the user when
/// the value is NaN.
/// @param U Use of the floating-point value being tested.
/// @return True if a NaN value's sign bit is unused at \p U.
LLVM_ABI bool canIgnoreSignBitOfNaN(const Use &U);

/// Return true if the floating-point value \p V is known to be an integer
/// value.
/// @param V Floating-point value to test.
/// @param SQ Simplify query providing context for the analysis.
/// @param FMF Fast-math flags that apply to the query.
/// @return True if \p V is known to be an integral floating-point value.
LLVM_ABI bool isKnownIntegral(const Value *V, const SimplifyQuery &SQ,
                              FastMathFlags FMF);

/// If the specified value can be set by repeating the same byte in memory,
/// return the i8 value that it is represented with.
///
/// This is true for all i8 values obviously, but is also true for i32 0,
/// i32 -1, i16 0xF0F0, double 0.0 etc. If the value can't be handled with a
/// repeated byte store (e.g. i16 0x1234), return null. If the value is entirely
/// undef and padding, return undef.
/// @param V Value that may be representable as a repeated byte.
/// @param DL Data layout used to interpret the value's in-memory layout.
/// @return Repeated byte value, undef, or null if not bytewise.
LLVM_ABI Value *isBytewiseValue(Value *V, const DataLayout &DL);

/// Given an aggregate and a sequence of indices, see if the scalar value
/// indexed is already around as a register.
///
/// For example, this succeeds if the scalar were inserted directly into the
/// aggregate. If InsertBefore is not empty, this function will duplicate
/// (modified) insertvalues when a part of a nested struct is extracted.
/// @param V Aggregate value being indexed.
/// @param idx_range Indices selecting the scalar element.
/// @param InsertBefore Optional insertion point for reconstructed inserts.
/// @return Existing scalar value for the indices, or null if none is found.
LLVM_ABI Value *FindInsertedValue(
    Value *V, ArrayRef<unsigned> idx_range,
    std::optional<BasicBlock::iterator> InsertBefore = std::nullopt);

/// Analyze the specified pointer to see if it can be expressed as a base
/// pointer plus a constant offset. Return the base and offset to the caller.
///
/// This is a wrapper around Value::stripAndAccumulateConstantOffsets that
/// creates and later unpacks the required APInt.
/// @param Ptr Pointer to decompose into base plus offset.
/// @param Offset Output constant offset from the returned base.
/// @param DL Data layout used for pointer index sizes.
/// @param AllowNonInbounds Whether non-inbounds GEPs may contribute offsets.
/// @return Base pointer after stripping constant offsets.
inline Value *GetPointerBaseWithConstantOffset(Value *Ptr, int64_t &Offset,
                                               const DataLayout &DL,
                                               bool AllowNonInbounds = true) {
  APInt OffsetAPInt(DL.getIndexTypeSizeInBits(Ptr->getType()), 0);
  Value *Base =
      Ptr->stripAndAccumulateConstantOffsets(DL, OffsetAPInt, AllowNonInbounds);

  Offset = OffsetAPInt.getSExtValue();
  return Base;
}
/// Const overload of GetPointerBaseWithConstantOffset.
/// @param Ptr Pointer to decompose into base plus offset.
/// @param Offset Output constant offset from the returned base.
/// @param DL Data layout used for pointer index sizes.
/// @param AllowNonInbounds Whether non-inbounds GEPs may contribute offsets.
/// @return Base pointer after stripping constant offsets.
inline const Value *
GetPointerBaseWithConstantOffset(const Value *Ptr, int64_t &Offset,
                                 const DataLayout &DL,
                                 bool AllowNonInbounds = true) {
  return GetPointerBaseWithConstantOffset(const_cast<Value *>(Ptr), Offset, DL,
                                          AllowNonInbounds);
}

/// Represents offset+length into a ConstantDataArray.
struct ConstantDataArraySlice {
  /// ConstantDataArray pointer. nullptr indicates a zeroinitializer (a valid
  /// initializer, it just doesn't fit the ConstantDataArray interface).
  const ConstantDataArray *Array;

  /// Slice starts at this Offset.
  uint64_t Offset;

  /// Length of the slice.
  uint64_t Length;

  /// Moves the Offset and adjusts Length accordingly.
  /// @param Delta Number of elements to advance the slice by.
  void move(uint64_t Delta) {
    assert(Delta < Length);
    Offset += Delta;
    Length -= Delta;
  }

  /// Convenience accessor for elements in the slice.
  /// @param I Index within the slice.
  /// @return Element at slice index \p I, or zero for zeroinitializer.
  uint64_t operator[](unsigned I) const {
    return Array == nullptr ? 0 : Array->getElementAsInteger(I + Offset);
  }
};

/// Returns true if the value \p V is a pointer into a ConstantDataArray.
/// If successful \p Slice will point to a ConstantDataArray info object
/// with an appropriate offset.
/// @param V Pointer value that may address a constant data array.
/// @param Slice Output slice describing the constant data region.
/// @param ElementSize Size in bits of each array element.
/// @param Offset Additional byte offset applied before matching.
/// @return True if \p V points into a ConstantDataArray (or zeroinitializer).
LLVM_ABI bool getConstantDataArrayInfo(const Value *V,
                                       ConstantDataArraySlice &Slice,
                                       unsigned ElementSize,
                                       uint64_t Offset = 0);

/// Compute the contents of a null-terminated C string pointed to by \p V.
///
/// If successful, it returns true and returns the string in Str. If
/// unsuccessful, it returns false. This does not include the trailing null
/// character by default. If TrimAtNul is set to false, then this returns any
/// trailing null characters as well as any other characters that come after
/// it.
/// @param V Pointer to a potential constant C string.
/// @param Str Output string contents on success.
/// @param TrimAtNul Whether to stop at the first nul character.
/// @return True if a constant string was successfully recovered.
LLVM_ABI bool getConstantStringInfo(const Value *V, StringRef &Str,
                                    bool TrimAtNul = true);

/// If we can compute the length of the string pointed to by the specified
/// pointer, return 'len+1'.  If we can't, return 0.
/// @param V Pointer to a potential constant string.
/// @param CharSize Character width in bits (usually 8).
/// @return String length plus one, or zero if unknown.
LLVM_ABI uint64_t GetStringLength(const Value *V, unsigned CharSize = 8);

/// Return the call pointer argument that aliasing rules treat as the returned
/// pointer.
///
/// You CAN'T use it to replace one value with another. If \p MustPreserveOffset
/// is true, the call must preserve the byte offset of the pointer within its
/// underlying object. Offset preservation implies nullness preservation; pass
/// true when callers reason about either offset or null equality (e.g. GEP
/// decomposition, dereferenceability, isKnownNonZero).
/// @param Call Call whose returned pointer may alias an argument.
/// @param MustPreserveOffset Require the call to preserve the pointer offset.
/// @return Argument that aliases the returned pointer, or null.
LLVM_ABI const Value *
getArgumentAliasingToReturnedPointer(const CallBase *Call,
                                     bool MustPreserveOffset);
/// Non-const overload of getArgumentAliasingToReturnedPointer.
/// @param Call Call whose returned pointer may alias an argument.
/// @param MustPreserveOffset Require the call to preserve the pointer offset.
/// @return Argument that aliases the returned pointer, or null.
inline Value *getArgumentAliasingToReturnedPointer(CallBase *Call,
                                                   bool MustPreserveOffset) {
  return const_cast<Value *>(getArgumentAliasingToReturnedPointer(
      const_cast<const CallBase *>(Call), MustPreserveOffset));
}

/// Return true if \p Call is an intrinsic that returns a pointer aliasing an
/// argument without capturing it otherwise.
///
/// {launder,strip}.invariant.group returns pointer that aliases its argument,
/// and it only captures pointer by returning it. These intrinsics are not
/// marked as nocapture, because returning is considered as capture. The
/// arguments are not marked as returned neither, because it would make it
/// useless. If \p MustPreserveOffset is true, the intrinsic must preserve the
/// byte offset of the pointer within its underlying object (which excludes
/// `llvm.ptrmask`, since masking off low bits changes the byte offset while
/// still aliasing the same object).
/// @param Call Call to classify.
/// @param MustPreserveOffset Require the intrinsic to preserve the pointer
/// offset.
/// @return True if the call returns an aliasing argument without other capture.
LLVM_ABI bool isIntrinsicReturningPointerAliasingArgumentWithoutCapturing(
    const CallBase *Call, bool MustPreserveOffset);

/// Strip GEP adjustments, pointer casts, and threadlocal.address from \p V.
///
/// This method strips off any GEP address adjustments, pointer casts or
/// `llvm.threadlocal.address` from the specified value \p V, returning the
/// original object being addressed. Note that the returned value has pointer
/// type if the specified value does. If the \p MaxLookup value is non-zero, it
/// limits the number of instructions to be stripped off.
/// @param V Pointer value to strip to an underlying object.
/// @param MaxLookup Maximum number of stripping steps, or zero for unlimited.
/// @return Underlying object addressed by \p V.
LLVM_ABI const Value *
getUnderlyingObject(const Value *V, unsigned MaxLookup = MaxLookupSearchDepth);
/// Non-const overload of getUnderlyingObject.
/// @param V Pointer value to strip to an underlying object.
/// @param MaxLookup Maximum number of stripping steps, or zero for unlimited.
/// @return Underlying object addressed by \p V.
inline Value *getUnderlyingObject(Value *V,
                                  unsigned MaxLookup = MaxLookupSearchDepth) {
  // Force const to avoid infinite recursion.
  const Value *VConst = V;
  return const_cast<Value *>(getUnderlyingObject(VConst, MaxLookup));
}

/// Like getUnderlyingObject(), but will try harder to find a single underlying
/// object. In particular, this function also looks through selects and phis.
/// @param V Pointer value to strip aggressively to one underlying object.
/// @return Single underlying object if one can be identified.
LLVM_ABI const Value *getUnderlyingObjectAggressive(const Value *V);

/// This method is similar to getUnderlyingObject except that it can
/// look through phi and select instructions and return multiple objects.
///
/// If LoopInfo is passed, loop phis are further analyzed.  If a pointer
/// accesses different objects in each iteration, we don't look through the
/// phi node. E.g. consider this loop nest:
///
///   int **A;
///   for (i)
///     for (j) {
///        A[i][j] = A[i-1][j] * B[j]
///     }
///
/// This is transformed by Load-PRE to stash away A[i] for the next iteration
/// of the outer loop:
///
///   Curr = A[0];          // Prev_0
///   for (i: 1..N) {
///     Prev = Curr;        // Prev = PHI (Prev_0, Curr)
///     Curr = A[i];
///     for (j: 0..N) {
///        Curr[j] = Prev[j] * B[j]
///     }
///   }
///
/// Since A[i] and A[i-1] are independent pointers, getUnderlyingObjects
/// should not assume that Curr and Prev share the same underlying object thus
/// it shouldn't look through the phi above.
/// @param V Pointer value whose underlying objects are collected.
/// @param Objects Output set of underlying objects.
/// @param LI Optional loop info used when analyzing loop phis.
/// @param MaxLookup Maximum number of stripping/lookup steps.
LLVM_ABI void getUnderlyingObjects(const Value *V,
                                   SmallVectorImpl<const Value *> &Objects,
                                   const LoopInfo *LI = nullptr,
                                   unsigned MaxLookup = MaxLookupSearchDepth);

/// This is a wrapper around getUnderlyingObjects and adds support for basic
/// ptrtoint+arithmetic+inttoptr sequences.
/// @param V Pointer value whose underlying objects are collected.
/// @param Objects Output set of underlying objects.
/// @return True if underlying objects were successfully identified.
LLVM_ABI bool getUnderlyingObjectsForCodeGen(const Value *V,
                                             SmallVectorImpl<Value *> &Objects);

/// Returns unique alloca where the value comes from, or nullptr.
/// If OffsetZero is true check that V points to the begining of the alloca.
/// @param V Value that may derive from an alloca.
/// @param OffsetZero Require \p V to point at the start of the alloca.
/// @return Unique source alloca, or null if none/ambiguous.
LLVM_ABI AllocaInst *findAllocaForValue(Value *V, bool OffsetZero = false);
/// Const overload of findAllocaForValue.
/// @param V Value that may derive from an alloca.
/// @param OffsetZero Require \p V to point at the start of the alloca.
/// @return Unique source alloca, or null if none/ambiguous.
inline const AllocaInst *findAllocaForValue(const Value *V,
                                            bool OffsetZero = false) {
  return findAllocaForValue(const_cast<Value *>(V), OffsetZero);
}

/// Return true if the only users of this pointer are lifetime markers.
/// @param V Pointer whose users are inspected.
/// @return True if every user is a lifetime marker.
LLVM_ABI bool onlyUsedByLifetimeMarkers(const Value *V);

/// Return true if the only users of this pointer are lifetime markers or
/// droppable instructions.
/// @param V Pointer whose users are inspected.
/// @return True if every user is a lifetime marker or droppable.
LLVM_ABI bool onlyUsedByLifetimeMarkersOrDroppableInsts(const Value *V);

/// Return true if the instruction doesn't potentially cross vector lanes.
///
/// This condition is weaker than checking that the instruction is lanewise:
/// lanewise means that the same operation is splatted across all lanes, but we
/// also include the case where there is a different operation on each lane, as
/// long as the operation only uses data from that lane. An example of an
/// operation that is not lanewise, but doesn't cross vector lanes is
/// insertelement.
/// @param I Instruction to classify.
/// @return True if \p I does not move data across vector lanes.
LLVM_ABI bool isNotCrossLaneOperation(const Instruction *I);

/// Return true if the instruction does not have any effects besides
/// calculating the result and does not have undefined behavior.
///
/// This method never returns true for an instruction that returns true for
/// mayHaveSideEffects; however, this method also does some other checks in
/// addition. It checks for undefined behavior, like dividing by zero or
/// loading from an invalid pointer (but not for undefined results, like a
/// shift with a shift amount larger than the width of the result). It checks
/// for malloc and alloca because speculatively executing them might cause a
/// memory leak. It also returns false for instructions related to control
/// flow, specifically terminators and PHI nodes.
///
/// If the CtxI is specified this method performs context-sensitive analysis
/// and returns true if it is safe to execute the instruction immediately
/// before the CtxI. If the instruction has (transitive) operands that don't
/// dominate CtxI, the analysis is performed under the assumption that these
/// operands will also be speculated to a point before CxtI.
///
/// If the CtxI is NOT specified this method only looks at the instruction
/// itself and its operands, so if this method returns true, it is safe to
/// move the instruction as long as the correct dominance relationships for
/// the operands and users hold.
///
/// If \p UseVariableInfo is true, the information from non-constant operands
/// will be taken into account.
///
/// If \p IgnoreUBImplyingAttrs is true, UB-implying attributes will be ignored.
/// The caller is responsible for correctly propagating them after hoisting.
///
/// This method can return true for instructions that read memory;
/// for such instructions, moving them may change the resulting value.
/// @param I Instruction to test for speculative safety.
/// @param CtxI Optional context instruction for speculation.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param DT Optional dominator tree for context-sensitive analysis.
/// @param TLI Optional target library info.
/// @param UseVariableInfo Whether non-constant operand info may be used.
/// @param IgnoreUBImplyingAttrs Whether to ignore UB-implying attributes.
/// @return True if \p I is safe to speculate in the given context.
LLVM_ABI bool isSafeToSpeculativelyExecute(
    const Instruction *I, const Instruction *CtxI = nullptr,
    AssumptionCache *AC = nullptr, const DominatorTree *DT = nullptr,
    const TargetLibraryInfo *TLI = nullptr, bool UseVariableInfo = true,
    bool IgnoreUBImplyingAttrs = true);

/// Iterator-context overload of isSafeToSpeculativelyExecute.
/// @param I Instruction to test for speculative safety.
/// @param CtxI Context position expressed as a basic-block iterator.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param DT Optional dominator tree for context-sensitive analysis.
/// @param TLI Optional target library info.
/// @param UseVariableInfo Whether non-constant operand info may be used.
/// @param IgnoreUBImplyingAttrs Whether to ignore UB-implying attributes.
/// @return True if \p I is safe to speculate in the given context.
inline bool isSafeToSpeculativelyExecute(const Instruction *I,
                                         BasicBlock::iterator CtxI,
                                         AssumptionCache *AC = nullptr,
                                         const DominatorTree *DT = nullptr,
                                         const TargetLibraryInfo *TLI = nullptr,
                                         bool UseVariableInfo = true,
                                         bool IgnoreUBImplyingAttrs = true) {
  // Take an iterator, and unwrap it into an Instruction *.
  return isSafeToSpeculativelyExecute(I, &*CtxI, AC, DT, TLI, UseVariableInfo,
                                      IgnoreUBImplyingAttrs);
}

/// Don't use information from its non-constant operands. This helper is used
/// when its operands are going to be replaced.
/// @param I Instruction to test for speculative safety after operand replace.
/// @param IgnoreUBImplyingAttrs Whether to ignore UB-implying attributes.
/// @return True if \p I is safe to speculate without variable operand info.
inline bool isSafeToSpeculativelyExecuteWithVariableReplaced(
    const Instruction *I, bool IgnoreUBImplyingAttrs = true) {
  return isSafeToSpeculativelyExecute(I, nullptr, nullptr, nullptr, nullptr,
                                      /*UseVariableInfo=*/false,
                                      IgnoreUBImplyingAttrs);
}

/// Like isSafeToSpeculativelyExecute, but with \p Opcode overriding Inst.
///
/// This returns the same result as isSafeToSpeculativelyExecute if Opcode is
/// the actual opcode of Inst. If the provided and actual opcode differ, the
/// function (virtually) overrides the opcode of Inst with the provided
/// Opcode. There are come constraints in this case:
/// * If Opcode has a fixed number of operands (eg, as binary operators do),
///   then Inst has to have at least as many leading operands. The function
///   will ignore all trailing operands beyond that number.
/// * If Opcode allows for an arbitrary number of operands (eg, as CallInsts
///   do), then all operands are considered.
/// * The virtual instruction has to satisfy all typing rules of the provided
///   Opcode.
/// * This function is pessimistic in the following sense: If one actually
///   materialized the virtual instruction, then isSafeToSpeculativelyExecute
///   may say that the materialized instruction is speculatable whereas this
///   function may have said that the instruction wouldn't be speculatable.
///   This behavior is a shortcoming in the current implementation and not
///   intentional.
/// @param Opcode Opcode to assume in place of \p Inst's real opcode.
/// @param Inst Instruction providing operands and types for the virtual op.
/// @param CtxI Optional context instruction for speculation.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param DT Optional dominator tree for context-sensitive analysis.
/// @param TLI Optional target library info.
/// @param UseVariableInfo Whether non-constant operand info may be used.
/// @param IgnoreUBImplyingAttrs Whether to ignore UB-implying attributes.
/// @return True if the virtual instruction is safe to speculate.
LLVM_ABI bool isSafeToSpeculativelyExecuteWithOpcode(
    unsigned Opcode, const Instruction *Inst, const Instruction *CtxI = nullptr,
    AssumptionCache *AC = nullptr, const DominatorTree *DT = nullptr,
    const TargetLibraryInfo *TLI = nullptr, bool UseVariableInfo = true,
    bool IgnoreUBImplyingAttrs = true);

/// Returns true if the result or effects of the given instructions \p I
/// depend values not reachable through the def use graph.
///
/// * Memory dependence arises for example if the instruction reads from
///   memory or may produce effects or undefined behaviour. Memory dependent
///   instructions generally cannot be reorderd with respect to other memory
///   dependent instructions.
/// * Control dependence arises for example if the instruction may fault
///   if lifted above a throwing call or infinite loop.
/// @param I Instruction to test for non-def-use dependencies.
/// @return True if \p I may depend on memory or control outside the use-def
/// graph.
LLVM_ABI bool mayHaveNonDefUseDependency(const Instruction &I);

/// Return true if it is an intrinsic that cannot be speculated but also
/// cannot trap.
/// @param I Instruction that may be an assume-like intrinsic.
/// @return True if \p I is an assume-like intrinsic.
LLVM_ABI bool isAssumeLikeIntrinsic(const Instruction *I);

/// Return true if assume \p I is valid to use at context instruction \p CxtI.
///
/// By default, ephemeral values of the assumption are treated as an invalid
/// context, to prevent the assumption from being used to optimize away its
/// argument. If the caller can ensure that this won't happen, it can call with
/// AllowEphemerals set to true to get more valid assumptions.
/// @param I Assume intrinsic providing the knowledge.
/// @param CxtI Context instruction where the assume would be used.
/// @param DT Optional dominator tree for validity checks.
/// @param AllowEphemerals Allow ephemeral values as a valid context.
/// @return True if the assume may be used at \p CxtI.
LLVM_ABI bool isValidAssumeForContext(const Instruction *I,
                                      const Instruction *CxtI,
                                      const DominatorTree *DT = nullptr,
                                      bool AllowEphemerals = false);

/// Return true if assume \p I is valid for the context in simplify query \p Q.
/// @param I Assume intrinsic providing the knowledge.
/// @param Q Simplify query supplying context instruction and related state.
/// @return True if the assume may be used in \p Q's context.
inline bool isValidAssumeForContext(const Instruction *I,
                                    const SimplifyQuery &Q) {
  return isValidAssumeForContext(I, Q.CxtI, Q.DT, Q.AllowEphemerals);
}

/// Returns true, if no instruction between \p Assume and \p CtxI may free
/// (including through synchronization).
/// @param Assume Starting assume instruction.
/// @param CtxI Ending context instruction.
/// @return True if no free can occur on the path between them.
LLVM_ABI bool willNotFreeBetween(const Instruction *Assume,
                                 const Instruction *CtxI);

/// Result of analyzing whether an arithmetic operation can overflow.
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

/// Compute whether an unsigned multiply of \p LHS and \p RHS can overflow.
/// @param LHS Left-hand operand of the multiply.
/// @param RHS Right-hand operand of the multiply.
/// @param SQ Simplify query providing context for the analysis.
/// @param IsNSW Whether the multiply is known NSW.
/// @return Overflow classification for the unsigned multiply.
LLVM_ABI OverflowResult computeOverflowForUnsignedMul(const Value *LHS,
                                                      const Value *RHS,
                                                      const SimplifyQuery &SQ,
                                                      bool IsNSW = false);
/// Compute whether a signed multiply of \p LHS and \p RHS can overflow.
/// @param LHS Left-hand operand of the multiply.
/// @param RHS Right-hand operand of the multiply.
/// @param SQ Simplify query providing context for the analysis.
/// @return Overflow classification for the signed multiply.
LLVM_ABI OverflowResult computeOverflowForSignedMul(const Value *LHS,
                                                    const Value *RHS,
                                                    const SimplifyQuery &SQ);
/// Compute whether an unsigned add of \p LHS and \p RHS can overflow.
/// @param LHS Left-hand operand of the add.
/// @param RHS Right-hand operand of the add.
/// @param SQ Simplify query providing context for the analysis.
/// @return Overflow classification for the unsigned add.
LLVM_ABI OverflowResult computeOverflowForUnsignedAdd(
    const WithCache<const Value *> &LHS, const WithCache<const Value *> &RHS,
    const SimplifyQuery &SQ);
/// Compute whether a signed add of \p LHS and \p RHS can overflow.
/// @param LHS Left-hand operand of the add.
/// @param RHS Right-hand operand of the add.
/// @param SQ Simplify query providing context for the analysis.
/// @return Overflow classification for the signed add.
LLVM_ABI OverflowResult computeOverflowForSignedAdd(
    const WithCache<const Value *> &LHS, const WithCache<const Value *> &RHS,
    const SimplifyQuery &SQ);
/// This version also leverages the sign bit of Add if known.
/// @param Add Signed add operator to analyze for overflow.
/// @param SQ Simplify query providing context for the analysis.
/// @return Overflow classification for the signed add.
LLVM_ABI OverflowResult computeOverflowForSignedAdd(const AddOperator *Add,
                                                    const SimplifyQuery &SQ);
/// Compute whether an unsigned subtract of \p LHS and \p RHS can overflow.
/// @param LHS Left-hand operand of the subtract.
/// @param RHS Right-hand operand of the subtract.
/// @param SQ Simplify query providing context for the analysis.
/// @return Overflow classification for the unsigned subtract.
LLVM_ABI OverflowResult computeOverflowForUnsignedSub(const Value *LHS,
                                                      const Value *RHS,
                                                      const SimplifyQuery &SQ);
/// Compute whether a signed subtract of \p LHS and \p RHS can overflow.
/// @param LHS Left-hand operand of the subtract.
/// @param RHS Right-hand operand of the subtract.
/// @param SQ Simplify query providing context for the analysis.
/// @return Overflow classification for the signed subtract.
LLVM_ABI OverflowResult computeOverflowForSignedSub(const Value *LHS,
                                                    const Value *RHS,
                                                    const SimplifyQuery &SQ);

/// Returns true if the arithmetic part of the \p WO 's result is used only
/// along no-overflow paths.
///
/// That is, the arithmetic result is used only along the paths control
/// dependent on the computation not overflowing, \p WO being an
/// <op>.with.overflow intrinsic.
/// @param WO With-overflow intrinsic to analyze.
/// @param DT Dominator tree used to reason about control dependence.
/// @return True if the arithmetic result is only used on no-overflow paths.
LLVM_ABI bool isOverflowIntrinsicNoWrap(const WithOverflowInst *WO,
                                        const DominatorTree &DT);

/// Determine the possible constant range of vscale with the given bit width,
/// based on the vscale_range function attribute.
/// @param F Function whose vscale_range attribute is consulted.
/// @param BitWidth Bit width of the resulting constant range.
/// @return Constant range of possible vscale values.
LLVM_ABI ConstantRange getVScaleRange(const Function *F, unsigned BitWidth);

/// Determine the possible constant range of an integer or vector of integer
/// value. This is intended as a cheap, non-recursive check.
/// @param V Integer or integer-vector value to analyze.
/// @param ForSigned Whether to compute a signed constant range.
/// @param SQ Simplify query providing context for the analysis.
/// @param Depth Current recursion depth for this query.
/// @return Possible constant range of \p V.
LLVM_ABI ConstantRange computeConstantRange(const Value *V, bool ForSigned,
                                            const SimplifyQuery &SQ,
                                            unsigned Depth = 0);

/// Combine constant ranges from computeConstantRange() and computeKnownBits().
/// @param V Value whose combined constant range is computed.
/// @param ForSigned Whether to compute a signed constant range.
/// @param SQ Simplify query providing context for the analysis.
/// @return Combined constant range including known-bits information.
LLVM_ABI ConstantRange computeConstantRangeIncludingKnownBits(
    const WithCache<const Value *> &V, bool ForSigned, const SimplifyQuery &SQ);

/// Return true if instruction \p I always transfers execution to a successor.
///
/// This includes the next instruction that follows within a basic block. E.g.
/// this is not guaranteed for function calls that could loop infinitely.
///
/// In other words, this function returns false for instructions that may
/// transfer execution or fail to transfer execution in a way that is not
/// captured in the CFG nor in the sequence of instructions within a basic
/// block.
///
/// Undefined behavior is assumed not to happen, so e.g. division is
/// guaranteed to transfer execution to the following instruction even
/// though division by zero might cause undefined behavior.
/// @param I Instruction to test.
/// @return True if \p I is guaranteed to transfer execution to a successor.
LLVM_ABI bool isGuaranteedToTransferExecutionToSuccessor(const Instruction *I);

/// Returns true if this block does not contain a potential implicit exit.
///
/// This is equivelent to saying that all instructions within the basic block
/// are guaranteed to transfer execution to their successor within the basic
/// block. This has the same assumptions w.r.t. undefined behavior as the
/// instruction variant of this function.
/// @param BB Basic block to test.
/// @return True if every instruction in \p BB transfers to its successor.
LLVM_ABI bool isGuaranteedToTransferExecutionToSuccessor(const BasicBlock *BB);

/// Return true if every instruction in the range (Begin, End) transfers.
///
/// \p ScanLimit bounds the search to avoid scanning huge blocks.
/// @param Begin Start of the instruction range (exclusive).
/// @param End End of the instruction range (exclusive).
/// @param ScanLimit Maximum number of instructions to examine.
/// @return True if every instruction in the range transfers execution.
LLVM_ABI bool
isGuaranteedToTransferExecutionToSuccessor(BasicBlock::const_iterator Begin,
                                           BasicBlock::const_iterator End,
                                           unsigned ScanLimit = 32);

/// Same as previous, but with range expressed via iterator_range.
/// @param Range Instruction range to examine.
/// @param ScanLimit Maximum number of instructions to examine.
/// @return True if every instruction in \p Range transfers execution.
LLVM_ABI bool isGuaranteedToTransferExecutionToSuccessor(
    iterator_range<BasicBlock::const_iterator> Range, unsigned ScanLimit = 32);

/// Return true if this function can prove that the instruction I
/// is executed for every iteration of the loop L.
///
/// Note that this currently only considers the loop header.
/// @param I Instruction whose execution frequency in the loop is tested.
/// @param L Loop in which \p I may execute.
/// @return True if \p I executes on every iteration of \p L.
LLVM_ABI bool isGuaranteedToExecuteForEveryIteration(const Instruction *I,
                                                     const Loop *L);

/// Return true if \p PoisonOp's user yields poison or raises UB if its
/// operand \p PoisonOp is poison.
///
/// If \p PoisonOp is a vector or an aggregate and the operation's result is a
/// single value, any poison element in /p PoisonOp should make the result
/// poison or raise UB.
///
/// To filter out operands that raise UB on poison, you can use
/// getGuaranteedNonPoisonOp.
/// @param PoisonOp Operand use that may be poison.
/// @return True if poison in \p PoisonOp propagates to the user result or UB.
LLVM_ABI bool propagatesPoison(const Use &PoisonOp);

/// Return whether this intrinsic propagates poison for all operands.
/// @param IID Intrinsic identifier to classify.
/// @return True if the intrinsic propagates poison from every operand.
LLVM_ABI bool intrinsicPropagatesPoison(Intrinsic::ID IID);

/// Return true if \p I must trigger UB when any of \p KnownPoison is poison.
///
/// That is, undefined behavior occurs when I is executed with any operands
/// which appear in KnownPoison holding a poison value at the point of
/// execution.
/// @param I Instruction that may trigger UB on poison operands.
/// @param KnownPoison Values already known to be poison at \p I.
/// @return True if executing \p I must trigger undefined behavior.
LLVM_ABI bool mustTriggerUB(const Instruction *I,
                            const SmallPtrSetImpl<const Value *> &KnownPoison);

/// Return true if this function can prove that if Inst is executed
/// and yields a poison value or undef bits, then that will trigger
/// undefined behavior.
///
/// Note that this currently only considers the basic block that is
/// the parent of Inst.
/// @param Inst Instruction whose undef/poison result would trigger UB.
/// @return True if undef or poison from \p Inst implies program UB.
LLVM_ABI bool programUndefinedIfUndefOrPoison(const Instruction *Inst);
/// Return true if a poison result from \p Inst implies program undefined
/// behavior.
/// @param Inst Instruction whose poison result would trigger UB.
/// @return True if poison from \p Inst implies program UB.
LLVM_ABI bool programUndefinedIfPoison(const Instruction *Inst);

/// Return true if \p Op can create undef or poison from clean operands.
///
/// For vectors, canCreateUndefOrPoison returns true if there is potential
/// poison or undef in any element of the result when vectors without
/// undef/poison poison are given as operands.
/// For example, given `Op = shl <2 x i32> %x, <0, 32>`, this function returns
/// true. If Op raises immediate UB but never creates poison or undef
/// (e.g. sdiv I, 0), canCreatePoison returns false.
///
/// \p ConsiderFlagsAndMetadata controls whether poison producing flags and
/// metadata on the instruction are considered.  This can be used to see if the
/// instruction could still introduce undef or poison even without poison
/// generating flags and metadata which might be on the instruction.
/// (i.e. could the result of Op->dropPoisonGeneratingFlags() still create
/// poison or undef)
///
/// canCreatePoison returns true if Op can create poison from non-poison
/// operands.
/// @param Op Operator that may introduce undef or poison.
/// @param ConsiderFlagsAndMetadata Whether poison-generating flags/metadata
/// count.
/// @return True if \p Op can create undef or poison.
LLVM_ABI bool canCreateUndefOrPoison(const Operator *Op,
                                     bool ConsiderFlagsAndMetadata = true);
/// Return true if \p Op can create poison from non-poison operands.
/// @param Op Operator that may introduce poison.
/// @param ConsiderFlagsAndMetadata Whether poison-generating flags/metadata
/// count.
/// @return True if \p Op can create poison.
LLVM_ABI bool canCreatePoison(const Operator *Op,
                              bool ConsiderFlagsAndMetadata = true);

/// Return true if V is poison given that ValAssumedPoison is already poison.
///
/// For example, if ValAssumedPoison is `icmp X, 10` and V is `icmp X, 5`,
/// impliesPoison returns true.
/// @param ValAssumedPoison Value already assumed to be poison.
/// @param V Value that may consequently be poison.
/// @return True if poison in \p ValAssumedPoison implies poison in \p V.
LLVM_ABI bool impliesPoison(const Value *ValAssumedPoison, const Value *V);

/// Return true if this function can prove that V does not have undef bits
/// and is never poison.
///
/// If V is an aggregate value or vector, check whether all elements (except
/// padding) are not undef or poison. Note that this is different from
/// canCreateUndefOrPoison because the function assumes Op's operands are not
/// poison/undef.
///
/// If CtxI and DT are specified this method performs flow-sensitive analysis
/// and returns true if it is guaranteed to be never undef or poison
/// immediately before the CtxI.
/// @param V Value to test for freedom from undef and poison.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param CtxI Optional context instruction for flow-sensitive analysis.
/// @param DT Optional dominator tree for flow-sensitive analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is guaranteed not undef or poison.
LLVM_ABI bool
isGuaranteedNotToBeUndefOrPoison(const Value *V, AssumptionCache *AC = nullptr,
                                 const Instruction *CtxI = nullptr,
                                 const DominatorTree *DT = nullptr,
                                 unsigned Depth = 0);

/// Returns true if V cannot be poison, but may be undef.
/// @param V Value to test for freedom from poison.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param CtxI Optional context instruction for flow-sensitive analysis.
/// @param DT Optional dominator tree for flow-sensitive analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is guaranteed not poison.
LLVM_ABI bool isGuaranteedNotToBePoison(const Value *V,
                                        AssumptionCache *AC = nullptr,
                                        const Instruction *CtxI = nullptr,
                                        const DominatorTree *DT = nullptr,
                                        unsigned Depth = 0);

/// Iterator-context overload of isGuaranteedNotToBePoison.
/// @param V Value to test for freedom from poison.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param CtxI Context position expressed as a basic-block iterator.
/// @param DT Optional dominator tree for flow-sensitive analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is guaranteed not poison.
inline bool isGuaranteedNotToBePoison(const Value *V, AssumptionCache *AC,
                                      BasicBlock::iterator CtxI,
                                      const DominatorTree *DT = nullptr,
                                      unsigned Depth = 0) {
  // Takes an iterator as a position, passes down to Instruction *
  // implementation.
  return isGuaranteedNotToBePoison(V, AC, &*CtxI, DT, Depth);
}

/// Returns true if V cannot be undef, but may be poison.
/// @param V Value to test for freedom from undef.
/// @param AC Optional assumption cache for context-sensitive facts.
/// @param CtxI Optional context instruction for flow-sensitive analysis.
/// @param DT Optional dominator tree for flow-sensitive analysis.
/// @param Depth Current recursion depth for this query.
/// @return True if \p V is guaranteed not undef.
LLVM_ABI bool isGuaranteedNotToBeUndef(const Value *V,
                                       AssumptionCache *AC = nullptr,
                                       const Instruction *CtxI = nullptr,
                                       const DominatorTree *DT = nullptr,
                                       unsigned Depth = 0);

/// Return true if poison from Root would force UB on the path to OnPathTo.
///
/// Note that this doesn't say anything about whether OnPathTo is actually
/// executed or whether Root is actually poison. This can be used to assess
/// whether a new use of Root can be added at a location which is control
/// equivalent with OnPathTo (such as immediately before it) without introducing
/// UB which didn't previously exist. Note that a false result conveys no
/// information.
/// @param Root Instruction that may produce poison.
/// @param OnPathTo Instruction that must be reached with UB if Root is poison.
/// @param DT Dominator tree used to reason about the path.
/// @return True if poison from \p Root implies UB before \p OnPathTo.
LLVM_ABI bool mustExecuteUBIfPoisonOnPathTo(Instruction *Root,
                                            Instruction *OnPathTo,
                                            DominatorTree *DT);

/// Flip the strictness of an integer compare against a constant RHS.
///
/// Convert an integer comparison with a constant RHS into an equivalent form
/// with the strictness flipped predicate. Return the new predicate and
/// corresponding constant RHS if possible. Otherwise return std::nullopt.
/// E.g., (icmp sgt X, 0) -> (icmp sle X, 1).
/// For a samesign predicate, fail if adjusting the constant would change its
/// sign bit, because that would change the comparison's poison domain.
/// @param Pred Comparison predicate to flip.
/// @param C Constant right-hand side of the comparison.
/// @return Flipped predicate and adjusted constant, or nullopt on failure.
LLVM_ABI std::optional<std::pair<CmpPredicate, Constant *>>
getFlippedStrictnessPredicateAndConstant(CmpPredicate Pred, Constant *C);

/// Specific patterns of select instructions we can match.
enum SelectPatternFlavor {
  /// Unknown or unmatched select pattern.
  SPF_UNKNOWN = 0,
  /// Signed minimum
  SPF_SMIN,
  /// Unsigned minimum
  SPF_UMIN,
  /// Signed maximum
  SPF_SMAX,
  /// Unsigned maximum
  SPF_UMAX,
  /// Floating point minnum
  SPF_FMINNUM,
  /// Floating point maxnum
  SPF_FMAXNUM,
  /// Absolute value
  SPF_ABS,
  /// Negated absolute value
  SPF_NABS
};

/// Behavior when a floating point min/max is given one NaN and one
/// non-NaN as input.
enum SelectPatternNaNBehavior {
  /// NaN behavior not applicable.
  SPNB_NA = 0,
  /// Given one NaN input, returns the NaN.
  SPNB_RETURNS_NAN,
  /// Given one NaN input, returns the non-NaN.
  SPNB_RETURNS_OTHER,
  /// Given one NaN input, can return either (or it has been determined that no
  /// operands can be NaN).
  SPNB_RETURNS_ANY
};

/// Result of matching a select as a min/max/abs idiom.
struct SelectPatternResult {
  /// Matched select pattern flavor.
  SelectPatternFlavor Flavor;
  /// NaN behavior when Flavor is SPF_FMINNUM or SPF_FMAXNUM.
  SelectPatternNaNBehavior NaNBehavior;
  /// When implementing this min/max pattern as fcmp; select, whether the fcmp
  /// must be ordered.
  bool Ordered;

  /// Return true if \p SPF is a min or a max pattern.
  /// @param SPF Select pattern flavor to classify.
  /// @return True if \p SPF is a min or max flavor.
  static bool isMinOrMax(SelectPatternFlavor SPF) {
    return SPF != SPF_UNKNOWN && SPF != SPF_ABS && SPF != SPF_NABS;
  }
};

/// Pattern match integer [SU]MIN, [SU]MAX and ABS idioms, returning the kind
/// and providing the out parameter results if we successfully match.
///
/// For ABS/NABS, LHS will be set to the input to the abs idiom. RHS will be
/// the negation instruction from the idiom.
///
/// If CastOp is not nullptr, also match MIN/MAX idioms where the type does
/// not match that of the original select. If this is the case, the cast
/// operation (one of Trunc,SExt,Zext) that must be done to transform the
/// type of LHS and RHS into the type of V is returned in CastOp.
///
/// For example:
///   %1 = icmp slt i32 %a, i32 4
///   %2 = sext i32 %a to i64
///   %3 = select i1 %1, i64 %2, i64 4
///
/// -> LHS = %a, RHS = i32 4, *CastOp = Instruction::SExt
///
/// @param V Select (or equivalent) value to match.
/// @param LHS Output left-hand operand of the matched idiom.
/// @param RHS Output right-hand operand of the matched idiom.
/// @param CastOp Optional output cast needed to unify operand types.
/// @param Depth Current recursion depth for this query.
/// @return Matched select pattern result.
LLVM_ABI SelectPatternResult
matchSelectPattern(Value *V, Value *&LHS, Value *&RHS,
                   Instruction::CastOps *CastOp = nullptr, unsigned Depth = 0);

/// Const overload of matchSelectPattern.
/// @param V Select (or equivalent) value to match.
/// @param LHS Output left-hand operand of the matched idiom.
/// @param RHS Output right-hand operand of the matched idiom.
/// @return Matched select pattern result.
inline SelectPatternResult matchSelectPattern(const Value *V, const Value *&LHS,
                                              const Value *&RHS) {
  Value *L = const_cast<Value *>(LHS);
  Value *R = const_cast<Value *>(RHS);
  auto Result = matchSelectPattern(const_cast<Value *>(V), L, R);
  LHS = L;
  RHS = R;
  return Result;
}

/// Determine the pattern that a select with the given compare as its
/// predicate and given values as its true/false operands would match.
/// @param CmpI Compare instruction used as the select predicate.
/// @param TrueVal Value selected when the compare is true.
/// @param FalseVal Value selected when the compare is false.
/// @param LHS Output left-hand operand of the matched idiom.
/// @param RHS Output right-hand operand of the matched idiom.
/// @param FMF Fast-math flags that apply to the select/compare.
/// @param CastOp Optional output cast needed to unify operand types.
/// @param Depth Current recursion depth for this query.
/// @return Matched select pattern result.
LLVM_ABI SelectPatternResult matchDecomposedSelectPattern(
    CmpInst *CmpI, Value *TrueVal, Value *FalseVal, Value *&LHS, Value *&RHS,
    FastMathFlags FMF = FastMathFlags(), Instruction::CastOps *CastOp = nullptr,
    unsigned Depth = 0);

/// Determine the pattern for predicate `X Pred Y ? X : Y`.
/// @param Pred Comparison predicate of the select idiom.
/// @param NaNBehavior Expected NaN behavior for floating-point min/max.
/// @param Ordered Whether an ordered compare is required.
/// @return Corresponding select pattern result.
LLVM_ABI SelectPatternResult getSelectPattern(
    CmpInst::Predicate Pred, SelectPatternNaNBehavior NaNBehavior = SPNB_NA,
    bool Ordered = false);

/// Return the canonical comparison predicate for the specified
/// minimum/maximum flavor.
/// @param SPF Min/max select pattern flavor.
/// @param Ordered Whether an ordered floating-point compare is required.
/// @return Canonical icmp/fcmp predicate for \p SPF.
LLVM_ABI CmpInst::Predicate getMinMaxPred(SelectPatternFlavor SPF,
                                          bool Ordered = false);

/// Convert given `SPF` to equivalent min/max intrinsic.
/// Caller must ensure `SPF` is an integer min or max pattern.
/// @param SPF Integer min/max select pattern flavor.
/// @return Matching min/max intrinsic ID.
LLVM_ABI Intrinsic::ID getMinMaxIntrinsic(SelectPatternFlavor SPF);

/// Return the inverse minimum/maximum flavor of the specified flavor.
/// For example, signed minimum is the inverse of signed maximum.
/// @param SPF Min/max select pattern flavor to invert.
/// @return Inverse min/max flavor of \p SPF.
LLVM_ABI SelectPatternFlavor getInverseMinMaxFlavor(SelectPatternFlavor SPF);

/// Return the inverse min/max intrinsic of \p MinMaxID.
/// @param MinMaxID Min/max intrinsic to invert.
/// @return Intrinsic ID of the inverse min/max operation.
LLVM_ABI Intrinsic::ID getInverseMinMaxIntrinsic(Intrinsic::ID MinMaxID);

/// Return the minimum or maximum constant value for the specified integer
/// min/max flavor and type.
/// @param SPF Integer min/max select pattern flavor.
/// @param BitWidth Bit width of the returned limit constant.
/// @return Minimum or maximum APInt for \p SPF and \p BitWidth.
LLVM_ABI APInt getMinMaxLimit(SelectPatternFlavor SPF, unsigned BitWidth);

/// Check if values in \p VL are selects convertible to a min/max intrinsic.
///
/// Returns the intrinsic ID, if such a conversion is possible, together with a
/// bool indicating whether all select conditions are only used by the selects.
/// Otherwise return Intrinsic::not_intrinsic.
/// @param VL Select values to consider for conversion.
/// @return Matching intrinsic ID and whether conditions are select-only.
LLVM_ABI std::pair<Intrinsic::ID, bool>
canConvertToMinOrMaxIntrinsic(ArrayRef<Value *> VL);

/// Match a simple first-order recurrence cycle from a phi node.
///
/// Attempt to match a simple first order recurrence cycle of the form:
///   %iv = phi Ty [%Start, %Entry], [%Inc, %backedge]
///   %inc = binop %iv, %step
/// OR
///   %iv = phi Ty [%Start, %Entry], [%Inc, %backedge]
///   %inc = binop %step, %iv
///
/// A first order recurrence is a formula with the form: X_n = f(X_(n-1))
///
/// A couple of notes on subtleties in that definition:
/// * The Step does not have to be loop invariant.  In math terms, it can
///   be a free variable.  We allow recurrences with both constant and
///   variable coefficients. Callers may wish to filter cases where Step
///   does not dominate P.
/// * For non-commutative operators, we will match both forms.  This
///   results in some odd recurrence structures.  Callers may wish to filter
///   out recurrences where the phi is not the LHS of the returned operator.
/// * Because of the structure matched, the caller can assume as a post
///   condition of the match the presence of a Loop with P's parent as it's
///   header *except* in unreachable code.  (Dominance decays in unreachable
///   code.)
///
/// NOTE: This is intentional simple.  If you want the ability to analyze
/// non-trivial loop conditons, see ScalarEvolution instead.
/// @param P Phi node that may be the recurrence induction variable.
/// @param BO Output binary operator of the recurrence step.
/// @param Start Output starting value of the recurrence.
/// @param Step Output step operand of the recurrence.
/// @return True if a simple recurrence was matched.
LLVM_ABI bool matchSimpleRecurrence(const PHINode *P, BinaryOperator *&BO,
                                    Value *&Start, Value *&Step);

/// Analogous to the above, but starting from the binary operator
/// @param I Binary operator that may be the recurrence step.
/// @param P Output phi node of the matched recurrence.
/// @param Start Output starting value of the recurrence.
/// @param Step Output step operand of the recurrence.
/// @return True if a simple recurrence was matched.
LLVM_ABI bool matchSimpleRecurrence(const BinaryOperator *I, PHINode *&P,
                                    Value *&Start, Value *&Step);

/// Match a simple binary-intrinsic value-accumulating recurrence.
///
/// Attempt to match a simple value-accumulating recurrence of the form:
///   %llvm.intrinsic.acc = phi Ty [%Init, %Entry], [%llvm.intrinsic, %backedge]
///   %llvm.intrinsic = call Ty @llvm.intrinsic(%OtherOp, %llvm.intrinsic.acc)
/// OR
///   %llvm.intrinsic.acc = phi Ty [%Init, %Entry], [%llvm.intrinsic, %backedge]
///   %llvm.intrinsic = call Ty @llvm.intrinsic(%llvm.intrinsic.acc, %OtherOp)
///
/// The recurrence relation is of kind:
///   X_0 = %a (initial value),
///   X_i = call @llvm.binary.intrinsic(X_i-1, %b)
/// Where %b is not required to be loop-invariant.
/// @param I Intrinsic call that may be the recurrence update.
/// @param P Output phi accumulator of the matched recurrence.
/// @param Init Output initial value of the accumulator.
/// @param OtherOp Output non-accumulator operand of the intrinsic.
/// @return True if a simple binary intrinsic recurrence was matched.
LLVM_ABI bool matchSimpleBinaryIntrinsicRecurrence(const IntrinsicInst *I,
                                                   PHINode *&P, Value *&Init,
                                                   Value *&OtherOp);

/// Match a simple ternary-intrinsic value-accumulating recurrence.
///
/// Attempt to match a simple value-accumulating recurrence of the form:
///   %llvm.intrinsic.acc = phi Ty [%Init, %Entry], [%llvm.intrinsic, %backedge]
///   %llvm.intrinsic = call Ty @llvm.intrinsic(%OtherOp0, %OtherOp1,
///   %llvm.intrinsic.acc)
/// OR
///   %llvm.intrinsic.acc = phi Ty [%Init, %Entry], [%llvm.intrinsic, %backedge]
///   %llvm.intrinsic = call Ty @llvm.intrinsic(%llvm.intrinsic.acc, %OtherOp0,
///   %OtherOp1)
///
/// The recurrence relation is of kind:
///   X_0 = %a (initial value),
///   X_i = call @llvm.ternary.intrinsic(X_i-1, %b, %c)
/// Where %b, %c are not required to be loop-invariant.
/// @param I Intrinsic call that may be the recurrence update.
/// @param P Output phi accumulator of the matched recurrence.
/// @param Init Output initial value of the accumulator.
/// @param OtherOp0 Output first non-accumulator operand of the intrinsic.
/// @param OtherOp1 Output second non-accumulator operand of the intrinsic.
/// @return True if a simple ternary intrinsic recurrence was matched.
LLVM_ABI bool matchSimpleTernaryIntrinsicRecurrence(const IntrinsicInst *I,
                                                    PHINode *&P, Value *&Init,
                                                    Value *&OtherOp0,
                                                    Value *&OtherOp1);

/// Return whether RHS is implied true or false by LHS, if known.
///
/// Return true if RHS is known to be implied true by LHS.  Return false if
/// RHS is known to be implied false by LHS.  Otherwise, return std::nullopt if
/// no implication can be made. A & B must be i1 (boolean) values or a vector of
/// such values. Note that the truth table for implication is the same as <=u on
/// i1 values (but not
/// <=s!).  The truth table for both is:
///    | T | F (B)
///  T | T | F
///  F | T | T
/// (A)
/// @param LHS Condition assumed true or false according to \p LHSIsTrue.
/// @param RHS Condition whose implication from \p LHS is tested.
/// @param DL Data layout used by the implication analysis.
/// @param LHSIsTrue Whether \p LHS is assumed true (vs false).
/// @param Depth Current recursion depth for this query.
/// @return True/false if implied, or nullopt if unknown.
LLVM_ABI std::optional<bool>
isImpliedCondition(const Value *LHS, const Value *RHS, const DataLayout &DL,
                   bool LHSIsTrue = true, unsigned Depth = 0);
/// Return whether a compare is implied true or false by \p LHS, if known.
/// @param LHS Condition assumed true or false according to \p LHSIsTrue.
/// @param RHSPred Predicate of the right-hand compare.
/// @param RHSOp0 Left operand of the right-hand compare.
/// @param RHSOp1 Right operand of the right-hand compare.
/// @param DL Data layout used by the implication analysis.
/// @param LHSIsTrue Whether \p LHS is assumed true (vs false).
/// @param Depth Current recursion depth for this query.
/// @return True/false if implied, or nullopt if unknown.
LLVM_ABI std::optional<bool>
isImpliedCondition(const Value *LHS, CmpPredicate RHSPred, const Value *RHSOp0,
                   const Value *RHSOp1, const DataLayout &DL,
                   bool LHSIsTrue = true, unsigned Depth = 0);

/// Return the boolean condition value in the context of the given instruction
/// if it is known based on dominating conditions.
/// @param Cond Boolean condition to evaluate in context.
/// @param ContextI Instruction providing the dominating-condition context.
/// @param DL Data layout used by the implication analysis.
/// @return Known boolean value of \p Cond, or nullopt if unknown.
LLVM_ABI std::optional<bool>
isImpliedByDomCondition(const Value *Cond, const Instruction *ContextI,
                        const DataLayout &DL);
/// Return whether a compare is known from dominating conditions at ContextI.
/// @param Pred Predicate of the compare to evaluate.
/// @param LHS Left-hand operand of the compare.
/// @param RHS Right-hand operand of the compare.
/// @param ContextI Instruction providing the dominating-condition context.
/// @param DL Data layout used by the implication analysis.
/// @return Known boolean value of the compare, or nullopt if unknown.
LLVM_ABI std::optional<bool>
isImpliedByDomCondition(CmpPredicate Pred, const Value *LHS, const Value *RHS,
                        const Instruction *ContextI, const DataLayout &DL);

/// Call \p InsertAffected on all Values whose known bits / value may be
/// affected by the condition \p Cond. Used by AssumptionCache and
/// DomConditionCache.
/// @param Cond Condition whose affected values are collected.
/// @param IsAssume Whether \p Cond comes from an assume intrinsic.
/// @param InsertAffected Callback invoked for each affected value.
LLVM_ABI void
findValuesAffectedByCondition(Value *Cond, bool IsAssume,
                              function_ref<void(Value *)> InsertAffected);

/// Returns the inner value X if the expression has the form f(X)
/// where f(X) == 0 if and only if X == 0, otherwise returns nullptr.
/// @param V Expression that may be a null-preserving wrapper around X.
/// @return Inner value X, or null if the form does not match.
LLVM_ABI Value *stripNullTest(Value *V);
/// Const overload of stripNullTest.
/// @param V Expression that may be a null-preserving wrapper around X.
/// @return Inner value X, or null if the form does not match.
LLVM_ABI const Value *stripNullTest(const Value *V);

/// Enumerate immediate constant values of \p V into \p Constants.
///
/// If \p AllowUndefOrPoison is false, it fails when V may contain undef/poison
/// elements. Returns true if the result is complete. Otherwise, the result is
/// incomplete (more than MaxCount values).
/// NOTE: The constant values are not distinct.
/// @param V Value whose possible immediate constants are collected.
/// @param Constants Output set of possible constant values.
/// @param MaxCount Maximum number of constants to collect.
/// @param AllowUndefOrPoison Whether undef/poison elements are allowed.
/// @return True if enumeration completed within \p MaxCount.
LLVM_ABI bool
collectPossibleValues(const Value *V,
                      SmallPtrSetImpl<const Constant *> &Constants,
                      unsigned MaxCount, bool AllowUndefOrPoison = true);

} // end namespace llvm

#endif // LLVM_ANALYSIS_VALUETRACKING_H
