//===- DwarfTransformer.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_OUTPUTAGGREGATOR_H
#define LLVM_DEBUGINFO_GSYM_OUTPUTAGGREGATOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/GSYM/ExtractRanges.h"

#include <map>
#include <string>

namespace llvm {

class raw_ostream;

namespace gsym {

/// Aggregates categorized messages and optionally forwards detail to a stream.
///
/// Counts occurrences of category strings reported via Report, and can emit
/// per-report detail through an optional raw_ostream. Results can be
/// enumerated or merged from another aggregator.
class OutputAggregator {
protected:
  /// Category name to occurrence count, ordered for predictable presentation.
  ///
  /// A std::map is preferable over an llvm::StringMap for presenting results
  /// in a predictable order.
  std::map<std::string, unsigned> Aggregation;

  /// Optional stream used for detail output and streaming via operator<<.
  raw_ostream *Out;

  /// Whether to suppress printing the detail messages passed to Report().
  ///
  /// Anything written through operator<< is unaffected, as is the aggregated
  /// summary.
  bool Quiet;

public:
  /// Construct an OutputAggregator that writes detail to the given stream.
  ///
  /// \param out The raw output stream to write into, or nullptr for none.
  /// \param Quiet If true, suppress detail callbacks from Report().
  OutputAggregator(raw_ostream *out, bool Quiet = false)
      : Out(out), Quiet(Quiet) {}

  /// Get the number of distinct categories that have been reported.
  ///
  /// \returns The number of entries in the aggregation map.
  size_t GetNumCategories() const { return Aggregation.size(); }

  /// Check whether detail output from Report is suppressed.
  ///
  /// \returns True if quiet mode is enabled.
  bool IsQuiet() const { return Quiet; }

  /// Record a category and optionally emit detail to the output stream.
  ///
  /// Increments the occurrence count for \p s. If an output stream is set and
  /// quiet mode is off, invokes \p detailCallback with that stream.
  ///
  /// \param s The category string to aggregate.
  /// \param detailCallback Invoked with the output stream to print detail.
  void Report(StringRef s, std::function<void(raw_ostream &o)> detailCallback) {
    Aggregation[std::string(s)]++;
    if (GetOS() && !Quiet)
      detailCallback(*Out);
  }

  /// Invoke a callback for each aggregated category and its count.
  ///
  /// \param handleCounts Called with each category name and occurrence count.
  void EnumerateResults(
      std::function<void(StringRef, unsigned)> handleCounts) const {
    for (auto &&[name, count] : Aggregation)
      handleCounts(name, count);
  }

  /// Get the optional raw output stream.
  ///
  /// \returns The stream pointer, or nullptr if none was set.
  raw_ostream *GetOS() const { return Out; }

  /// Stream a value to the underlying output stream if one is set.
  ///
  /// You can just use the stream, and if it's null, nothing happens.
  /// Don't do a lot of stuff like this, but it's convenient for silly stuff.
  /// It doesn't work with things that have custom insertion operators, though.
  ///
  /// \param value The value to insert into the stream.
  /// \returns A reference to this aggregator for chaining.
  template <typename T> OutputAggregator &operator<<(T &&value) {
    if (Out != nullptr)
      *Out << value;
    return *this;
  }

  /// Merge category counts from another aggregator into this one.
  ///
  /// For multi-threaded usage, we can collect stuff in another aggregator,
  /// then merge it in here. Note that this is *not* thread safe. It is up to
  /// the caller to ensure that this is only called from one thread at a time.
  ///
  /// \param other The aggregator whose counts should be added into this one.
  void Merge(const OutputAggregator &other) {
    for (auto &&[name, count] : other.Aggregation)
      Aggregation[name] += count;
  }
};

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_OUTPUTAGGREGATOR_H
