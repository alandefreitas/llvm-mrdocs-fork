//===- PDBFile.h - Low level interface to a PDB file ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_PDBFILE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_PDBFILE_H

#include "llvm/DebugInfo/MSF/IMSFFile.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/Object/DXContainer.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"

#include <memory>

namespace llvm {

class BinaryStream;

namespace msf {
class MappedBlockStream;
}

namespace pdb {
class DbiStream;
class GlobalsStream;
class InfoStream;
class InjectedSourceStream;
class PDBStringTable;
class PDBFileBuilder;
class PublicsStream;
class SymbolStream;
class TpiStream;

/// Low-level interface to a PDB file backed by an MSF container.
class LLVM_ABI PDBFile : public msf::IMSFFile {
  friend PDBFileBuilder;

public:
  /// Construct a PDBFile for the buffer at \p Path.
  ///
  /// \param Path Path to the PDB file on disk.
  /// \param PdbFileBuffer Binary stream containing the PDB file bytes.
  /// \param Allocator Allocator used for mapped streams and parsed data.
  PDBFile(StringRef Path, std::unique_ptr<BinaryStream> PdbFileBuffer,
          BumpPtrAllocator &Allocator);
  /// Destroy the PDB file.
  ~PDBFile() override;

  /// Return the directory portion of the PDB file path.
  ///
  /// \returns The directory component of the file path.
  StringRef getFileDirectory() const;
  /// Return the full path of the PDB file.
  ///
  /// \returns The full path string for this PDB file.
  StringRef getFilePath() const;

  /// Return the free block map block index from the MSF superblock.
  ///
  /// \returns The free block map block index.
  uint32_t getFreeBlockMapBlock() const;
  /// Return the unknown superblock field whose purpose is not yet known.
  ///
  /// \returns The unknown superblock field value.
  uint32_t getUnknown1() const;

  /// Return the size in bytes of each block in the MSF file.
  ///
  /// \returns The block size in bytes.
  uint32_t getBlockSize() const override;
  /// Return the total number of blocks in the MSF file.
  ///
  /// \returns The number of blocks in the file.
  uint32_t getBlockCount() const override;
  /// Return the number of bytes that make up the MSF directory.
  ///
  /// \returns The directory size in bytes.
  uint32_t getNumDirectoryBytes() const;
  /// Return the block map address from the MSF superblock.
  ///
  /// \returns The block map index from the superblock.
  uint32_t getBlockMapIndex() const;
  /// Return the number of blocks occupied by the MSF directory.
  ///
  /// \returns The number of directory blocks.
  uint32_t getNumDirectoryBlocks() const;
  /// Return the byte offset of the block map within the MSF file.
  ///
  /// \returns The byte offset of the block map.
  uint64_t getBlockMapOffset() const;

  /// Return the number of streams in the MSF file.
  ///
  /// \returns The number of streams.
  uint32_t getNumStreams() const override;
  /// Return the size in bytes of the largest stream in the MSF file.
  ///
  /// \returns The maximum stream size in bytes.
  uint32_t getMaxStreamSize() const;
  /// Return the size in bytes of the stream at \p StreamIndex.
  ///
  /// \param StreamIndex Index of the stream whose size is requested.
  ///
  /// \returns The size of the stream in bytes.
  uint32_t getStreamByteSize(uint32_t StreamIndex) const override;
  /// Return the list of block indices that make up the stream at
  /// \p StreamIndex.
  ///
  /// \param StreamIndex Index of the stream whose block list is requested.
  ///
  /// \returns The block indices that compose the stream.
  ArrayRef<support::ulittle32_t>
  getStreamBlockList(uint32_t StreamIndex) const override;
  /// Return the total size in bytes of the underlying PDB file buffer.
  ///
  /// \returns The size of the PDB file buffer in bytes.
  uint64_t getFileSize() const;

