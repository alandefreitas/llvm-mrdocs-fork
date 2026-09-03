//===-- llvm/Remarks/RemarkFormat.h - The format of remarks -----*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines utilities to deal with the format of remarks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_REMARKFORMAT_H
#define LLVM_REMARKS_REMARKFORMAT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace remarks {

/// Magic string identifying a YAML remarks metadata stream.
constexpr StringLiteral Magic("REMARKS");

/// The format used for serializing/deserializing remarks.
enum class Format {
  /// Unrecognized or invalid remark format.
  Unknown,
  /// Detect the format automatically from the input magic.
  Auto,
  /// YAML-based remark serialization format.
  YAML,
  /// LLVM bitstream remark serialization format.
  Bitstream
};

/// Parse and validate a string for the remark format.
/// @param FormatStr Format name such as "yaml" or "bitstream".
/// @return The corresponding Format, or an error if \p FormatStr is invalid.
LLVM_ABI Expected<Format> parseFormat(StringRef FormatStr);

/// Parse and validate a magic number to a remark format.
/// @param Magic Leading bytes of a remarks buffer used for format detection.
/// @return The Format matching \p Magic, or an error if it is unrecognized.
LLVM_ABI Expected<Format> magicToFormat(StringRef Magic);

/// Detect format based on selected format and magic number.
/// @param Selected Requested format, or Auto to infer from \p Magic.
/// @param Magic Leading bytes of a remarks buffer used when \p Selected is Auto.
/// @return \p Selected when not Auto, otherwise the Format inferred from \p Magic,
///         or an error on failure.
LLVM_ABI Expected<Format> detectFormat(Format Selected, StringRef Magic);

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_REMARKFORMAT_H
