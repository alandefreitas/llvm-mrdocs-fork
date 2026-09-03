//===- InterleavedRange.h - Output stream formatting for ranges -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements format objects for printing ranges to output streams.
// For example:
// ```c++
//    ArrayRef<Type> Types = ...;
//    OS << "Types: " << interleaved(Types); // ==> "Types: i32, f16, i8"
//    ArrayRef<int> Values = ...;
//    OS << "Values: " << interleaved_array(Values); // ==> "Values: [1, 2, 3]"
// ```
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_INTERLEAVED_RANGE_H
#define LLVM_SUPPORT_INTERLEAVED_RANGE_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// Format object that prints a range with a separator and optional surroundings.
///
/// Supports specifying the separator and, optionally, the prefix and suffix to
/// be printed surrounding the range. Uses the operator '<<' of the range
/// element type for printing. The range type itself does not have to have an
/// '<<' operator defined.
template <typename Range> class InterleavedRange {
  const Range &TheRange;
  StringRef Separator;
  StringRef Prefix;
  StringRef Suffix;

public:
  /// Construct from range \p R with \p Separator between elements and optional
  /// \p Prefix / \p Suffix.
  ///
  /// \param R Range whose elements will be printed.
  /// \param Separator Text inserted between consecutive elements.
  /// \param Prefix Text printed before the first element.
  /// \param Suffix Text printed after the last element.
  InterleavedRange(const Range &R, StringRef Separator, StringRef Prefix,
                   StringRef Suffix)
      : TheRange(R), Separator(Separator), Prefix(Prefix), Suffix(Suffix) {}

  /// Write \p Interleaved to \p OS and return \p OS.
  ///
  /// \param OS Destination stream.
  /// \param Interleaved Format object to print.
  /// \return The stream \p OS after writing.
  template <typename OStream>
  friend OStream &operator<<(OStream &OS, const InterleavedRange &Interleaved) {
    OS << Interleaved.Prefix;
    llvm::interleave(Interleaved.TheRange, OS, Interleaved.Separator);
    OS << Interleaved.Suffix;
    return OS;
  }

  /// Return the formatted range as a string.
  ///
  /// \return The formatted range as a string.
  std::string str() const {
    std::string Result;
    raw_string_ostream Stream(Result);
    Stream << *this;
    return Result;
  }

  /// Convert to a string by formatting the range.
  ///
  /// \return The formatted range as a string.
  operator std::string() const { return str(); }
};

/// Create a format object that prints \p R with interleaved elements.
///
/// Requires the range element type to be printable using
/// `raw_ostream& operator<<`. The `Separator` and `Prefix` / `Suffix` can be
/// customized. Examples:
/// ```c++
///   SmallVector<int> Vals = {1, 2, 3};
///   OS << interleaved(Vals);                 // ==> "1, 2, 3"
///   OS << interleaved(Vals, ";");            // ==> "1;2;3"
///   OS << interleaved(Vals, " ", "{", "}");  // ==> "{1 2 3}"
/// ```
///
/// \param R Range whose elements will be printed.
/// \param Separator Text inserted between consecutive elements.
/// \param Prefix Text printed before the first element.
/// \param Suffix Text printed after the last element.
/// \return A format object that prints \p R with interleaved elements.
template <typename Range>
InterleavedRange<Range> interleaved(const Range &R, StringRef Separator = ", ",
                                    StringRef Prefix = "",
                                    StringRef Suffix = "") {
  return {R, Separator, Prefix, Suffix};
}

/// Create a format object that prints \p R as an interleaved array.
///
/// Requires the range element type to be printable using
/// `raw_ostream& operator<<`. The `Separator` can be customized. Examples:
/// ```c++
///   SmallVector<int> Vals = {1, 2, 3};
///   OS << interleaved_array(Vals);       // ==> "[1, 2, 3]"
///   OS << interleaved_array(Vals, ";");  // ==> "[1;2;3]"
///   OS << interleaved_array(Vals, " ");  // ==> "[1 2 3]"
/// ```
///
/// \param R Range whose elements will be printed.
/// \param Separator Text inserted between consecutive elements.
/// \return A format object that prints \p R as an interleaved array.
template <typename Range>
InterleavedRange<Range> interleaved_array(const Range &R,
                                          StringRef Separator = ", ") {
  return {R, Separator, "[", "]"};
}

} // end namespace llvm

#endif // LLVM_SUPPORT_INTERLEAVED_RANGE_H