  /// Return a view of \p NumBytes of data from the block at \p BlockIndex.
  ///
  /// \param BlockIndex Index of the block to read from.
  /// \param NumBytes Number of bytes to read from the start of the block.
  ///
  /// \returns The block data on success, or an error if the read fails.
  Expected<ArrayRef<uint8_t>> getBlockData(uint32_t BlockIndex,
                                           uint32_t NumBytes) const override;
  /// Attempt to write \p Data into the block at \p BlockIndex.
  ///
  /// PDBFile is immutable, so this always fails.
  ///
  /// \param BlockIndex Index of the block to write to.
  /// \param Offset Byte offset within the block at which to begin writing.
  /// \param Data Bytes to write into the block.
  ///
  /// \returns An error indicating that the PDB file is not writable.
  Error setBlockData(uint32_t BlockIndex, uint32_t Offset,
                     ArrayRef<uint8_t> Data) const override;

  /// Return the byte sizes of every stream in the MSF layout.
  ///
  /// \returns An array of stream sizes in bytes.
  ArrayRef<support::ulittle32_t> getStreamSizes() const {
    return ContainerLayout.StreamSizes;
  }
  /// Return the block lists of every stream in the MSF layout.
  ///
  /// \returns An array of block-index lists, one per stream.
  ArrayRef<ArrayRef<support::ulittle32_t>> getStreamMap() const {
    return ContainerLayout.StreamMap;
  }

  /// Return the parsed MSF layout for this PDB file.
  ///
  /// \returns A const reference to the MSF layout.
  const msf::MSFLayout &getMsfLayout() const { return ContainerLayout; }
  /// Return a reference to the underlying MSF file buffer.
  ///
  /// \returns A BinaryStreamRef over the MSF file buffer.
  BinaryStreamRef getMsfBuffer() const { return *Buffer; }

  /// Return the block indices that store the MSF directory.
  ///
  /// \returns The directory block index array.
  ArrayRef<support::ulittle32_t> getDirectoryBlockArray() const;

  /// Create a mapped stream for the stream numbered \p SN.
  ///
  /// \param SN Stream number to map, or an invalid index to return null.
  ///
  /// \returns A mapped block stream for \p SN, or null if \p SN is invalid.
  std::unique_ptr<msf::MappedBlockStream>
  createIndexedStream(uint16_t SN) const;
  /// Create a mapped stream for \p StreamIndex if that stream exists.
  ///
  /// Unlike createIndexedStream, this returns an error when the stream index
  /// is out of range.
  ///
  /// \param StreamIndex Index of the stream to map.
  ///
  /// \returns The mapped stream on success, or an error if the stream does
  ///     not exist.
  Expected<std::unique_ptr<msf::MappedBlockStream>>
  safelyCreateIndexedStream(uint32_t StreamIndex) const;
  /// Create a mapped stream for the named stream \p Name.
  ///
  /// \param Name Named-stream name to look up in the PDB info stream.
  ///
  /// \returns The mapped stream on success, or an error if the name or stream
  ///     cannot be resolved.
  Expected<std::unique_ptr<msf::MappedBlockStream>>
  safelyCreateNamedStream(StringRef Name);

  /// Return the layout of the stream at \p StreamIdx.
  ///
  /// \param StreamIdx Index of the stream whose layout is requested.
  ///
  /// \returns The MSF stream layout for \p StreamIdx.
  msf::MSFStreamLayout getStreamLayout(uint32_t StreamIdx) const;
  /// Return the layout of the free page map stream.
  ///
  /// \returns The MSF stream layout of the free page map.
  msf::MSFStreamLayout getFpmStreamLayout() const;

  /// Parse and validate the MSF superblock, free page map, and directory map.
  ///
  /// \returns Success, or an error if the headers are missing or corrupt.
  Error parseFileHeaders();
  /// Parse the MSF directory into stream sizes and stream block maps.
  ///
  /// \returns Success, or an error if the directory stream is corrupt.
  Error parseStreamData();

