//==- MappedBlockStream.h - Discontiguous stream data in an MSF --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_MSF_MAPPEDBLOCKSTREAM_H
#define LLVM_DEBUGINFO_MSF_MAPPEDBLOCKSTREAM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryStream.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {
namespace msf {

/// A BinaryStream view of data stored as possibly discontiguous MSF blocks.
///
/// MappedBlockStream represents data stored in an MSF file into chunks of a
/// particular size (called the Block Size), and whose chunks may not be
/// necessarily contiguous.  The arrangement of these chunks MSF the file
/// is described by some other metadata contained within the MSF file.  In
/// the case of a standard MSF Stream, the layout of the stream's blocks
/// is described by the MSF "directory", but in the case of the directory
/// itself, the layout is described by an array at a fixed location within
/// the MSF.  MappedBlockStream provides methods for reading from and writing
/// to one of these streams transparently, as if it were a contiguous sequence
/// of bytes.
class LLVM_ABI MappedBlockStream : public BinaryStream {
  friend class WritableMappedBlockStream;

public:
  /// Create a stream over an arbitrary MSF stream layout.
  ///
  /// \param BlockSize Size in bytes of each MSF block.
  /// \param Layout Layout describing which blocks make up the stream.
  /// \param MsfData Readable view of the underlying MSF file data.
  /// \param Allocator Allocator used for caches that outlive this stream.
  /// \returns A new MappedBlockStream for the given layout.
  static std::unique_ptr<MappedBlockStream>
  createStream(uint32_t BlockSize, const MSFStreamLayout &Layout,
               BinaryStreamRef MsfData, BumpPtrAllocator &Allocator);

  /// Create a stream for the MSF stream at the given directory index.
  ///
  /// \param Layout Full MSF file layout containing stream metadata.
  /// \param MsfData Readable view of the underlying MSF file data.
  /// \param StreamIndex Index of the stream within the MSF directory.
  /// \param Allocator Allocator used for caches that outlive this stream.
  /// \returns A new MappedBlockStream for the indexed stream.
  static std::unique_ptr<MappedBlockStream>
  createIndexedStream(const MSFLayout &Layout, BinaryStreamRef MsfData,
                      uint32_t StreamIndex, BumpPtrAllocator &Allocator);

  /// Create a stream over the free page map (FPM) of an MSF file.
  ///
  /// \param Layout Full MSF file layout containing FPM metadata.
  /// \param MsfData Readable view of the underlying MSF file data.
  /// \param Allocator Allocator used for caches that outlive this stream.
  /// \returns A new MappedBlockStream for the FPM.
  static std::unique_ptr<MappedBlockStream>
  createFpmStream(const MSFLayout &Layout, BinaryStreamRef MsfData,
                  BumpPtrAllocator &Allocator);

  /// Create a stream over the MSF directory.
  ///
  /// \param Layout Full MSF file layout containing directory metadata.
  /// \param MsfData Readable view of the underlying MSF file data.
  /// \param Allocator Allocator used for caches that outlive this stream.
  /// \returns A new MappedBlockStream for the directory.
  static std::unique_ptr<MappedBlockStream>
  createDirectoryStream(const MSFLayout &Layout, BinaryStreamRef MsfData,
                        BumpPtrAllocator &Allocator);

  /// Return the endianness of multi-byte values in this stream.
  ///
  /// \returns Little-endian byte order.
  llvm::endianness getEndian() const override {
    return llvm::endianness::little;
  }

  /// Read \p Size bytes starting at \p Offset into \p Buffer.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Size Number of bytes to read.
  /// \param Buffer Set to data owned by the stream covering the requested
  ///        range.
  /// \returns Success, or an error if the read fails.
  Error readBytes(uint64_t Offset, uint64_t Size,
                  ArrayRef<uint8_t> &Buffer) override;

  /// Read the longest contiguous chunk starting at \p Offset.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Buffer Set to the longest contiguous chunk starting at \p Offset.
  /// \returns Success, or an error if the read fails.
  Error readLongestContiguousChunk(uint64_t Offset,
                                   ArrayRef<uint8_t> &Buffer) override;

