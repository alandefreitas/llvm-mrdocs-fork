//===- Format.h - Efficient printf-style formatting for streams -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the format() function, which can be used with other
// LLVM subsystems to provide printf-style formatting.  This gives all the power
// and risk of printf.  This can be used like this (with raw_ostreams as an
// example):
//
//    OS << "mynumber: " << format("%4.5f", 1234.412) << '\n';
//
// Or if you prefer:
//
//  OS << format("mynumber: %4.5f\n", 1234.412);
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FORMAT_H
#define LLVM_SUPPORT_FORMAT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdio>
#include <optional>
#include <tuple>
#include <utility>

namespace llvm {

namespace detail {
template <typename T> struct decay_if_c_char_array {
  using type = T;
};
template <std::size_t N> struct decay_if_c_char_array<char[N]> {
  using type = const char *;
};
template <typename T>
using decay_if_c_char_array_t = typename decay_if_c_char_array<T>::type;
} // namespace detail

/// Captures a printf-style format string and values for deferred formatting.
///
/// When printed, this synthesizes the string into a temporary buffer and
/// reports whether the buffer was large enough.
template <typename... Ts> class format_object {
  const char *Fmt;
  std::tuple<detail::decay_if_c_char_array_t<Ts>...> Vals;

  template <std::size_t... Is>
  int snprint_tuple(char *Buffer, unsigned BufferSize,
                    std::index_sequence<Is...>) const {
    return snprintf(Buffer, BufferSize, Fmt, std::get<Is>(Vals)...);
  }

public:
  /// Construct from format string \p fmt and argument values \p vals.
  ///
  /// \param fmt Printf-style format string.
  /// \param vals Values substituted into \p fmt.
  format_object(const char *fmt, const Ts &...vals) : Fmt(fmt), Vals(vals...) {
    static_assert(
        (std::is_scalar_v<detail::decay_if_c_char_array_t<Ts>> && ...),
        "format can't be used with non fundamental / non pointer type");
  }

  /// Format into \p Buffer using the stored format string and values.
  ///
  /// \param Buffer Destination character buffer.
  /// \param BufferSize Capacity of \p Buffer in bytes.
  /// \return The number of characters that would have been written, not
  /// counting the terminating null, as with snprintf.
  int snprint(char *Buffer, unsigned BufferSize) const {
    return snprint_tuple(Buffer, BufferSize, std::index_sequence_for<Ts...>());
  }
};

/// Write a formatted object to \p OS.
///
/// \param OS Destination stream.
/// \param Fmt Format object to print.
/// \return The stream \p OS after writing.
template <typename... Ts>
raw_ostream &operator<<(raw_ostream &OS, format_object<Ts...> Fmt) {
  // Stream through an explicitly-typed function_ref. Passing the lambda
  // directly is ambiguous when block pointer conversions are enabled due to a
  // competing raw_ostream::operator<<(const void *) candidate. This ambiguity
  // affects the Swift compiler because it contains Swift code that
  // interoperates with C++ code that instantiates this template, and Swift's
  // C++ interoperability enables block pointer conversions.
  auto Print = [&Fmt](char *Buf, size_t Size) -> int {
    return Fmt.snprint(Buf, Size);
  };
  OS << function_ref<int(char *, size_t)>(Print);
  return OS;
}

/// Create a printf-style format object from \p Fmt and \p Vals.
///
/// These are helper functions used to produce formatted output.  They use
/// template type deduction to construct the appropriate instance of the
/// format_object class to simplify their construction.
///
/// This is typically used like:
/// \code
///   OS << format("%0.4f", myfloat) << '\n';
/// \endcode
///
/// \param Fmt Printf-style format string.
/// \param Vals Values substituted into \p Fmt.
/// \return A format object that prints \p Fmt with \p Vals when streamed.
template <typename... Ts>
inline format_object<Ts...> format(const char *Fmt, const Ts &... Vals) {
  return format_object<Ts...>(Fmt, Vals...);
}

/// This is a helper class for left_justify, right_justify, and center_justify.
class FormattedString {
public:
  /// Text justification options for formatted output.
  enum Justification {
    /// Leave the string unmodified.
    JustifyNone,
    /// Pad on the right to reach the requested width.
    JustifyLeft,
    /// Pad on the left to reach the requested width.
    JustifyRight,
    /// Pad on both sides to center within the requested width.
    JustifyCenter
  };
  /// Construct a formatted string \p S with width \p W and justification \p J.
  ///
  /// \param S String to format.
  /// \param W Target field width in characters.
  /// \param J How to justify \p S within the field.
  FormattedString(StringRef S, unsigned W, Justification J)
      : Str(S), Width(W), Justify(J) {}

private:
  StringRef Str;
  unsigned Width;
  Justification Justify;
  friend class raw_ostream;
};

/// left_justify - append spaces after string so total output is
/// \p Width characters.  If \p Str is larger that \p Width, full string
/// is written with no padding.
///
/// \param Str String to left-justify.
/// \param Width Target field width in characters.
/// \return A formatted string that left-justifies \p Str to \p Width.
inline FormattedString left_justify(StringRef Str, unsigned Width) {
  return FormattedString(Str, Width, FormattedString::JustifyLeft);
}

/// right_justify - add spaces before string so total output is
/// \p Width characters.  If \p Str is larger that \p Width, full string
/// is written with no padding.
///
/// \param Str String to right-justify.
/// \param Width Target field width in characters.
/// \return A formatted string that right-justifies \p Str to \p Width.
inline FormattedString right_justify(StringRef Str, unsigned Width) {
  return FormattedString(Str, Width, FormattedString::JustifyRight);
}

/// center_justify - add spaces before and after string so total output is
/// \p Width characters.  If \p Str is larger that \p Width, full string
/// is written with no padding.
///
/// \param Str String to center-justify.
/// \param Width Target field width in characters.
/// \return A formatted string that center-justifies \p Str to \p Width.
inline FormattedString center_justify(StringRef Str, unsigned Width) {
  return FormattedString(Str, Width, FormattedString::JustifyCenter);
}

/// This is a helper class used for format_hex() and format_decimal().
class FormattedNumber {
  uint64_t HexValue;
  int64_t DecValue;
  unsigned Width;
  bool Hex;
  bool Upper;
  bool HexPrefix;
  friend class raw_ostream;

public:
  /// Construct a number formatter with value, width, and hex/decimal style.
  ///
  /// \param HV Hexadecimal value when formatting as hex.
  /// \param DV Decimal value when formatting as decimal.
  /// \param W Field width in characters.
  /// \param H True to format as hexadecimal.
  /// \param U True to use uppercase hex digits.
  /// \param Prefix True to prepend "0x" for hexadecimal output.
  FormattedNumber(uint64_t HV, int64_t DV, unsigned W, bool H, bool U,
                  bool Prefix)
      : HexValue(HV), DecValue(DV), Width(W), Hex(H), Upper(U),
        HexPrefix(Prefix) {}
};

/// Output \p N as a fixed-width hexadecimal value with a "0x" prefix.
///
/// If the number will not fit in \p Width, the full number is still printed.
/// Examples:
///   OS << format_hex(255, 4)              => 0xff
///   OS << format_hex(255, 4, true)        => 0xFF
///   OS << format_hex(255, 6)              => 0x00ff
///   OS << format_hex(255, 2)              => 0xff
///
/// \param N Value to format.
/// \param Width Minimum field width in characters.
/// \param Upper If true, use uppercase hex digits.
/// \return A formatted number that prints \p N as prefixed hexadecimal.
inline FormattedNumber format_hex(uint64_t N, unsigned Width,
                                  bool Upper = false) {
  assert(Width <= 18 && "hex width must be <= 18");
  return FormattedNumber(N, 0, Width, true, Upper, true);
}

/// Output \p N as a fixed-width hexadecimal value without a "0x" prefix.
///
/// If the number will not fit in \p Width, the full number is still printed.
/// Examples:
///   OS << format_hex_no_prefix(255, 2)              => ff
///   OS << format_hex_no_prefix(255, 2, true)        => FF
///   OS << format_hex_no_prefix(255, 4)              => 00ff
///   OS << format_hex_no_prefix(255, 1)              => ff
///
/// \param N Value to format.
/// \param Width Minimum field width in characters.
/// \param Upper If true, use uppercase hex digits.
/// \return A formatted number that prints \p N as unprefixed hexadecimal.
inline FormattedNumber format_hex_no_prefix(uint64_t N, unsigned Width,
                                            bool Upper = false) {
  assert(Width <= 16 && "hex width must be <= 16");
  return FormattedNumber(N, 0, Width, true, Upper, false);
}

/// Output \p N as a right-justified, fixed-width decimal.
///
/// If the number will not fit in \p Width, the full number is still printed.
/// Examples:
///   OS << format_decimal(0, 5)     => "    0"
///   OS << format_decimal(255, 5)   => "  255"
///   OS << format_decimal(-1, 3)    => " -1"
///   OS << format_decimal(12345, 3) => "12345"
///
/// \param N Value to format.
/// \param Width Minimum field width in characters.
/// \return A formatted number that prints \p N as a fixed-width decimal.
inline FormattedNumber format_decimal(int64_t N, unsigned Width) {
  return FormattedNumber(0, N, Width, false, false, false);
}

/// Helper that formats a byte sequence as a hex dump for streaming.
class FormattedBytes {
  ArrayRef<uint8_t> Bytes;