  /// Return the PDB info stream, loading and caching it on first use.
  ///
  /// \returns The info stream on success, or an error if it cannot be loaded.
  Expected<InfoStream &> getPDBInfoStream();
  /// Return the PDB DBI stream, loading and caching it on first use.
  ///
  /// \returns The DBI stream on success, or an error if it cannot be loaded.
  Expected<DbiStream &> getPDBDbiStream();
  /// Return the PDB globals stream, loading and caching it on first use.
  ///
  /// \returns The globals stream on success, or an error if it cannot be
  ///     loaded.
  Expected<GlobalsStream &> getPDBGlobalsStream();
  /// Return the PDB TPI stream, loading and caching it on first use.
  ///
  /// \returns The TPI stream on success, or an error if it cannot be loaded.
  Expected<TpiStream &> getPDBTpiStream();
  /// Return the PDB IPI stream, loading and caching it on first use.
  ///
  /// \returns The IPI stream on success, or an error if it cannot be loaded.
  Expected<TpiStream &> getPDBIpiStream();
  /// Return the PDB publics stream, loading and caching it on first use.
  ///
  /// \returns The publics stream on success, or an error if it cannot be
  ///     loaded.
  Expected<PublicsStream &> getPDBPublicsStream();
  /// Return the PDB symbol records stream, loading and caching it on first
  /// use.
  ///
  /// \returns The symbol stream on success, or an error if it cannot be
  ///     loaded.
  Expected<SymbolStream &> getPDBSymbolStream();
  /// Return the PDB string table, loading and caching it on first use.
  ///
  /// \returns The string table on success, or an error if it cannot be loaded.
  Expected<PDBStringTable &> getStringTable();
  /// Return the injected-source stream, loading and caching it on first use.
  ///
  /// \returns The injected-source stream on success, or an error if it cannot
  ///     be loaded.
  Expected<InjectedSourceStream &> getInjectedSourceStream();
  /// Return the DXContainer stream, loading and caching it on first use.
  ///
  /// \returns The DXContainer on success, or an error if it cannot be loaded.
  Expected<object::DXContainer &> getDXContainerStream();

  /// Return the bump allocator used by this PDB file.
  ///
  /// \returns A reference to the bump allocator.
  BumpPtrAllocator &getAllocator() { return Allocator; }

  /// Return true if this PDB contains a non-empty DBI stream.
  ///
  /// \returns True if a non-empty DBI stream is present.
  bool hasPDBDbiStream() const;
  /// Return true if this PDB contains a globals stream.
  ///
  /// \returns True if a globals stream is present.
  bool hasPDBGlobalsStream();
  /// Return true if this PDB contains an info stream.
  ///
  /// \returns True if an info stream is present.
  bool hasPDBInfoStream() const;
  /// Return true if this PDB contains an IPI stream.
  ///
  /// \returns True if an IPI stream is present.
  bool hasPDBIpiStream() const;
  /// Return true if this PDB contains a publics stream.
  ///
  /// \returns True if a publics stream is present.
  bool hasPDBPublicsStream();
  /// Return true if this PDB contains a symbol records stream.
  ///
  /// \returns True if a symbol records stream is present.
  bool hasPDBSymbolStream();
  /// Return true if this PDB contains a non-empty TPI stream.
  ///
  /// \returns True if a non-empty TPI stream is present.
  bool hasPDBTpiStream() const;
  /// Return true if this PDB contains a named "/names" string table stream.
  ///
  /// \returns True if a "/names" string table stream is present.
  bool hasPDBStringTable();
  /// Return true if this PDB contains an injected-source header block stream.
  ///
  /// \returns True if an injected-source stream is present.
  bool hasPDBInjectedSourceStream();

  /// Return the pointer size implied by the DBI machine type.
  ///
  /// \returns 8 for AMD64, 4 for other machines, or 0 if the DBI stream cannot
  ///     be loaded.
  uint32_t getPointerSize();

private:
  std::string FilePath;
  BumpPtrAllocator &Allocator;

  std::unique_ptr<BinaryStream> Buffer;

  msf::MSFLayout ContainerLayout;

  std::unique_ptr<GlobalsStream> Globals;
  std::unique_ptr<InfoStream> Info;
  std::unique_ptr<DbiStream> Dbi;
  std::unique_ptr<TpiStream> Tpi;
  std::unique_ptr<TpiStream> Ipi;
  std::unique_ptr<object::DXContainer> Dxc;
  std::unique_ptr<PublicsStream> Publics;
  std::unique_ptr<SymbolStream> Symbols;
  std::unique_ptr<msf::MappedBlockStream> DirectoryStream;
  std::unique_ptr<msf::MappedBlockStream> StringTableStream;
  std::unique_ptr<InjectedSourceStream> InjectedSources;
  std::unique_ptr<PDBStringTable> Strings;
};
}
}

#endif
