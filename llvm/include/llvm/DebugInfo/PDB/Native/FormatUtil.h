//===- FormatUtil.h ------------------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_FORMATUTIL_H
#define LLVM_DEBUGINFO_PDB_NATIVE_FORMATUTIL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"

#include <string>

namespace llvm {
namespace pdb {

#define PUSH_MASKED_FLAG(Enum, Mask, TheOpt, Value, Text)                      \
  if (Enum::TheOpt == (Value & Mask))                                          \
    Opts.push_back(Text);

#define PUSH_FLAG(Enum, TheOpt, Value, Text)                                   \
  PUSH_MASKED_FLAG(Enum, Enum::TheOpt, TheOpt, Value, Text)

#define RETURN_CASE(Enum, X, Ret)                                              \
  case Enum::X:                                                                \
    return Ret;

/// Format an unrecognized enumerator as \c "unknown (N)".
///
/// \param Value The enumerator whose underlying integer value is printed.
///
/// \returns A string of the form \c "unknown (N)" where \c N is the underlying
///     value of \p Value.
template <typename T> std::string formatUnknownEnum(T Value) {
  return formatv("unknown ({0})", llvm::to_underlying(Value)).str();
}

/// Format a segment:offset address pair as \c "XXXX:YYYY".
///
/// \param Segment The 16-bit segment (or section) number.
/// \param Offset The 32-bit offset within the segment.
///
/// \returns A zero-padded \c "segment:offset" string.
LLVM_ABI std::string formatSegmentOffset(uint16_t Segment, uint32_t Offset);

/// How COFF section characteristic flags are rendered as text.
enum class CharacteristicStyle {
  HeaderDefinition, ///< Format as Windows header macro names.
  Descriptive,      ///< Format as human-readable words.
};

/// Format a COFF section characteristics bitfield as a typeset flag list.
///
/// \param IndentLevel Number of spaces to indent continuation lines.
/// \param C The section characteristics bitfield to format.
/// \param FlagsPerLine Maximum number of flags to place on each line.
/// \param Separator Text inserted between consecutive flags.
/// \param Style Whether to use Windows header names or descriptive words.
///
/// \returns A formatted string listing the set characteristic flags, or
///     \c "invalid" / \c "none" when applicable.
LLVM_ABI std::string formatSectionCharacteristics(
    uint32_t IndentLevel, uint32_t C, uint32_t FlagsPerLine,
    StringRef Separator,
    CharacteristicStyle Style = CharacteristicStyle::HeaderDefinition);

/// Join strings into lines of at most \p GroupSize items each.
///
/// \param Opts The items to join.
/// \param IndentLevel Number of spaces to indent each continuation line.
/// \param GroupSize Maximum number of items on each output line.
/// \param Sep Separator inserted between items (and before each newline).
///
/// \returns The typeset multi-line string.
LLVM_ABI std::string typesetItemList(ArrayRef<std::string> Opts,
                                     uint32_t IndentLevel, uint32_t GroupSize,
                                     StringRef Sep);

/// Format strings as a bracketed, indented vertical list.
///
/// \param IndentLevel Number of spaces to indent each string entry.
/// \param Strings The string entries to list.
///
/// \returns A string of the form \c "[\\n  s1\\n  s2\\n...]" with each entry
///     indented by \p IndentLevel spaces.
LLVM_ABI std::string typesetStringList(uint32_t IndentLevel,
                                       ArrayRef<StringRef> Strings);

/// Format a CodeView debug subsection kind as text.
///
/// \param Kind The debug subsection kind to format.
/// \param Friendly If true, use short friendly names; otherwise use
///     \c DEBUG_S_* header-style names.
///
/// \returns The formatted name, or an unknown-enum string for unrecognized
///     values.
LLVM_ABI std::string formatChunkKind(codeview::DebugSubsectionKind Kind,
                                     bool Friendly = true);

/// Format a CodeView symbol kind as its enumerator name.
///
/// \param K The symbol kind to format.
///
/// \returns The enumerator name string, or an unknown-enum string for
///     unrecognized values.
LLVM_ABI std::string formatSymbolKind(codeview::SymbolKind K);

/// Format a CodeView type leaf kind as its enumerator name.
///
/// \param K The type leaf kind to format.
///
/// \returns The enumerator name string, or an \c "UNKNOWN RECORD" string for
///     unrecognized values.
LLVM_ABI std::string formatTypeLeafKind(codeview::TypeLeafKind K);

namespace detail {
template <typename T>
struct EndianAdapter final
    : public FormatAdapter<support::detail::packed_endian_specific_integral<
          T, llvm::endianness::little, support::unaligned>> {
  using EndianType = support::detail::packed_endian_specific_integral<
      T, llvm::endianness::little, support::unaligned>;

  explicit EndianAdapter(EndianType &&Item)
      : FormatAdapter<EndianType>(std::move(Item)) {}

  void format(llvm::raw_ostream &Stream, StringRef Style) {
    format_provider<T>::format(static_cast<T>(this->Item), Stream, Style);
  }
};
} // namespace detail

/// Wrap a little-endian packed integral so it can be passed to \c formatv.
///
/// \param Value The little-endian packed value to adapt for formatting.
///
/// \returns An \c EndianAdapter that formats \p Value as its underlying type.
template <typename T>
detail::EndianAdapter<T> fmtle(support::detail::packed_endian_specific_integral<
                               T, llvm::endianness::little, support::unaligned>
                                   Value) {
  return detail::EndianAdapter<T>(std::move(Value));
}
} // namespace pdb
} // namespace llvm
#endif