  /// Return the number of bytes of data in this stream.
  ///
  /// \returns The stream length in bytes.
  uint64_t getLength() override;

  /// Return the allocator used for cross-block read caches.
  ///
  /// \returns The bump allocator used by this stream.
  BumpPtrAllocator &getAllocator() { return Allocator; }

  /// Invalidate cached contiguous copies of discontiguous stream data.
  void invalidateCache();

  /// Return the size in bytes of each MSF block.
  ///
  /// \returns The MSF block size in bytes.
  uint32_t getBlockSize() const { return BlockSize; }

  /// Return the number of blocks that make up this stream.
  ///
  /// \returns The number of blocks in the stream layout.
  uint32_t getNumBlocks() const { return StreamLayout.Blocks.size(); }

  /// Return the logical length in bytes of this stream.
  ///
  /// \returns The logical stream length in bytes.
  uint32_t getStreamLength() const { return StreamLayout.Length; }

protected:
  /// Construct a MappedBlockStream for the given layout and MSF data.
  ///
  /// \param BlockSize Size in bytes of each MSF block.
  /// \param StreamLayout Layout describing which blocks make up the stream.
  /// \param MsfData Readable view of the underlying MSF file data.
  /// \param Allocator Allocator used for caches that outlive this stream.
  MappedBlockStream(uint32_t BlockSize, const MSFStreamLayout &StreamLayout,
                    BinaryStreamRef MsfData, BumpPtrAllocator &Allocator);

private:
  const MSFStreamLayout &getStreamLayout() const { return StreamLayout; }
  void fixCacheAfterWrite(uint64_t Offset, ArrayRef<uint8_t> Data) const;

  Error readBytes(uint64_t Offset, MutableArrayRef<uint8_t> Buffer);
  bool tryReadContiguously(uint64_t Offset, uint64_t Size,
                           ArrayRef<uint8_t> &Buffer);

  const uint32_t BlockSize;
  const MSFStreamLayout StreamLayout;
  BinaryStreamRef MsfData;

  using CacheEntry = MutableArrayRef<uint8_t>;

  // We just store the allocator by reference.  We use this to allocate
  // contiguous memory for things like arrays or strings that cross a block
  // boundary, and this memory is expected to outlive the stream.  For example,
  // someone could create a stream, read some stuff, then close the stream, and
  // we would like outstanding references to fields to remain valid since the
  // entire file is mapped anyway.  Because of that, the user must supply the
  // allocator to allocate broken records from.
  BumpPtrAllocator &Allocator;
  DenseMap<uint32_t, std::vector<CacheEntry>> CacheMap;
};

/// A writable BinaryStream view of a possibly discontiguous MSF stream.
class LLVM_ABI WritableMappedBlockStream : public WritableBinaryStream {
public:
  /// Create a writable stream over an arbitrary MSF stream layout.
  ///
  /// \param BlockSize Size in bytes of each MSF block.
  /// \param Layout Layout describing which blocks make up the stream.
  /// \param MsfData Writable view of the underlying MSF file data.
  /// \param Allocator Allocator used for caches that outlive this stream.
  /// \returns A new WritableMappedBlockStream for the given layout.
  static std::unique_ptr<WritableMappedBlockStream>
  createStream(uint32_t BlockSize, const MSFStreamLayout &Layout,
               WritableBinaryStreamRef MsfData, BumpPtrAllocator &Allocator);

  /// Create a writable stream for the MSF stream at the given directory index.
  ///
  /// \param Layout Full MSF file layout containing stream metadata.
  /// \param MsfData Writable view of the underlying MSF file data.
  /// \param StreamIndex Index of the stream within the MSF directory.
  /// \param Allocator Allocator used for caches that outlive this stream.
  /// \returns A new WritableMappedBlockStream for the indexed stream.
  static std::unique_ptr<WritableMappedBlockStream>
  createIndexedStream(const MSFLayout &Layout, WritableBinaryStreamRef MsfData,
                      uint32_t StreamIndex, BumpPtrAllocator &Allocator);

