//===-------- BlockFrequency.h - Block Frequency Wrapper --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Block Frequency class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BLOCKFREQUENCY_H
#define LLVM_SUPPORT_BLOCKFREQUENCY_H

#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <optional>

namespace llvm {

class raw_ostream;
class BranchProbability;

/// Relative execution frequency of a basic block, stored as a 64-bit
/// fixed-point value scaled by the function entry frequency.
class BlockFrequency {
  uint64_t Frequency;

public:
  /// Construct a zero block frequency.
  BlockFrequency() : Frequency(0) {}
  /// Construct a block frequency from the raw fixed-point value \p Freq.
  ///
  /// \param Freq Raw fixed-point frequency scaled by the entry frequency.
  explicit BlockFrequency(uint64_t Freq) : Frequency(Freq) {}

  /// Returns the maximum possible frequency, the saturation value.
  ///
  /// \return A block frequency saturated at \c UINT64_MAX.
  static BlockFrequency max() { return BlockFrequency(UINT64_MAX); }

  /// Returns the frequency as a fixpoint number scaled by the entry
  /// frequency.
  ///
  /// \return The raw fixed-point frequency value.
  uint64_t getFrequency() const { return Frequency; }

  /// Multiplies with a branch probability. The computation will never
  /// overflow.
  ///
  /// \param Prob Branch probability to scale this frequency by.
  /// \return This frequency after scaling by \p Prob.
  LLVM_ABI BlockFrequency &operator*=(BranchProbability Prob);
  /// Return this block frequency scaled by \p Prob without overflow.
  ///
  /// \param Prob Branch probability to scale this frequency by.
  /// \return The scaled frequency.
  LLVM_ABI BlockFrequency operator*(BranchProbability Prob) const;

  /// Divide by a non-zero branch probability using saturating
  /// arithmetic.
  ///
  /// \param Prob Non-zero branch probability to divide by.
  /// \return This frequency after saturating division by \p Prob.
  LLVM_ABI BlockFrequency &operator/=(BranchProbability Prob);
  /// Return this block frequency divided by \p Prob using saturating arithmetic.
  ///
  /// \param Prob Non-zero branch probability to divide by.
  /// \return The saturating quotient.
  LLVM_ABI BlockFrequency operator/(BranchProbability Prob) const;

  /// Adds another block frequency using saturating arithmetic.
  ///
  /// \param Freq Frequency to add; overflow saturates to the maximum value.
  /// \return This frequency after saturating addition.
  BlockFrequency &operator+=(BlockFrequency Freq) {
    uint64_t Before = Freq.Frequency;
    Frequency += Freq.Frequency;

    // If overflow, set frequency to the maximum value.
    if (Frequency < Before)
      Frequency = UINT64_MAX;

    return *this;
  }
  /// Return the saturating sum of this frequency and \p Freq.
  ///
  /// \param Freq Frequency to add.
  /// \return The saturating sum.
  BlockFrequency operator+(BlockFrequency Freq) const {
    BlockFrequency NewFreq(Frequency);
    NewFreq += Freq;
    return NewFreq;
  }

  /// Subtracts another block frequency using saturating arithmetic.
  ///
  /// \param Freq Frequency to subtract; underflow saturates to zero.
  /// \return This frequency after saturating subtraction.
  BlockFrequency &operator-=(BlockFrequency Freq) {
    // If underflow, set frequency to 0.
    if (Frequency <= Freq.Frequency)
      Frequency = 0;
    else
      Frequency -= Freq.Frequency;
    return *this;
  }
  /// Return the saturating difference of this frequency and \p Freq.
  ///
  /// \param Freq Frequency to subtract.
  /// \return The saturating difference.
  BlockFrequency operator-(BlockFrequency Freq) const {
    BlockFrequency NewFreq(Frequency);
    NewFreq -= Freq;
    return NewFreq;
  }

  /// Multiplies frequency with `Factor`. Returns `nullopt` in case of overflow.
  ///
  /// \param Factor Integer scale factor to multiply by.
  /// \return The product, or \c nullopt if multiplication would overflow.
  LLVM_ABI std::optional<BlockFrequency> mul(uint64_t Factor) const;

  /// Shift block frequency to the right by count digits saturating to 1.
  ///
  /// \param count Number of bits to shift right.
  /// \return This frequency after the saturating right shift.
  BlockFrequency &operator>>=(const unsigned count) {
    // Frequency can never be 0 by design.
    assert(Frequency != 0);

    // Shift right by count.
    Frequency >>= count;

    // Saturate to 1 if we are 0.
    Frequency |= Frequency == 0;
    return *this;
  }

  /// Return true if this frequency is strictly less than \p RHS.
  ///
  /// \param RHS Frequency to compare against.
  /// \return True if this frequency is strictly less than \p RHS.
  bool operator<(BlockFrequency RHS) const {
    return Frequency < RHS.Frequency;
  }

  /// Return true if this frequency is less than or equal to \p RHS.
  ///
  /// \param RHS Frequency to compare against.
  /// \return True if this frequency is less than or equal to \p RHS.
  bool operator<=(BlockFrequency RHS) const {
    return Frequency <= RHS.Frequency;
  }

  /// Return true if this frequency is strictly greater than \p RHS.
  ///
  /// \param RHS Frequency to compare against.
  /// \return True if this frequency is strictly greater than \p RHS.
  bool operator>(BlockFrequency RHS) const {
    return Frequency > RHS.Frequency;
  }

  /// Return true if this frequency is greater than or equal to \p RHS.
  ///
  /// \param RHS Frequency to compare against.
  /// \return True if this frequency is greater than or equal to \p RHS.
  bool operator>=(BlockFrequency RHS) const {
    return Frequency >= RHS.Frequency;
  }

  /// Return true if this frequency equals \p RHS.
  ///
  /// \param RHS Frequency to compare against.
  /// \return True if the frequencies are equal.
  bool operator==(BlockFrequency RHS) const {
    return Frequency == RHS.Frequency;
  }

  /// Return true if this frequency differs from \p RHS.
  ///
  /// \param RHS Frequency to compare against.
  /// \return True if the frequencies differ.
  bool operator!=(BlockFrequency RHS) const {
    return Frequency != RHS.Frequency;
  }
};

/// Write the raw fixed-point frequency value of \p Freq to \p OS.
///
/// \param OS Stream to write to.
/// \param Freq Block frequency to print.
/// \return \p OS after writing.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, BlockFrequency Freq);

/// Print \p Freq as a multiple of the entry frequency \p EntryFreq.
///
/// \param OS Stream to write to.
/// \param EntryFreq Function entry frequency used as the relative baseline.
/// \param Freq Block frequency to print relative to \p EntryFreq.
LLVM_ABI void printRelativeBlockFreq(raw_ostream &OS, BlockFrequency EntryFreq,
                                     BlockFrequency Freq);

} // namespace llvm

#endif
