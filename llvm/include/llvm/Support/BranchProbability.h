//===- BranchProbability.h - Branch Probability Wrapper ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definition of BranchProbability shared by IR and Machine Instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BRANCHPROBABILITY_H
#define LLVM_SUPPORT_BRANCHPROBABILITY_H

#include "llvm/ADT/ADL.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <numeric>

namespace llvm {

class raw_ostream;

/// Branch probability as a non-negative fraction no greater than one.
///
/// Uses a fixed-point-like representation whose denominator is always the
/// constant value \c 1<<31 for maximum precision.
class BranchProbability {
  // Numerator
  uint32_t N;

  // Denominator, which is a constant value.
  static constexpr uint32_t D = 1u << 31;
  static constexpr uint32_t UnknownN = UINT32_MAX;

  // Construct a BranchProbability with only numerator assuming the denominator
  // is 1<<31. For internal use only.
  explicit constexpr BranchProbability(uint32_t n) : N(n) {}

public:
  /// Construct an unknown branch probability.
  constexpr BranchProbability() : N(UnknownN) {}
  /// Construct a branch probability from \p Numerator / \p Denominator.
  ///
  /// \param Numerator Non-negative numerator; must not exceed \p Denominator.
  /// \param Denominator Positive denominator of the probability fraction.
  constexpr BranchProbability(uint32_t Numerator, uint32_t Denominator)
      : N(Numerator) {
    assert(Denominator > 0 && "Denominator cannot be 0!");
    assert(Numerator <= Denominator && "Probability cannot be bigger than 1!");
    if (Denominator != D) {
      uint64_t Prob64 =
          (Numerator * static_cast<uint64_t>(D) + Denominator / 2) /
          Denominator;
      N = static_cast<uint32_t>(Prob64);
    }
  }

  /// Return true if this probability is zero (never taken).
  ///
  /// \return True if this probability is zero.
  bool isZero() const { return N == 0; }
  /// Return true if this probability is one (always taken).
  ///
  /// \return True if this probability is one.
  bool isOne() const { return N == D; }
  /// Return true if this probability is unknown.
  ///
  /// \return True if this probability is unknown.
  bool isUnknown() const { return N == UnknownN; }

  /// Return a probability representing zero (never taken).
  ///
  /// \return A probability of zero.
  static constexpr BranchProbability getZero() { return BranchProbability(0); }
  /// Return a probability representing one (always taken).
  ///
  /// \return A probability of one.
  static constexpr BranchProbability getOne() { return BranchProbability(D); }
  /// Return an unknown branch probability.
  ///
  /// \return An unknown branch probability.
  static constexpr BranchProbability getUnknown() {
    return BranchProbability(UnknownN);
  }
  /// Create a probability from the raw fixed-point numerator \p N.
  ///
  /// The denominator is the constant \c 1<<31.
  ///
  /// \param N Raw numerator in the fixed-point representation.
  /// \return A probability with the given raw numerator.
  static constexpr BranchProbability getRaw(uint32_t N) {
    return BranchProbability(N);
  }
  /// Create a probability from 64-bit \p Numerator and \p Denominator.
  ///
  /// \param Numerator Non-negative numerator of the probability fraction.
  /// \param Denominator Positive denominator of the probability fraction.
  /// \return The corresponding branch probability.
  LLVM_ABI static BranchProbability getBranchProbability(uint64_t Numerator,
                                                         uint64_t Denominator);
  /// Create a probability from a double in the closed range [0, 1].
  ///
  /// \param Prob Probability as a floating-point value from 0 to 1 inclusive.
  /// \return The corresponding branch probability.
  LLVM_ABI static BranchProbability getBranchProbability(double Prob);

  /// Normalize probabilities in [\p Begin, \p End) so they sum to about one.
  ///
  /// \param Begin Iterator to the first probability to normalize.
  /// \param End Iterator one past the last probability to normalize.
  template <class ProbabilityIter>
  static void normalizeProbabilities(ProbabilityIter Begin,
                                     ProbabilityIter End);

  /// Normalize probabilities in \p R so they sum to about one.
  ///
  /// Unknown entries get defaults; totals beyond the representable range are
  /// rebalanced.
  ///
  /// \param R Container of probabilities to normalize in place.
  template <class ProbabilityContainer>
  static void normalizeProbabilities(ProbabilityContainer &&R) {
    normalizeProbabilities(adl_begin(R), adl_end(R));
  }

