//===- HashRecognize.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Interface for the HashRecognize analysis, which identifies hash functions
// that can be optimized using a lookup-table or with target-specific
// instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_HASHRECOGNIZE_H
#define LLVM_ANALYSIS_HASHRECOGNIZE_H

#include "llvm/ADT/APInt.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include <variant>

namespace llvm {

class LPMUpdater;

/// A 256-entry CRC lookup table with a print helper.
///
/// Inherits the array storage privately and re-exports the public
/// \c std::array interface with documentation.
struct CRCTable : private std::array<APInt, 256> {
private:
  using Base = std::array<APInt, 256>;

public:
  /// Element type stored in the table.
  using value_type = Base::value_type;
  /// Unsigned size type for indices and lengths.
  using size_type = Base::size_type;
  /// Signed type for iterator distances.
  using difference_type = Base::difference_type;
  /// Mutable reference to an element.
  using reference = Base::reference;
  /// Immutable reference to an element.
  using const_reference = Base::const_reference;
  /// Mutable pointer to an element.
  using pointer = Base::pointer;
  /// Immutable pointer to an element.
  using const_pointer = Base::const_pointer;
  /// Mutable random-access iterator.
  using iterator = Base::iterator;
  /// Immutable random-access iterator.
  using const_iterator = Base::const_iterator;
  /// Mutable reverse iterator.
  using reverse_iterator = Base::reverse_iterator;
  /// Immutable reverse iterator.
  using const_reverse_iterator = Base::const_reverse_iterator;

  /// Access the element at \p N with bounds checking.
  using Base::at;
  /// Access the element at \p N without bounds checking.
  using Base::operator[];
  /// Return a reference to the first element.
  using Base::front;
  /// Return a reference to the last element.
  using Base::back;
  /// Return a pointer to the underlying element storage.
  using Base::data;
  /// Return an iterator to the first element.
  using Base::begin;
  /// Return an iterator past the last element.
  using Base::end;
  /// Return a const iterator to the first element.
  using Base::cbegin;
  /// Return a const iterator past the last element.
  using Base::cend;
  /// Return a reverse iterator to the last element.
  using Base::rbegin;
  /// Return a reverse iterator past the first element.
  using Base::rend;
  /// Return a const reverse iterator to the last element.
  using Base::crbegin;
  /// Return a const reverse iterator past the first element.
  using Base::crend;
  /// Return whether the table has no elements.
  using Base::empty;
  /// Return the number of elements (always 256).
  using Base::size;
  /// Return the maximum number of elements.
  using Base::max_size;
  /// Assign \p Value to every element.
  using Base::fill;
  /// Swap contents with another table.
  using Base::swap;

  /// Write the table entries to \p OS.
  /// @param OS Output stream for the printed entries.
  LLVM_ABI void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump the table to the debug stream.
  LLVM_ABI LLVM_DUMP_METHOD void dump() const;
#endif
};

/// Result returned when a polynomial algorithm is recognized by the analysis.
///
/// Currently, only the CRC algorithm is recognized.
struct PolynomialInfo {
  /// Small constant trip-count of the analyzed loop.
  unsigned TripCount;

  /// LHS of the polynomial operation, or the initial variable of the
  /// computation.
  ///
  /// All polynomial operations must have a constant RHS, which is the
  /// generating polynomial. It is the LHS of the polynomial division in the
  /// case of CRC. Since polynomial division is an XOR in GF(2^m), this variable
  /// must be XOR'ed with RHS in a loop to yield the ComputedValue.
  Value *LHS;

  /// Generating polynomial, or the RHS of the polynomial division for CRC.
  APInt RHS;

  /// Final computed value of the recognized polynomial algorithm.
  ///
  /// This is a remainder of a polynomial division in the case of CRC, which
  /// must be zero.
  Value *ComputedValue;

  /// Whether the algorithm processes bits in big-endian order.
  ///
  /// The big-endian case implies that bits are reversed, in the case of
  /// bit-wise algorithms such as CRC.
  bool IsBigEndian;

  /// Optional auxiliary checksum that augments the LHS.
  ///
  /// In the case of CRC, it is XOR'ed with the LHS, so that the computation's
  /// final remainder is zero.
  Value *LHSAux;

  /// Construct polynomial recognition results for a matched loop.
  /// @param TripCount Constant trip count of the analyzed loop.
  /// @param LHS Initial LHS value of the polynomial computation.
  /// @param RHS Generating polynomial (constant RHS).
  /// @param ComputedValue Final computed value of the algorithm.
  /// @param IsBigEndian Whether the algorithm is big-endian.
  /// @param LHSAux Optional auxiliary value XOR'ed with \p LHS.
  LLVM_ABI PolynomialInfo(unsigned TripCount, Value *LHS, const APInt &RHS,
                          Value *ComputedValue, bool IsBigEndian,
                          Value *LHSAux = nullptr);
};

/// Analysis that recognizes hash algorithms such as CRC in loops.
class HashRecognize {
  const Loop &L;
  ScalarEvolution &SE;

public:
  /// Construct the analysis for loop \p L using scalar evolution \p SE.
  /// @param L Loop to analyze.
  /// @param SE Scalar evolution analysis for \p L.
  LLVM_ABI HashRecognize(const Loop &L, ScalarEvolution &SE);

  /// Attempt to recognize a CRC algorithm in the analyzed loop.
  /// @return PolynomialInfo on success, or a StringRef failure reason.
  LLVM_ABI std::variant<PolynomialInfo, StringRef> recognizeCRC() const;

  /// Return recognized polynomial info when CRC matching succeeds.
  /// @return PolynomialInfo if recognition succeeds; otherwise std::nullopt.
  LLVM_ABI std::optional<PolynomialInfo> getResult() const;

  /// Build a 256-entry Sarwate CRC table for generating polynomial \p GenPoly.
  /// @param GenPoly Generating polynomial used to interleave table entries.
  /// @param IsBigEndian Whether to build the table for a big-endian CRC.
  /// @return CRC lookup table with 256 interleaved entries.
  LLVM_ABI static CRCTable genSarwateTable(const APInt &GenPoly,
                                           bool IsBigEndian);

  /// Generate Mu and FullGenPoly constants for a GF(2) Barrett reduction.
  ///
  /// Returns a pair of Mu of bitwidth TC+1 and FullGenPoly of bitwidth BW+1.
  /// Mu is used in the first clmul operation. Mu = floor(x^(BW+TC) / P(x)).
  /// FullGenPoly is used in the second clmul operation, and is Info.RHS with
  /// the implied BW'th bit.
  /// Endianness is accounted for using Info.IsBigEndian.
  /// @param Info Recognized polynomial algorithm providing RHS and endianness.
  /// @return Pair of (Mu, FullGenPoly) Barrett reduction constants.
  LLVM_ABI static std::pair<APInt, APInt>
  genBarrettConstants(const PolynomialInfo &Info);

  /// Write analysis results for the loop to \p OS.
  /// @param OS Output stream for the printed analysis.
  LLVM_ABI void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump analysis results to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif
};

/// Printer pass for the HashRecognize analysis.
class HashRecognizePrinterPass
    : public RequiredPassInfoMixin<HashRecognizePrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes HashRecognize results to \p OS.
  /// @param OS Output stream for the printed analysis.
  explicit HashRecognizePrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print HashRecognize results for loop \p L.
  /// @param L Loop whose hash recognition results are printed.
  /// @param AM Loop analysis manager providing analyses.
  /// @param AR Standard loop analysis results, including ScalarEvolution.
  /// @param U Loop pass manager updater (unused by the printer).
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};
} // namespace llvm

#endif
