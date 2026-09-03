//===- NamedStreamMap.h - PDB Named Stream Map ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NAMEDSTREAMMAP_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NAMEDSTREAMMAP_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/Native/HashTable.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

class BinaryStreamReader;
class BinaryStreamWriter;

namespace pdb {

class NamedStreamMap;

/// Hash-table traits adapting stream names to offsets in a NamedStreamMap.
struct NamedStreamMapTraits {
  /// The named stream map whose string buffer backs storage keys.
  NamedStreamMap *NS;

  /// Construct traits bound to the named stream map \p NS.
  ///
  /// \param NS The named stream map used to resolve and append string data.
  LLVM_ABI explicit NamedStreamMapTraits(NamedStreamMap &NS);
  /// Hash the lookup key \p S for closed-hash table lookup.
  ///
  /// \param S The stream name used as the lookup key.
  ///
  /// \returns The truncated hash of \p S as used by the PDB named stream map.
  LLVM_ABI uint16_t hashLookupKey(StringRef S) const;
  /// Convert a storage key offset into the corresponding lookup string.
  ///
  /// \param Offset Byte offset of the null-terminated name in the string
  ///     buffer.
  ///
  /// \returns The stream name stored at \p Offset.
  LLVM_ABI StringRef storageKeyToLookupKey(uint32_t Offset) const;
  /// Convert a lookup string into a storage key, appending if needed.
  ///
  /// \param S The stream name to store or look up in the string buffer.
  ///
  /// \returns The byte offset of \p S in the named stream map string buffer.
  LLVM_ABI uint32_t lookupKeyToStorageKey(StringRef S);
};

/// Maps PDB named-stream names to MSF stream numbers.
class NamedStreamMap {
  /// Friend builder with privileged access to NamedStreamMap internals.
  friend class NamedStreamMapBuilder;

public:
  /// Construct an empty named stream map.
  LLVM_ABI NamedStreamMap();

  /// Load the named stream map from a binary stream.
  ///
  /// \param Stream The reader positioned at a serialized named stream map.
  ///
  /// \returns Success, or an error if the stream is truncated or corrupt.
  LLVM_ABI Error load(BinaryStreamReader &Stream);
  /// Write the named stream map to a binary stream.
  ///
  /// \param Writer The writer that receives the serialized named stream map.
  ///
  /// \returns Success, or an error if the write fails.
  LLVM_ABI Error commit(BinaryStreamWriter &Writer) const;
  /// Return the number of bytes required to serialize this map.
  ///
  /// \returns The serialized size in bytes, including the string buffer and
  ///     offset-index hash table.
  LLVM_ABI uint32_t calculateSerializedLength() const;

  /// Return the number of named stream entries.
  ///
  /// \returns The count of name-to-stream-number mappings.
  LLVM_ABI uint32_t size() const;
  /// Look up the stream number for the named stream \p Stream.
  ///
  /// \param Stream The named stream to look up.
  /// \param StreamNo On success, set to the MSF stream number for \p Stream.
  ///
  /// \returns True if \p Stream was found; false otherwise.
  LLVM_ABI bool get(StringRef Stream, uint32_t &StreamNo) const;
  /// Insert or update the mapping from \p Stream to \p StreamNo.
  ///
  /// \param Stream The named stream to insert or update.
  /// \param StreamNo The MSF stream number associated with \p Stream.
  LLVM_ABI void set(StringRef Stream, uint32_t StreamNo);

  /// Append null-terminated string data for \p S to the internal buffer.
  ///
  /// \param S The string to append (a trailing null is stored after it).
  ///
  /// \returns The byte offset of \p S in the string buffer before appending.
  LLVM_ABI uint32_t appendStringData(StringRef S);
  /// Return the null-terminated string stored at \p Offset.
  ///
  /// \param Offset Byte offset into the internal string buffer.
  ///
  /// \returns A view of the string beginning at \p Offset.
  LLVM_ABI StringRef getString(uint32_t Offset) const;
  /// Hash the string stored at \p Offset.
  ///
  /// \param Offset Byte offset into the internal string buffer.
  ///
  /// \returns The V1 string hash of the name at \p Offset.
  LLVM_ABI uint32_t hashString(uint32_t Offset) const;

  /// Return a copy of all name-to-stream-number mappings.
  ///
  /// \returns A string map from stream name to MSF stream number.
  LLVM_ABI StringMap<uint32_t> entries() const;

private:
  NamedStreamMapTraits HashTraits;
  /// Closed hash table from Offset -> StreamNumber, where Offset is the offset
  /// of the stream name in NamesBuffer.
  HashTable<support::ulittle32_t> OffsetIndexMap;

  /// Buffer of string data.
  std::vector<char> NamesBuffer;
};

} // end namespace pdb

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NAMEDSTREAMMAP_H
