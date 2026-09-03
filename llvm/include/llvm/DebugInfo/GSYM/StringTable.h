//===- StringTable.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_STRINGTABLE_H
#define LLVM_DEBUGINFO_GSYM_STRINGTABLE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/GSYM/ExtractRanges.h"
#include "llvm/DebugInfo/GSYM/GsymTypes.h"
#include <stdint.h>

namespace llvm {
namespace gsym {

/// String tables in GSYM files are required to start with an empty
/// string at offset zero. Strings must be UTF8 NULL terminated strings.
struct StringTable {
  /// Contiguous UTF-8 string-table bytes, null-terminated at each entry.
  StringRef Data;

  /// Construct an empty StringTable.
  StringTable() = default;

  /// Construct a StringTable that views the given string-table bytes.
  ///
  /// \param D Contiguous UTF-8 string-table data starting with an empty
  ///          string at offset zero.
  StringTable(StringRef D) : Data(D) {}

  /// Look up the null-terminated string at the given byte offset.
  ///
  /// \param Offset Byte offset into the string table.
  /// \returns The string at \p Offset, or an empty StringRef if out of range.
  StringRef operator[](size_t Offset) const { return getString(Offset); }

  /// Look up the null-terminated string at the given byte offset.
  ///
  /// \param Offset Byte offset into the string table.
  /// \returns The string at \p Offset, or an empty StringRef if out of range.
  StringRef getString(gsym_strp_t Offset) const {
    if (Offset < Data.size()) {
      auto End = Data.find('\0', Offset);
      return Data.substr(Offset, End - Offset);
    }
    return StringRef();
  }

  /// Clear the string table so it no longer references any data.
  void clear() { Data = StringRef(); }
};

/// Dump all strings in a StringTable to a stream with formatted offsets.
///
/// \param OS The stream to write the dump to.
/// \param S The string table to dump.
/// \param StringOffsetSize Size in bytes used to format each string offset.
inline void dump(raw_ostream &OS, const StringTable &S,
                 uint8_t StringOffsetSize) {
  OS << "String table:\n";
  gsym_strp_t Offset = 0;
  const size_t Size = S.Data.size();
  while (Offset < Size) {
    StringRef Str = S.getString(Offset);
    switch (StringOffsetSize) {
    case 1:
      OS << HEX8(Offset);
      break;
    case 2:
      OS << HEX16(Offset);
      break;
    case 4:
      OS << HEX32(Offset);
      break;
    case 8:
      OS << HEX64(Offset);
      break;
    default:
      OS << HEX64(Offset);
    }
    OS << ": \"" << Str << "\"\n";
    Offset += Str.size() + 1;
  }
}

} // namespace gsym
} // namespace llvm
#endif // LLVM_DEBUGINFO_GSYM_STRINGTABLE_H
