//===- GlobalsStream.h - PDB Index of Symbols by Name -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_GLOBALSSTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_GLOBALSSTREAM_H

#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"

namespace llvm {
class BinaryStreamReader;
namespace msf {
class MappedBlockStream;
}
namespace pdb {
class SymbolStream;

/// Iterator over hash records producing symbol record offsets. Abstracts away
/// the fact that symbol record offsets on disk are off-by-one.
class GSIHashIterator
    : public iterator_adaptor_base<
          GSIHashIterator, FixedStreamArrayIterator<PSHashRecord>,
          std::random_access_iterator_tag, const uint32_t> {
public:
  /// Construct a GSI hash iterator from an underlying hash-record iterator.
  ///
  /// \param v Underlying FixedStreamArrayIterator over PSHashRecord entries.
  template <typename T>
  GSIHashIterator(T &&v)
      : GSIHashIterator::iterator_adaptor_base(std::forward<T &&>(v)) {}

  /// Return the corrected symbol-record offset for the current hash entry.
  ///
  /// \returns The on-disk offset minus one, yielding the real offset into the
  ///     PDB symbol stream.
  uint32_t operator*() const {
    uint32_t Off = this->I->Off;
    return --Off;
  }
};

/// GSI hash table size constants from the Microsoft PDB reference.
///
/// See https://github.com/Microsoft/microsoft-pdb/blob/master/PDB/dbi/gsi.cpp
enum : unsigned {
  /// Number of hash buckets in a GSI hash table.
  IPHR_HASH = 4096
};

/// Readonly view of a GSI hash table in the globals and publics streams.
///
/// Most clients will only want to iterate this to get symbol record offsets
/// into the PDB symbol stream.
class GSIHashTable {
public:
  /// Parsed GSI hash header describing signature, version, and sizes.
  const GSIHashHeader *HashHdr;
  /// Array of hash records giving symbol offsets into the symbol stream.
  FixedStreamArray<PSHashRecord> HashRecords;
  /// Bitmap of which of the IPHR_HASH buckets are present on disk.
  FixedStreamArray<support::ulittle32_t> HashBitmap;
  /// Compressed hash bucket table; each entry indexes into HashRecords.
  FixedStreamArray<support::ulittle32_t> HashBuckets;
  /// Maps an expanded bucket index in [0, IPHR_HASH] to a compressed index, or
  /// -1 if that bucket is empty.
  std::array<int32_t, IPHR_HASH + 1> BucketMap;

  /// Read and parse a GSI hash table from \p Reader.
  ///
  /// \param Reader Stream reader positioned at the start of a GSI hash table.
  ///
  /// \returns An Error on failure, or success if the table was parsed.
  LLVM_ABI Error read(BinaryStreamReader &Reader);

  /// Return the GSI hash header signature field.
  ///
  /// \returns The VerSignature value from the parsed GSI hash header.
  uint32_t getVerSignature() const { return HashHdr->VerSignature; }
  /// Return the GSI hash header version field.
  ///
  /// \returns The VerHdr value from the parsed GSI hash header.
  uint32_t getVerHeader() const { return HashHdr->VerHdr; }
  /// Return the size in bytes of the hash-record array.
  ///
  /// \returns The HrSize value from the parsed GSI hash header.
  uint32_t getHashRecordSize() const { return HashHdr->HrSize; }
  /// Return the number of hash buckets recorded in the header.
  ///
  /// \returns The NumBuckets value from the parsed GSI hash header.
  uint32_t getNumBuckets() const { return HashHdr->NumBuckets; }

  /// Iterator type for walking hash records in this table.
  typedef GSIHashHeader iterator;
  /// Return an iterator to the first hash record.
  ///
  /// \returns A GSIHashIterator positioned at the first hash record.
  GSIHashIterator begin() const { return GSIHashIterator(HashRecords.begin()); }
  /// Return an iterator past the last hash record.
  ///
  /// \returns A GSIHashIterator positioned one past the last hash record.
  GSIHashIterator end() const { return GSIHashIterator(HashRecords.end()); }
};

/// Provides read access to the PDB globals stream hash table.
class GlobalsStream {
public:
  /// Construct a globals stream reader over \p Stream.
  ///
  /// \param Stream Owning mapped MSF stream for the globals stream.
  LLVM_ABI explicit GlobalsStream(
      std::unique_ptr<msf::MappedBlockStream> Stream);
  /// Destroy the globals stream reader.
  LLVM_ABI ~GlobalsStream();
  /// Return the parsed globals GSI hash table.
  ///
  /// \returns A const reference to the parsed globals GSIHashTable.
  const GSIHashTable &getGlobalsTable() const { return GlobalsTable; }
  /// Reload and reparse the globals stream from the underlying MSF stream.
  ///
  /// \returns An Error on failure, or success if the stream was reloaded.
  LLVM_ABI Error reload();

  /// Find global symbol records whose name equals \p Name.
  ///
  /// \param Name Symbol name to look up in the globals hash table.
  /// \param Symbols Symbol stream used to read matching CVSymbol records.
  ///
  /// \returns A vector of (offset, record) pairs for each matching symbol.
  LLVM_ABI std::vector<std::pair<uint32_t, codeview::CVSymbol>>
  findRecordsByName(StringRef Name, const SymbolStream &Symbols) const;

private:
  GSIHashTable GlobalsTable;
  std::unique_ptr<msf::MappedBlockStream> Stream;
};
} // namespace pdb
}

#endif
