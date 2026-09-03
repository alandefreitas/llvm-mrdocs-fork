//===- TpiStream.cpp - PDB Type Info (TPI) Stream 2 Access ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_TPISTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_TPISTREAM_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/PDB/Native/HashTable.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"

#include "llvm/Support/Error.h"

namespace llvm {
class BinaryStream;
namespace codeview {
class TypeIndex;
struct TypeIndexOffset;
class LazyRandomTypeCollection;
}
namespace msf {
class MappedBlockStream;
}
namespace pdb {
struct TpiStreamHeader;
class PDBFile;

/// Provides read access to the PDB Type Info (TPI) stream.
class TpiStream {
  friend class TpiStreamBuilder;

public:
  /// Construct a TPI stream reader over \p Stream from \p File.
  ///
  /// \param File Owning PDB file used to open the related hash stream.
  /// \param Stream Owning mapped MSF stream for the TPI stream.
  LLVM_ABI TpiStream(PDBFile &File,
                     std::unique_ptr<msf::MappedBlockStream> Stream);
  /// Destroy the TPI stream reader.
  LLVM_ABI ~TpiStream();
  /// Reload and reparse the TPI stream from the underlying MSF stream.
  ///
  /// \returns An Error on failure, or success if the stream was reloaded.
  LLVM_ABI Error reload();

  /// Return the TPI stream format version from the header.
  ///
  /// \returns The TPI stream format version.
  LLVM_ABI PdbRaw_TpiVer getTpiVersion() const;

  /// Return the first type index covered by this TPI stream.
  ///
  /// \returns The first type index covered by this stream.
  LLVM_ABI uint32_t TypeIndexBegin() const;
  /// Return one past the last type index covered by this TPI stream.
  ///
  /// \returns One past the last type index covered by this stream.
  LLVM_ABI uint32_t TypeIndexEnd() const;
  /// Return the number of type records in this TPI stream.
  ///
  /// \returns The number of type records in this stream.
  LLVM_ABI uint32_t getNumTypeRecords() const;
  /// Return the MSF stream index of the TPI hash stream.
  ///
  /// \returns The MSF stream index of the TPI hash stream.
  LLVM_ABI uint16_t getTypeHashStreamIndex() const;
  /// Return the MSF stream index of the TPI auxiliary hash stream.
  ///
  /// \returns The MSF stream index of the TPI auxiliary hash stream.
  LLVM_ABI uint16_t getTypeHashStreamAuxIndex() const;

  /// Return the hash key size in bytes from the TPI header.
  ///
  /// \returns The hash key size in bytes.
  LLVM_ABI uint32_t getHashKeySize() const;
  /// Return the number of hash buckets from the TPI header.
  ///
  /// \returns The number of hash buckets.
  LLVM_ABI uint32_t getNumHashBuckets() const;
  /// Return the per-type hash values from the TPI hash stream.
  ///
  /// \returns The per-type hash values from the TPI hash stream.
  LLVM_ABI FixedStreamArray<support::ulittle32_t> getHashValues() const;
  /// Return the type-index-to-offset table from the TPI hash stream.
  ///
  /// \returns The type-index-to-offset table from the TPI hash stream.
  LLVM_ABI FixedStreamArray<codeview::TypeIndexOffset>
  getTypeIndexOffsets() const;
  /// Return the hash adjuster table from the TPI hash stream.
  ///
  /// \returns A reference to the hash adjuster table.
  LLVM_ABI HashTable<support::ulittle32_t> &getHashAdjusters();

  /// Iterate the type records in this stream, optionally reporting parse errors.
  ///
  /// \param HadError Optional out-parameter set if a record fails to parse.
  ///
  /// \returns A range over the CodeView type records in the TPI stream.
  LLVM_ABI codeview::CVTypeRange types(bool *HadError) const;
  /// Return the array of CodeView type records in this TPI stream.
  ///
  /// \returns A const reference to the array of CodeView type records.
  const codeview::CVTypeArray &typeArray() const { return TypeRecords; }

