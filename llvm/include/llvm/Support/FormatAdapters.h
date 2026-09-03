//===- FormatAdapters.h - Formatters for common LLVM types -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FORMATADAPTERS_H
#define LLVM_SUPPORT_FORMATADAPTERS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatCommon.h"
#include "llvm/Support/FormatVariadicDetails.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
/// Base class for format adapters that wrap a value of type \c T.
template <typename T> class FormatAdapter {
protected:
  /// Construct an adapter that owns \p Item.
  ///
  /// \param Item Value to wrap for formatting.
  explicit FormatAdapter(T &&Item) : Item(std::forward<T>(Item)) {}

  /// Wrapped value to format.
  T Item;
};

namespace support {
namespace detail {
template <typename T> class AlignAdapter final : public FormatAdapter<T> {
  AlignStyle Where;
  size_t Amount;
  char Fill;

public:
  AlignAdapter(T &&Item, AlignStyle Where, size_t Amount, char Fill)
      : FormatAdapter<T>(std::forward<T>(Item)), Where(Where), Amount(Amount),
        Fill(Fill) {}

  void format(llvm::raw_ostream &Stream, StringRef Style) {
    auto Adapter = detail::FormatFunctor(std::forward<T>(this->Item));
    FmtAlign(Adapter, Where, Amount, Fill).format(Stream, Style);
  }
};

template <typename T> class PadAdapter final : public FormatAdapter<T> {
  size_t Left;
  size_t Right;

public:
  PadAdapter(T &&Item, size_t Left, size_t Right)
      : FormatAdapter<T>(std::forward<T>(Item)), Left(Left), Right(Right) {}

  void format(llvm::raw_ostream &Stream, StringRef Style) {
    auto Adapter = detail::FormatFunctor(std::forward<T>(this->Item));
    Stream.indent(Left);
    Adapter(Stream, Style);
    Stream.indent(Right);
  }
};

template <typename T> class RepeatAdapter final : public FormatAdapter<T> {
  size_t Count;

public:
  RepeatAdapter(T &&Item, size_t Count)
      : FormatAdapter<T>(std::forward<T>(Item)), Count(Count) {}

  void format(llvm::raw_ostream &Stream, StringRef Style) {
    auto Adapter = detail::FormatFunctor(std::forward<T>(this->Item));
    for (size_t I = 0; I < Count; ++I) {
      Adapter(Stream, Style);
    }
  }
};

class ErrorAdapter : public FormatAdapter<Error> {
public:
  ErrorAdapter(Error &&Item) : FormatAdapter(std::move(Item)) {}
  ErrorAdapter(ErrorAdapter &&) = default;
  ~ErrorAdapter() { consumeError(std::move(Item)); }
  void format(llvm::raw_ostream &Stream, StringRef Style) { Stream << Item; }
};
} // namespace detail
} // namespace support

/// Format \p Item aligned within a field of width \p Amount.
///
/// \param Item Value to format.
/// \param Where Alignment of the value within the field.
/// \param Amount Field width in characters.
/// \param Fill Pad character for unused space in the field.
/// \return An adapter that formats \p Item aligned within the field.
template <typename T>
support::detail::AlignAdapter<T> fmt_align(T &&Item, AlignStyle Where,
                                           size_t Amount, char Fill = ' ') {
  return support::detail::AlignAdapter<T>(std::forward<T>(Item), Where, Amount,
                                          Fill);
}

/// Format \p Item with \p Left spaces before and \p Right spaces after.
///
/// \param Item Value to format.
/// \param Left Number of spaces to indent before the value.
/// \param Right Number of spaces to indent after the value.
/// \return An adapter that formats \p Item with the requested padding.
template <typename T>
support::detail::PadAdapter<T> fmt_pad(T &&Item, size_t Left, size_t Right) {
  return support::detail::PadAdapter<T>(std::forward<T>(Item), Left, Right);
}

/// Format \p Item repeated \p Count times.
///
/// \param Item Value to format.
/// \param Count Number of times to format \p Item.
/// \return An adapter that formats \p Item \p Count times.
template <typename T>
support::detail::RepeatAdapter<T> fmt_repeat(T &&Item, size_t Count) {
  return support::detail::RepeatAdapter<T>(std::forward<T>(Item), Count);
}

/// Wrap an Error so formatv takes ownership and consumes it.
///
/// llvm::Error values must be consumed before being destroyed. Wrapping an
/// error in fmt_consume explicitly indicates that the formatv_object should
/// take ownership and consume it.
///
/// \param Item Error to format and consume.
/// \return An adapter that formats and consumes \p Item.
inline support::detail::ErrorAdapter fmt_consume(Error &&Item) {
  return support::detail::ErrorAdapter(std::move(Item));
}
}

#endif