  /// Return the fixed-point numerator of this probability.
  ///
  /// \return The fixed-point numerator.
  uint32_t getNumerator() const { return N; }
  /// Return the fixed denominator used by all branch probabilities (\c 1 << 31).
  ///
  /// \return The fixed denominator (\c 1 << 31).
  static uint32_t getDenominator() { return D; }
  /// Return this probability as a floating-point value in [0, 1].
  ///
  /// \return This probability as a value in [0, 1].
  double toDouble() const { return static_cast<double>(N) / D; }

  /// Return the complementary probability (1 minus this probability).
  ///
  /// \return The complementary probability.
  BranchProbability getCompl() const { return BranchProbability(D - N); }

  /// Print this probability to \p OS.
  ///
  /// \param OS Stream to write to.
  /// \return \p OS after writing.
  LLVM_ABI raw_ostream &print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this probability to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif

  /// Scale a large integer.
  ///
  /// Scales \c Num.  Guarantees full precision.  Returns the floor of the
  /// result.
  ///
  /// \param Num Integer value to scale by this probability.
  /// \return \c Num times \c this.
  LLVM_ABI uint64_t scale(uint64_t Num) const;

  /// Scale a large integer by the inverse.
  ///
  /// Scales \c Num by the inverse of \c this.  Guarantees full precision.
  /// Returns the floor of the result.
  ///
  /// \param Num Integer value to scale by the inverse of this probability.
  /// \return \c Num divided by \c this.
  LLVM_ABI uint64_t scaleByInverse(uint64_t Num) const;

  /// Compute pow(Probability, N).
  ///
  /// \param N Non-negative exponent.
  /// \return This probability raised to the power \p N.
  LLVM_ABI BranchProbability pow(unsigned N) const;

  /// Add \p RHS to this probability, saturating at one.
  ///
  /// \param RHS Probability to add.
  /// \return A reference to this probability.
  BranchProbability &operator+=(BranchProbability RHS) {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in arithmetics.");
    // Saturate the result in case of overflow.
    N = (uint64_t(N) + RHS.N > D) ? D : N + RHS.N;
    return *this;
  }

  /// Subtract \p RHS from this probability, saturating at zero.
  ///
  /// \param RHS Probability to subtract.
  /// \return A reference to this probability.
  BranchProbability &operator-=(BranchProbability RHS) {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in arithmetics.");
    // Saturate the result in case of underflow.
    N = N < RHS.N ? 0 : N - RHS.N;
    return *this;
  }

  /// Multiply this probability by \p RHS.
  ///
  /// \param RHS Probability to multiply by.
  /// \return A reference to this probability.
  BranchProbability &operator*=(BranchProbability RHS) {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in arithmetics.");
    N = (static_cast<uint64_t>(N) * RHS.N + D / 2) / D;
    return *this;
  }

  /// Multiply this probability by integer \p RHS, saturating at certainty.
  ///
  /// \param RHS Integer scale factor to multiply by.
  /// \return A reference to this probability.
  BranchProbability &operator*=(uint32_t RHS) {
    assert(N != UnknownN &&
           "Unknown probability cannot participate in arithmetics.");
    N = (uint64_t(N) * RHS > D) ? D : N * RHS;
    return *this;
  }

  /// Divide this probability by \p RHS.
  ///
  /// \param RHS Probability to divide by.
  /// \return A reference to this probability.
  BranchProbability &operator/=(BranchProbability RHS) {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in arithmetics.");
    N = (static_cast<uint64_t>(N) * D + RHS.N / 2) / RHS.N;
    return *this;
  }

  /// Divide the probability numerator by \p RHS, leaving the denominator fixed.
  ///
  /// \param RHS Positive integer divisor.
  /// \return A reference to this probability.
  BranchProbability &operator/=(uint32_t RHS) {
    assert(N != UnknownN &&
           "Unknown probability cannot participate in arithmetics.");
    assert(RHS > 0 && "The divider cannot be zero.");
    N /= RHS;
    return *this;
  }

  /// Add two branch probabilities, saturating at one.
  ///
  /// \param RHS Probability to add.
  /// \return The sum of this probability and \p RHS.
  BranchProbability operator+(BranchProbability RHS) const {
    BranchProbability Prob(*this);
    Prob += RHS;
    return Prob;
  }

  /// Subtract \p RHS from this probability, saturating at zero.
  ///
  /// \param RHS Probability to subtract.
  /// \return The difference of this probability and \p RHS.
  BranchProbability operator-(BranchProbability RHS) const {
    BranchProbability Prob(*this);
    Prob -= RHS;
    return Prob;
  }