  /// Create a writable stream over the MSF directory.
  ///
  /// \param Layout Full MSF file layout containing directory metadata.
  /// \param MsfData Writable view of the underlying MSF file data.
  /// \param Allocator Allocator used for caches that outlive this stream.
  /// \returns A new WritableMappedBlockStream for the directory.
  static std::unique_ptr<WritableMappedBlockStream>
  createDirectoryStream(const MSFLayout &Layout,
                        WritableBinaryStreamRef MsfData,
                        BumpPtrAllocator &Allocator);

  /// Create a writable stream over the free page map (FPM) of an MSF file.
  ///
  /// \param Layout Full MSF file layout containing FPM metadata.
  /// \param MsfData Writable view of the underlying MSF file data.
  /// \param Allocator Allocator used for caches that outlive this stream.
  /// \param AltFpm If true, use the alternate FPM rather than the main FPM.
  /// \returns A new WritableMappedBlockStream for the FPM.
  static std::unique_ptr<WritableMappedBlockStream>
  createFpmStream(const MSFLayout &Layout, WritableBinaryStreamRef MsfData,
                  BumpPtrAllocator &Allocator, bool AltFpm = false);

  /// Return the endianness of multi-byte values in this stream.
  ///
  /// \returns Little-endian byte order.
  llvm::endianness getEndian() const override {
    return llvm::endianness::little;
  }

  /// Read \p Size bytes starting at \p Offset into \p Buffer.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Size Number of bytes to read.
  /// \param Buffer Set to data owned by the stream covering the requested
  ///        range.
  /// \returns Success, or an error if the read fails.
  Error readBytes(uint64_t Offset, uint64_t Size,
                  ArrayRef<uint8_t> &Buffer) override;

  /// Read the longest contiguous chunk starting at \p Offset.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Buffer Set to the longest contiguous chunk starting at \p Offset.
  /// \returns Success, or an error if the read fails.
  Error readLongestContiguousChunk(uint64_t Offset,
                                   ArrayRef<uint8_t> &Buffer) override;

  /// Return the number of bytes of data in this stream.
  ///
  /// \returns The stream length in bytes.
  uint64_t getLength() override;

  /// Write \p Buffer into the stream starting at \p Offset.
  ///
  /// \param Offset Byte offset into the stream at which to begin writing.
  /// \param Buffer Bytes to copy into the stream.
  /// \returns Success, or an error if the write fails.
  Error writeBytes(uint64_t Offset, ArrayRef<uint8_t> Buffer) override;

  /// Commit buffered writes to the underlying MSF backing store.
  ///
  /// \returns Success, or an error if the commit fails.
  Error commit() override;

  /// Return the layout describing which blocks make up this stream.
  ///
  /// \returns The MSF stream layout for this stream.
  const MSFStreamLayout &getStreamLayout() const {
    return ReadInterface.getStreamLayout();
  }

  /// Return the size in bytes of each MSF block.
  ///
  /// \returns The MSF block size in bytes.
  uint32_t getBlockSize() const { return ReadInterface.getBlockSize(); }

  /// Return the number of blocks that make up this stream.
  ///
  /// \returns The number of blocks in the stream layout.
  uint32_t getNumBlocks() const { return ReadInterface.getNumBlocks(); }

  /// Return the logical length in bytes of this stream.
  ///
  /// \returns The logical stream length in bytes.
  uint32_t getStreamLength() const { return ReadInterface.getStreamLength(); }

protected:
  /// Construct a WritableMappedBlockStream for the given layout and MSF data.
  ///
  /// \param BlockSize Size in bytes of each MSF block.
  /// \param StreamLayout Layout describing which blocks make up the stream.
  /// \param MsfData Writable view of the underlying MSF file data.
  /// \param Allocator Allocator used for caches that outlive this stream.
  WritableMappedBlockStream(uint32_t BlockSize,
                            const MSFStreamLayout &StreamLayout,
                            WritableBinaryStreamRef MsfData,
                            BumpPtrAllocator &Allocator);

private:
  MappedBlockStream ReadInterface;
  WritableBinaryStreamRef WriteInterface;
};

} // namespace msf
} // end namespace llvm

#endif // LLVM_DEBUGINFO_MSF_MAPPEDBLOCKSTREAM_H