  /// Return the lazy random type collection built over this stream's records.
  ///
  /// \returns A reference to the lazy random type collection.
  codeview::LazyRandomTypeCollection &typeCollection() { return *Types; }

  /// Resolve a UDT forward reference to its matching full declaration, if any.
  ///
  /// \param ForwardRefTI Type index of a UDT forward-ref (or any type index).
  ///
  /// \returns The type index of the full declaration on success, \p ForwardRefTI
  ///     if it is not a forward ref or no better match is found, or an Error.
  LLVM_ABI Expected<codeview::TypeIndex>
  findFullDeclForForwardRef(codeview::TypeIndex ForwardRefTI) const;

  /// Find type records whose rendered name equals \p Name.
  ///
  /// \param Name Type name to look up via the TPI hash map.
  ///
  /// \returns Type indices of all records whose computed name matches \p Name.
  LLVM_ABI std::vector<codeview::TypeIndex>
  findRecordsByName(StringRef Name) const;

  /// Get a type by its index. \p Index must be a valid type index.
  /// Otherwise, this method asserts in Debug mode.
  ///
  /// \param Index Type index of the record to retrieve; must be valid.
  ///
  /// \returns The CodeView type record at \p Index.
  LLVM_ABI codeview::CVType getType(codeview::TypeIndex Index);

  /// The same as getType() except that the return value is defined
  /// (empty/invalid) if \p Index doesn't exist. Use codeview::CVType::valid()
  /// to check the return value.
  ///
  /// \param Index Type index of the record to retrieve.
  ///
  /// \returns The CodeView type record at \p Index, or an empty/invalid record
  ///     if \p Index does not exist.
  LLVM_ABI codeview::CVType getTypeOrEmpty(codeview::TypeIndex Index);

  /// Get the type at \p Index if it exists or \c std::nullopt otherwise.
  ///
  /// \param Index Type index of the record to retrieve.
  ///
  /// \returns The CodeView type record at \p Index, or \c std::nullopt if it
  ///     does not exist.
  LLVM_ABI std::optional<codeview::CVType>
  tryGetType(codeview::TypeIndex Index);

  /// Get the type at \p Index if it exists or an error about the failure.
  ///
  /// \param Index Type index of the record to retrieve.
  ///
  /// \returns The CodeView type record at \p Index, or an Error if it does not
  ///     exist.
  LLVM_ABI Expected<codeview::CVType> getTypeOrError(codeview::TypeIndex Index);

  /// Return a reference to the raw type-records substream bytes.
  ///
  /// \returns A reference to the raw type-records substream bytes.
  LLVM_ABI BinarySubstreamRef getTypeRecordsSubstream() const;

  /// Commit any pending writes for this stream.
  ///
  /// \returns An Error on failure, or success if there was nothing to commit.
  LLVM_ABI Error commit();

  /// Build the in-memory hash map used for name and forward-ref lookups.
  LLVM_ABI void buildHashMap();

  /// Return true if the in-memory type hash map has already been built.
  ///
  /// \returns True if the in-memory type hash map has already been built.
  LLVM_ABI bool supportsTypeLookup() const;

private:
  PDBFile &Pdb;
  std::unique_ptr<msf::MappedBlockStream> Stream;

  std::unique_ptr<codeview::LazyRandomTypeCollection> Types;

  BinarySubstreamRef TypeRecordsSubstream;

  codeview::CVTypeArray TypeRecords;

  std::unique_ptr<BinaryStream> HashStream;
  FixedStreamArray<support::ulittle32_t> HashValues;
  FixedStreamArray<codeview::TypeIndexOffset> TypeIndexOffsets;
  HashTable<support::ulittle32_t> HashAdjusters;

  std::vector<std::vector<codeview::TypeIndex>> HashMap;

  const TpiStreamHeader *Header;
};
}
}

#endif