  /// Multiply this probability by \p RHS.
  ///
  /// \param RHS Probability to multiply by.
  /// \return The product of this probability and \p RHS.
  BranchProbability operator*(BranchProbability RHS) const {
    BranchProbability Prob(*this);
    Prob *= RHS;
    return Prob;
  }

  /// Multiply this probability by the integer scale \p RHS.
  ///
  /// \param RHS Integer scale factor to multiply by.
  /// \return This probability scaled by \p RHS.
  BranchProbability operator*(uint32_t RHS) const {
    BranchProbability Prob(*this);
    Prob *= RHS;
    return Prob;
  }

  /// Divide this probability by \p RHS.
  ///
  /// \param RHS Probability to divide by.
  /// \return The quotient of this probability and \p RHS.
  BranchProbability operator/(BranchProbability RHS) const {
    BranchProbability Prob(*this);
    Prob /= RHS;
    return Prob;
  }

  /// Divide this probability by the integer scale \p RHS.
  ///
  /// \param RHS Positive integer divisor.
  /// \return This probability divided by \p RHS.
  BranchProbability operator/(uint32_t RHS) const {
    BranchProbability Prob(*this);
    Prob /= RHS;
    return Prob;
  }

  /// Return true if this probability equals \p RHS.
  ///
  /// \param RHS Probability to compare against.
  /// \return True if the probabilities are equal.
  bool operator==(BranchProbability RHS) const { return N == RHS.N; }
  /// Return true if this probability differs from \p RHS.
  ///
  /// \param RHS Probability to compare against.
  /// \return True if the probabilities differ.
  bool operator!=(BranchProbability RHS) const { return !(*this == RHS); }

  /// Return true if this probability is strictly less than \p RHS.
  ///
  /// \param RHS Probability to compare against.
  /// \return True if this probability is strictly less than \p RHS.
  bool operator<(BranchProbability RHS) const {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in comparisons.");
    return N < RHS.N;
  }

  /// Return true if this probability is greater than \p RHS.
  ///
  /// \param RHS Probability to compare against.
  /// \return True if this probability is greater than \p RHS.
  bool operator>(BranchProbability RHS) const {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in comparisons.");
    return RHS < *this;
  }

  /// Return true if this probability is less than or equal to \p RHS.
  ///
  /// \param RHS Probability to compare against.
  /// \return True if this probability is less than or equal to \p RHS.
  bool operator<=(BranchProbability RHS) const {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in comparisons.");
    return !(RHS < *this);
  }

  /// Return true if this probability is greater than or equal to \p RHS.
  ///
  /// \param RHS Probability to compare against.
  /// \return True if this probability is greater than or equal to \p RHS.
  bool operator>=(BranchProbability RHS) const {
    assert(N != UnknownN && RHS.N != UnknownN &&
           "Unknown probability cannot participate in comparisons.");
    return !(*this < RHS);
  }
};

/// Write \p Prob to \p OS.
///
/// \param OS Stream to write to.
/// \param Prob Branch probability to print.
/// \return \p OS after writing.
inline raw_ostream &operator<<(raw_ostream &OS, BranchProbability Prob) {
  return Prob.print(OS);
}

template <class ProbabilityIter>
void BranchProbability::normalizeProbabilities(ProbabilityIter Begin,
                                               ProbabilityIter End) {
  if (Begin == End)
    return;

  unsigned UnknownProbCount = 0;
  uint64_t Sum = std::accumulate(Begin, End, uint64_t(0),
                                 [&](uint64_t S, const BranchProbability &BP) {
                                   if (!BP.isUnknown())
                                     return S + BP.N;
                                   UnknownProbCount++;
                                   return S;
                                 });

  if (UnknownProbCount > 0) {
    BranchProbability ProbForUnknown = BranchProbability::getZero();
    // If the sum of all known probabilities is less than one, evenly distribute
    // the complement of sum to unknown probabilities. Otherwise, set unknown
    // probabilities to zeros and continue to normalize known probabilities.
    if (Sum < BranchProbability::getDenominator())
      ProbForUnknown = BranchProbability::getRaw(
          (BranchProbability::getDenominator() - Sum) / UnknownProbCount);

    std::replace_if(Begin, End,
                    [](const BranchProbability &BP) { return BP.isUnknown(); },
                    ProbForUnknown);

    if (Sum <= BranchProbability::getDenominator())
      return;
  }

  if (Sum == 0) {
    BranchProbability BP(1, std::distance(Begin, End));
    std::fill(Begin, End, BP);
    return;
  }

  for (auto I = Begin; I != End; ++I)
    I->N = (I->N * uint64_t(D) + Sum / 2) / Sum;
}

}

#endif