  // If not std::nullopt, display offsets for each line relative to starting
  // value.
  std::optional<uint64_t> FirstByteOffset;
  uint32_t IndentLevel;  // Number of characters to indent each line.
  uint32_t NumPerLine;   // Number of bytes to show per line.
  uint8_t ByteGroupSize; // How many hex bytes are grouped without spaces
  bool Upper;            // Show offset and hex bytes as upper case.
  bool ASCII;            // Show the ASCII bytes for the hex bytes to the right.
  friend class raw_ostream;

public:
  /// Construct a hex-dump formatter for \p B with layout options.
  ///
  /// \param B Bytes to format.
  /// \param IL Number of characters to indent each line.
  /// \param O Optional base offset shown for each line.
  /// \param NPL Number of bytes shown per line.
  /// \param BGS Number of hex bytes grouped without spaces.
  /// \param U If true, show offset and hex bytes in uppercase.
  /// \param A If true, show an ASCII column beside the hex bytes.
  FormattedBytes(ArrayRef<uint8_t> B, uint32_t IL, std::optional<uint64_t> O,
                 uint32_t NPL, uint8_t BGS, bool U, bool A)
      : Bytes(B), FirstByteOffset(O), IndentLevel(IL), NumPerLine(NPL),
        ByteGroupSize(BGS), Upper(U), ASCII(A) {

    if (ByteGroupSize > NumPerLine)
      ByteGroupSize = NumPerLine;
  }
};

/// Format \p Bytes as a hex dump without an ASCII column.
///
/// \param Bytes Bytes to format.
/// \param FirstByteOffset Optional base offset shown for each line.
/// \param NumPerLine Number of bytes shown per line.
/// \param ByteGroupSize Number of hex bytes grouped without spaces.
/// \param IndentLevel Number of characters to indent each line.
/// \param Upper If true, show offset and hex bytes in uppercase.
/// \return A formatted bytes object that dumps \p Bytes as hex without ASCII.
inline FormattedBytes
format_bytes(ArrayRef<uint8_t> Bytes,
             std::optional<uint64_t> FirstByteOffset = std::nullopt,
             uint32_t NumPerLine = 16, uint8_t ByteGroupSize = 4,
             uint32_t IndentLevel = 0, bool Upper = false) {
  return FormattedBytes(Bytes, IndentLevel, FirstByteOffset, NumPerLine,
                        ByteGroupSize, Upper, false);
}

/// Format \p Bytes as hex with an ASCII column.
///
/// \param Bytes Bytes to format.
/// \param FirstByteOffset Optional base offset shown for each line.
/// \param NumPerLine Number of bytes shown per line.
/// \param ByteGroupSize Number of hex bytes grouped without spaces.
/// \param IndentLevel Number of characters to indent each line.
/// \param Upper If true, show offset and hex bytes in uppercase.
/// \return A formatted bytes object that dumps \p Bytes as hex with ASCII.
inline FormattedBytes
format_bytes_with_ascii(ArrayRef<uint8_t> Bytes,
                        std::optional<uint64_t> FirstByteOffset = std::nullopt,
                        uint32_t NumPerLine = 16, uint8_t ByteGroupSize = 4,
                        uint32_t IndentLevel = 0, bool Upper = false) {
  return FormattedBytes(Bytes, IndentLevel, FirstByteOffset, NumPerLine,
                        ByteGroupSize, Upper, true);
}

} // end namespace llvm

#endif
