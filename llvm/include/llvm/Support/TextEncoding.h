//===-- TextEncoding.h - Text encoding conversion class -----------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides a utility class to convert between different character
/// set encodings.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TEXT_ENCODING_H
#define LLVM_SUPPORT_TEXT_ENCODING_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorOr.h"

#include <string>
#include <system_error>

namespace llvm {

template <typename T> class SmallVectorImpl;

namespace details {
/// Abstract interface for text encoding conversion implementations.
class TextEncodingConverterImplBase {

private:
  /// Converts a string.
  /// \param[in] Source source string
  /// \param[out] Result container for converted string
  /// \return error code in case something went wrong
  ///
  /// The following error codes can occur, among others:
  ///   - std::errc::argument_list_too_long: The result requires more than
  ///     std::numeric_limits<size_t>::max() bytes.
  ///   - std::errc::illegal_byte_sequence: The input contains an invalid
  ///     multibyte sequence.
  ///   - std::errc::invalid_argument: The input contains an incomplete
  ///     multibyte sequence.
  ///
  /// If the destination encoding is stateful, the shift state will be set
  /// to the initial state.
  ///
  /// In case of an error, the result string contains the successfully converted
  /// part of the input string.
  ///
  virtual std::error_code convertString(StringRef Source,
                                        SmallVectorImpl<char> &Result) = 0;

  /// Resets the converter to the initial state.
  virtual void reset() = 0;

public:
  /// Virtual destructor for polymorphic encoding converter implementations.
  virtual ~TextEncodingConverterImplBase() = default;

  /// Converts a string and resets the converter to the initial state.
  /// \param[in] Source source string
  /// \param[out] Result container for converted string
  /// \return error code in case something went wrong
  std::error_code convert(StringRef Source, SmallVectorImpl<char> &Result) {
    auto EC = convertString(Source, Result);
    reset();
    return EC;
  }
};
} // namespace details

/// Character set encodings supported by TextEncodingConverter.
///
/// Names inspired by https://wg21.link/p1885.
enum class TextEncoding {
  /// UTF-8 character set encoding.
  UTF8,

  /// IBM EBCDIC 1047 character set encoding.
  IBM1047
};

/// Utility class to convert between different character encodings.
class TextEncodingConverter {
  std::unique_ptr<details::TextEncodingConverterImplBase> Converter;

  TextEncodingConverter(
      std::unique_ptr<details::TextEncodingConverterImplBase> Converter)
      : Converter(std::move(Converter)) {}

public:
  /// Creates a TextEncodingConverter instance.
  /// Returns std::errc::invalid_argument in case the requested conversion is
  /// not supported.
  /// \param[in] From the source character encoding
  /// \param[in] To the target character encoding
  /// \return a TextEncodingConverter instance or an error code
  LLVM_ABI static ErrorOr<TextEncodingConverter> create(TextEncoding From,
                                                        TextEncoding To);

  /// Creates a TextEncodingConverter instance.
  /// Returns std::errc::invalid_argument in case the requested conversion is
  /// not supported.
  /// \param[in] From name of the source character encoding
  /// \param[in] To name of the target character encoding
  /// \return a TextEncodingConverter instance or an error code
  LLVM_ABI static ErrorOr<TextEncodingConverter> create(StringRef From,
                                                        StringRef To);

  /// Deleted copy constructor; TextEncodingConverter is move-only.
  ///
  /// \param Other Unused; copy construction is not supported.
  TextEncodingConverter(const TextEncodingConverter &Other) = delete;
  /// Deleted copy assignment; TextEncodingConverter is move-only.
  ///
  /// \param Other Unused; copy assignment is not supported.
  TextEncodingConverter &operator=(const TextEncodingConverter &Other) = delete;

  /// Move-construct from \p Other, taking ownership of its converter.
  ///
  /// \param Other Converter to move from.
  TextEncodingConverter(TextEncodingConverter &&Other)
      : Converter(std::move(Other.Converter)) {}

  /// Move-assign from \p Other, taking ownership of its converter.
  ///
  /// \param Other Converter to move from.
  /// \return Reference to this converter.
  TextEncodingConverter &operator=(TextEncodingConverter &&Other) {
    if (this != &Other)
      Converter = std::move(Other.Converter);
    return *this;
  }

  /// Destroys the converter and releases its implementation.
  ~TextEncodingConverter() = default;

  /// Converts a string.
  /// \param[in] Source source string
  /// \param[out] Result container for converted string
  /// \return error code in case something went wrong
  std::error_code convert(StringRef Source,
                          SmallVectorImpl<char> &Result) const {
    return Converter->convert(Source, Result);
  }

  /// Converts a string to a newly allocated std::string.
  /// \param[in] Source source string
  /// \return the converted string, or an error code if conversion failed
  ErrorOr<std::string> convert(StringRef Source) const {
    SmallString<100> Result;
    auto EC = Converter->convert(Source, Result);
    if (!EC)
      return std::string(Result);
    return EC;
  }
};

} // namespace llvm

#endif
