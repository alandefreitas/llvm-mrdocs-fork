//===- StringToOffsetTable.h - Emit a big concatenated string ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_STRINGTOOFFSETTABLE_H
#define LLVM_TABLEGEN_STRINGTOOFFSETTABLE_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include <optional>

namespace llvm {

/// Uniques nul-terminated strings and tracks their offsets in a contiguous allocation.
///
/// This class uniques a bunch of nul-terminated strings and keeps track of their
/// offset in a massive contiguous string allocation. It can then output this
/// string blob and use indexes into the string to reference each piece.
class StringToOffsetTable {
  StringMap<unsigned> StringOffset;
  std::string AggregateString;

  /// If this is to be a static class member, the prefix to use (i.e. class name
  /// plus ::)
  const StringRef ClassPrefix;
  const bool AppendZero;
  const bool UsePrefixForStorageMember;

public:
  /// Construct a table that starts with the empty string at offset zero.
  ///
  /// Empty initialization of offsets also acts as zero initialization because
  /// the empty string is always inserted first.
  /// \param AppendZero Whether to append a NUL after each added string.
  /// \param ClassPrefix Prefix for static class members (class name plus `::`).
  /// \param UsePrefixForStorageMember Whether to apply \p ClassPrefix to the
  ///        storage member name when emitting.
  StringToOffsetTable(bool AppendZero = true, StringRef ClassPrefix = "",
                      bool UsePrefixForStorageMember = true)
      : ClassPrefix(ClassPrefix), AppendZero(AppendZero),
        UsePrefixForStorageMember(UsePrefixForStorageMember) {
    // Ensure we always put the empty string at offset zero. That lets empty
    // initialization also be zero initialization for offsets into the table.
    GetOrAddStringOffset("");
  }

  /// Return true if the offset map contains no strings.
  /// \returns True if the offset map contains no strings.
  bool empty() const { return StringOffset.empty(); }

  /// Return the size in bytes of the aggregate string allocation.
  /// \returns Size in bytes of the aggregate string allocation.
  size_t size() const { return AggregateString.size(); }

  /// Return the offset of \p Str, inserting it into the table if needed.
  /// \param Str String to look up or append.
  /// \returns Byte offset of \p Str within the aggregate string.
  LLVM_ABI unsigned GetOrAddStringOffset(StringRef Str);

  /// Return the offset of \p Str if present, otherwise \c std::nullopt.
  /// \param Str String to look up.
  /// \returns Offset of \p Str, or \c std::nullopt if it is not in the table.
  std::optional<unsigned> GetStringOffset(StringRef Str) const {
    auto II = StringOffset.find(Str);
    if (II == StringOffset.end())
      return std::nullopt;
    return II->second;
  }

  /// Emit a string table definition named \p Name to \p OS.
  ///
  /// When possible, this uses string-literal concatenation to emit the string
  /// contents in a readable and searchable way. However, for (very) large string
  /// tables MSVC cannot reliably use string literals and so there we use a large
  /// character array. We still use a line oriented emission and add comments to
  /// provide searchability even in this case.
  ///
  /// The string table, and its input string contents, are always emitted as both
  /// `static` and `constexpr`. Both `Name` and (`Name` + "Storage") must be
  /// valid identifiers to declare.
  /// \param OS Output stream that receives the definition.
  /// \param Name Identifier for the string table and its storage member.
  LLVM_ABI void EmitStringTableDef(raw_ostream &OS, const Twine &Name) const;

  /// Emit the aggregate string contents as a single escaped string literal.
  /// \param O Output stream that receives the string literal.
  LLVM_ABI void EmitString(raw_ostream &O) const;
};

} // end namespace llvm

#endif
