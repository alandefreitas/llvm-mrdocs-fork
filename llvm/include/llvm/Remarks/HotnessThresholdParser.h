//===- HotnessThresholdParser.h - Parser for hotness threshold --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a simple parser to decode commandline option for
/// remarks hotness threshold that supports both int and a special 'auto' value.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_HOTNESSTHRESHOLDPARSER_H
#define LLVM_REMARKS_HOTNESSTHRESHOLDPARSER_H

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include <optional>

namespace llvm {
namespace remarks {

/// Parse a remarks hotness threshold option value.
///
/// Valid option values are:
/// 1. integer: manually specified threshold; or
/// 2. string 'auto': automatically get threshold from profile summary.
///
/// Returns \c std::nullopt if 'auto' is specified, indicating the value will
/// be filled later during PSI. Negative integers are treated as no threshold
/// (zero).
///
/// \param Arg Option value string to parse.
/// \return The threshold, \c std::nullopt for 'auto', or an error if \p Arg is
/// not a valid integer or 'auto'.
inline Expected<std::optional<uint64_t>> parseHotnessThresholdOption(StringRef Arg) {
  if (Arg == "auto")
    return std::nullopt;

  int64_t Val;
  if (Arg.getAsInteger(10, Val))
    return createStringError(llvm::inconvertibleErrorCode(),
                             "Not an integer: %s", Arg.data());

  // Negative integer effectively means no threshold
  return Val < 0 ? 0 : Val;
}

/// Command-line parser for `*-remarks-hotness-threshold=` option values.
class HotnessThresholdParser : public cl::parser<std::optional<uint64_t>> {
public:
  /// Construct a hotness threshold parser owned by option \p O.
  ///
  /// \param O Option that owns this parser.
  HotnessThresholdParser(cl::Option &O) : cl::parser<std::optional<uint64_t>>(O) {}

  /// Parse \p Arg into a hotness threshold value stored in \p V.
  ///
  /// \param O Option being parsed (used for error reporting).
  /// \param ArgName Option name as written on the command line.
  /// \param Arg Option value string to parse.
  /// \param V Destination for the parsed optional threshold.
  /// \return True on error.
  bool parse(cl::Option &O, StringRef ArgName, StringRef Arg,
             std::optional<uint64_t> &V) {
    auto ResultOrErr = parseHotnessThresholdOption(Arg);
    if (!ResultOrErr)
      return O.error("Invalid argument '" + Arg +
                     "', only integer or 'auto' is supported.");

    V = *ResultOrErr;
    return false;
  }
};

} // namespace remarks
} // namespace llvm
#endif // LLVM_REMARKS_HOTNESSTHRESHOLDPARSER_H
