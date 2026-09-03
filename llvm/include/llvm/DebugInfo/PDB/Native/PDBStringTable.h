//===- PDBStringTable.h - PDB String Table -----------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_PDBSTRINGTABLE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_PDBSTRINGTABLE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {
class BinaryStreamReader;

namespace pdb {

struct PDBStringTableHeader;

/// Reader for the PDB \c /names string table stream.
///
/// Parses the string table header, names buffer, hash bucket array, and name
/// count so strings can be looked up by ID or by content.
class PDBStringTable {
public:
  /// Reload this string table from a binary stream.
  ///
  /// \param Reader The reader positioned at a serialized PDB string table.
  ///
  /// \returns Success, or an error if the stream is truncated or corrupt.
  LLVM_ABI Error reload(BinaryStreamReader &Reader);

  /// Return the size in bytes of the names buffer.
  ///
  /// \returns The \c ByteSize field from the string table header.
  LLVM_ABI uint32_t getByteSize() const;
  /// Return the number of names stored in this string table.
  ///
  /// \returns The name count from the string table epilogue.
  LLVM_ABI uint32_t getNameCount() const;
  /// Return the hash algorithm version used by this string table.
  ///
  /// \returns The \c HashVersion field from the string table header (1 or 2).
  LLVM_ABI uint32_t getHashVersion() const;
  /// Return the string table signature from the header.
  ///
  /// \returns The \c Signature field; expected to equal
  ///     \c PDBStringTableSignature.
  LLVM_ABI uint32_t getSignature() const;

  /// Look up the string stored at the given ID.
  ///
  /// \param ID Byte offset of the null-terminated string in the names buffer.
  ///
  /// \returns The string at \p ID, or an error if the ID is out of range.
  LLVM_ABI Expected<StringRef> getStringForID(uint32_t ID) const;
  /// Look up the ID of the given string in the hash table.
  ///
  /// \param Str The string to find in the string table.
  ///
  /// \returns The ID (byte offset) of \p Str, or an error if it is not found.
  LLVM_ABI Expected<uint32_t> getIDForString(StringRef Str) const;

  /// Return the hash bucket array of string IDs.
  ///
  /// \returns A view of the closed-hash bucket array used for name lookup.
  LLVM_ABI FixedStreamArray<support::ulittle32_t> name_ids() const;

  /// Return the underlying CodeView string table subsection.
  ///
  /// \returns A reference to the parsed names buffer.
  LLVM_ABI const codeview::DebugStringTableSubsectionRef &
  getStringTable() const;

private:
  Error readHeader(BinaryStreamReader &Reader);
  Error readStrings(BinaryStreamReader &Reader);
  Error readHashTable(BinaryStreamReader &Reader);
  Error readEpilogue(BinaryStreamReader &Reader);

  const PDBStringTableHeader *Header = nullptr;
  codeview::DebugStringTableSubsectionRef Strings;
  FixedStreamArray<support::ulittle32_t> IDs;
  uint32_t NameCount = 0;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_PDBSTRINGTABLE_H
