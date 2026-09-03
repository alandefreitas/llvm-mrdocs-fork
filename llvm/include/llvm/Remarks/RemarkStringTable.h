//===-- RemarkStringTable.h - Serializing string table ----------*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class is used to deduplicate and serialize a string table used for
// generating remarks.
//
// For parsing a string table, use ParsedStringTable in RemarkParser.h
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_REMARKSTRINGTABLE_H
#define LLVM_REMARKS_REMARKSTRINGTABLE_H

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <vector>

namespace llvm {

class raw_ostream;
class StringRef;

namespace remarks {

struct ParsedStringTable;
struct Remark;

/// The string table used for serializing remarks.
/// This table can be for example serialized in a section to be consumed after
/// the compilation.
struct StringTable {
  /// The string table containing all the unique strings used in the output.
  /// It maps a string to an unique ID.
  StringMap<unsigned, BumpPtrAllocator> StrTab;
  /// Total size of the string table when serialized.
  size_t SerializedSize = 0;

  /// Construct an empty string table.
  StringTable() = default;

  /// Copy construction is deleted; string tables are not copyable.
  /// @param Other Unused; this constructor is deleted.
  StringTable(const StringTable &Other) = delete;
  /// Copy assignment is deleted; string tables are not copyable.
  /// @param Other Unused; this assignment is deleted.
  StringTable &operator=(const StringTable &Other) = delete;
  /// Move-construct a string table, taking ownership of the strings.
  /// @param Other String table to move from.
  StringTable(StringTable &&Other) = default;
  /// Move-assign from another string table, taking ownership of the strings.
  /// @param Other String table to move from.
  /// @return Reference to this string table.
  StringTable &operator=(StringTable &&Other) = default;

  /// Construct a string table from a ParsedStringTable.
  /// @param Other Parsed string table whose entries are copied into this table.
  LLVM_ABI StringTable(const ParsedStringTable &Other);

  /// Add a string to the table. It returns an unique ID of the string.
  /// @param Str String to insert or look up in the table.
  /// @return Pair of the string's unique ID and a reference to the stored string.
  LLVM_ABI std::pair<unsigned, StringRef> add(StringRef Str);
  /// Modify \p R to use strings from this string table. If the string table
  /// does not contain the strings, it adds them.
  /// @param R Remark whose strings are replaced with entries from this table.
  LLVM_ABI void internalize(Remark &R);
  /// Serialize the string table to a stream.
  ///
  /// It is serialized as a little endian uint64 (the size of the table in
  /// bytes) followed by a sequence of NULL-terminated strings, where the N-th
  /// string is the string with the ID N in the StrTab map.
  /// @param OS Output stream that receives the serialized table.
  LLVM_ABI void serialize(raw_ostream &OS) const;
  /// Serialize the string table to a vector of string references.
  ///
  /// This allows users to do the actual writing to file/memory/other. The
  /// string with the ID == N should be the N-th element in the vector.
  /// @return Vector of string references ordered by ID.
  LLVM_ABI std::vector<StringRef> serialize() const;
};

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_REMARKSTRINGTABLE_H
