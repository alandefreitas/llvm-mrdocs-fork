//===- NativeFormatting.h - Low level formatting helpers ---------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_NATIVEFORMATTING_H
#define LLVM_SUPPORT_NATIVEFORMATTING_H

#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <optional>

namespace llvm {
class raw_ostream;
/// Floating-point formatting style for native writers.
enum class FloatStyle {
  /// Scientific notation with lowercase exponent (e.g. 1.0e+3).
  Exponent,
  ExponentUpper,
  /// Fixed-point notation.
  Fixed,
  /// Percentage notation (value multiplied by 100 with a % suffix).
  Percent
};
/// Integer formatting style for native writers.
enum class IntegerStyle {
  /// Format as a plain integer.
  Integer,
  /// Format as a decimal number without a radix prefix.
  Number,
};
/// Hexadecimal formatting style for native writers.
enum class HexPrintStyle {
  /// Uppercase hex digits without a 0x prefix.
  Upper,
  Lower,
  /// Uppercase hex digits with a 0x prefix.
  PrefixUpper,
  /// Lowercase hex digits with a 0x prefix.
  PrefixLower
};

/// Return the default digit precision for a float formatting style.
///
/// \param Style Float formatting style whose default precision is requested.
/// \return Default digit precision for \p Style.
LLVM_ABI size_t getDefaultPrecision(FloatStyle Style);

/// Return true if a hex print style includes a radix prefix.
///
/// \param S Hex print style to query.
/// \return True if \p S includes a 0x radix prefix.
LLVM_ABI bool isPrefixedHexStyle(HexPrintStyle S);

/// Write an unsigned int to a stream.
///
/// \param S Destination stream.
/// \param N Value to write.
/// \param MinDigits Minimum number of digits to emit (zero-padded if needed).
/// \param Style Integer formatting style.
/// \param NonNegativePlus If true, prefix a '+' for non-negative values.
LLVM_ABI void write_integer(raw_ostream &S, unsigned int N, size_t MinDigits,
                            IntegerStyle Style, bool NonNegativePlus = false);
/// Write a signed int to a stream.
///
/// \param S Destination stream.
/// \param N Value to write.
/// \param MinDigits Minimum number of digits to emit (zero-padded if needed).
/// \param Style Integer formatting style.
/// \param NonNegativePlus If true, prefix a '+' for non-negative values.
LLVM_ABI void write_integer(raw_ostream &S, int N, size_t MinDigits,
                            IntegerStyle Style, bool NonNegativePlus = false);
/// Write an unsigned long to a stream.
///
/// \param S Destination stream.
/// \param N Value to write.
/// \param MinDigits Minimum number of digits to emit (zero-padded if needed).
/// \param Style Integer formatting style.
/// \param NonNegativePlus If true, prefix a '+' for non-negative values.
LLVM_ABI void write_integer(raw_ostream &S, unsigned long N, size_t MinDigits,
                            IntegerStyle Style, bool NonNegativePlus = false);
/// Write a signed long to a stream.
///
/// \param S Destination stream.
/// \param N Value to write.
/// \param MinDigits Minimum number of digits to emit (zero-padded if needed).
/// \param Style Integer formatting style.
/// \param NonNegativePlus If true, prefix a '+' for non-negative values.
LLVM_ABI void write_integer(raw_ostream &S, long N, size_t MinDigits,
                            IntegerStyle Style, bool NonNegativePlus = false);
/// Write an unsigned long long to a stream.
///
/// \param S Destination stream.
/// \param N Value to write.
/// \param MinDigits Minimum number of digits to emit (zero-padded if needed).
/// \param Style Integer formatting style.
/// \param NonNegativePlus If true, prefix a '+' for non-negative values.
LLVM_ABI void write_integer(raw_ostream &S, unsigned long long N,
                            size_t MinDigits, IntegerStyle Style,
                            bool NonNegativePlus = false);
/// Write a signed long long to a stream.
///
/// \param S Destination stream.
/// \param N Value to write.
/// \param MinDigits Minimum number of digits to emit (zero-padded if needed).
/// \param Style Integer formatting style.
/// \param NonNegativePlus If true, prefix a '+' for non-negative values.
LLVM_ABI void write_integer(raw_ostream &S, long long N, size_t MinDigits,
                            IntegerStyle Style, bool NonNegativePlus = false);

/// Write a hexadecimal value to a stream.
///
/// \param S Destination stream.
/// \param N Value to write.
/// \param Style Hexadecimal formatting style.
/// \param Width Optional minimum field width; zero-pads when set.
LLVM_ABI void write_hex(raw_ostream &S, uint64_t N, HexPrintStyle Style,
                        std::optional<size_t> Width = std::nullopt);
/// Write a floating-point value to a stream.
///
/// \param S Destination stream.
/// \param D Value to write.
/// \param Style Floating-point formatting style.
/// \param Precision Optional digit precision; defaults per \p Style when unset.
LLVM_ABI void write_double(raw_ostream &S, double D, FloatStyle Style,
                           std::optional<size_t> Precision = std::nullopt);
}

#endif

