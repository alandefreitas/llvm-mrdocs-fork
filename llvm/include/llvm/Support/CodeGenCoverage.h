//== llvm/Support/CodeGenCoverage.h ------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file This file provides rule coverage tracking for tablegen-erated CodeGen.
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CODEGENCOVERAGE_H
#define LLVM_SUPPORT_CODEGENCOVERAGE_H

#include "llvm/ADT/BitVector.h"

namespace llvm {
class MemoryBuffer;

/// Tracks which TableGen CodeGen rules have been exercised at runtime.
class CodeGenCoverage {
protected:
  /// Bit vector of covered rule IDs; bit \c N is set when rule \c N was hit.
  BitVector RuleCoverage;

public:
  /// Const iterator over the set bits of \c RuleCoverage (covered rule IDs).
  using const_covered_iterator = BitVector::const_set_bits_iterator;

  /// Construct empty coverage with no rules marked covered.
  LLVM_ABI CodeGenCoverage();

  /// Mark rule \p RuleID as covered.
  ///
  /// \param RuleID Identifier of the CodeGen rule that was exercised.
  LLVM_ABI void setCovered(uint64_t RuleID);
  /// Return true if rule \p RuleID has been marked covered.
  ///
  /// \param RuleID Identifier of the CodeGen rule to query.
  /// \return True if the rule has been marked covered.
  LLVM_ABI bool isCovered(uint64_t RuleID) const;
  /// Return a range over the IDs of all covered rules.
  ///
  /// \return Iterator range over the IDs of all covered rules.
  LLVM_ABI iterator_range<const_covered_iterator> covered() const;

  /// Load coverage data for \p BackendName from \p Buffer.
  ///
  /// \param Buffer Memory buffer containing serialized coverage records.
  /// \param BackendName Backend whose rule IDs should be recorded; other
  ///        backends in the buffer are skipped.
  /// \return False if the buffer is malformed; true otherwise.
  LLVM_ABI bool parse(MemoryBuffer &Buffer, StringRef BackendName);
  /// Append this coverage to a file named \p FilePrefix plus the process ID.
  ///
  /// Writes nothing when \p FilePrefix is empty or no rules are covered.
  ///
  /// \param FilePrefix Path prefix for the output coverage file.
  /// \param BackendName Backend name written into the coverage record.
  /// \return False if the output file could not be opened; true otherwise.
  LLVM_ABI bool emit(StringRef FilePrefix, StringRef BackendName) const;
  /// Clear all recorded coverage, leaving no rules marked covered.
  LLVM_ABI void reset();
};
} // namespace llvm

#endif // LLVM_SUPPORT_CODEGENCOVERAGE_H
