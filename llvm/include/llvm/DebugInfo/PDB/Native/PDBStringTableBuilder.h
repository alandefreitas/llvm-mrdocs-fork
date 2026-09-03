//===- PDBStringTableBuilder.h - PDB String Table Builder -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file creates the "/names" stream.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_PDBSTRINGTABLEBUILDER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_PDBSTRINGTABLEBUILDER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {
class BinaryStreamWriter;
class WritableBinaryStreamRef;

namespace msf {
struct MSFLayout;
}

namespace pdb {

class PDBFileBuilder;
class PDBStringTableBuilder;

/// Hash-table traits adapting string keys to offsets in a PDBStringTableBuilder.
struct StringTableHashTraits {
  /// The string table builder whose string buffer backs storage keys.
  PDBStringTableBuilder *Table;

  /// Construct default traits with no associated string table builder.
  LLVM_ABI StringTableHashTraits() = default;
  /// Construct traits bound to the string table builder \p Table.
  ///
  /// \param Table The string table builder used to resolve and append strings.
  LLVM_ABI explicit StringTableHashTraits(PDBStringTableBuilder &Table);
  /// Hash the lookup key \p S for closed-hash table lookup.
  ///
  /// \param S The string used as the lookup key.
  ///
  /// \returns The truncated hash of \p S as used by the PDB string table.
  LLVM_ABI uint32_t hashLookupKey(StringRef S) const;
  /// Convert a storage key offset into the corresponding lookup string.
  ///
  /// \param Offset Byte offset of the null-terminated string in the table.
  ///
  /// \returns The string stored at \p Offset.
  LLVM_ABI StringRef storageKeyToLookupKey(uint32_t Offset) const;
  /// Convert a lookup string into a storage key, inserting if needed.
  ///
  /// \param S The string to store or look up in the string table.
  ///
  /// \returns The byte offset of \p S in the string table buffer.
  LLVM_ABI uint32_t lookupKeyToStorageKey(StringRef S);
};

/// Builds the PDB "/names" string table stream.
class PDBStringTableBuilder {
public:
  /// If string \p S does not exist in the string table, insert it.
  ///
  /// \param S The string to insert or look up.
  ///
  /// \returns The ID for \p S.
  LLVM_ABI uint32_t insert(StringRef S);

  /// Return the ID of an existing string in the table.
  ///
  /// \param S The string to look up.
  ///
  /// \returns The ID associated with \p S.
  LLVM_ABI uint32_t getIdForString(StringRef S) const;
  /// Return the string associated with the given ID.
  ///
  /// \param Id The string table ID to look up.
  ///
  /// \returns The string stored for \p Id.
  LLVM_ABI StringRef getStringForId(uint32_t Id) const;

  /// Return the number of bytes required to serialize this string table.
  ///
  /// \returns The serialized size in bytes, including header, strings, and
  ///     hash table.
  LLVM_ABI uint32_t calculateSerializedSize() const;
  /// Write the string table to a binary stream.
  ///
  /// \param Writer The writer that receives the serialized string table.
  ///
  /// \returns Success, or an error if the write fails.
  LLVM_ABI Error commit(BinaryStreamWriter &Writer) const;

  /// Replace the builder contents with an existing debug string table.
  ///
  /// \param Strings The CodeView debug string table subsection to copy from.
  LLVM_ABI void setStrings(const codeview::DebugStringTableSubsection &Strings);

private:
  uint32_t calculateHashTableSize() const;
  Error writeHeader(BinaryStreamWriter &Writer) const;
  Error writeStrings(BinaryStreamWriter &Writer) const;
  Error writeHashTable(BinaryStreamWriter &Writer) const;
  Error writeEpilogue(BinaryStreamWriter &Writer) const;

  codeview::DebugStringTableSubsection Strings;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_PDBSTRINGTABLEBUILDER_H
