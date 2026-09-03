//===- MSFBuilder.h - MSF Directory & Metadata Builder ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_MSF_MSFBUILDER_H
#define LLVM_DEBUGINFO_MSF_MSFBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <utility>
#include <vector>

namespace llvm {
class FileBufferByteStream;
namespace msf {

struct MSFLayout;

/// Builds the directory and metadata layout of an MSF file.
class MSFBuilder {
public:
  /// Create a new `MSFBuilder`.
  ///
  /// \param Allocator The bump-pointer allocator used for layout storage.
  ///
  /// \param BlockSize The internal block size used by the PDB file.  See
  /// isValidBlockSize() for a list of valid block sizes.
  ///
  /// \param MinBlockCount Causes the builder to reserve up front space for
  /// at least `MinBlockCount` blocks.  This is useful when using `MSFBuilder`
  /// to read an existing MSF that you want to write back out later.  The
  /// original MSF file's SuperBlock contains the exact number of blocks used
  /// by the file, so is a good hint as to how many blocks the new MSF file
  /// will contain.  Furthermore, it is actually necessary in this case.  To
  /// preserve stability of the file's layout, it is helpful to try to keep
  /// all streams mapped to their original block numbers.  To ensure that this
  /// is possible, space for all blocks must be allocated beforehand so that
  /// streams can be assigned to them.
  ///
  /// \param CanGrow If true, any operation which results in an attempt to
  /// locate a free block when all available blocks have been exhausted will
  /// allocate a new block, thereby growing the size of the final MSF file.
  /// When false, any such attempt will result in an error.  This is especially
  /// useful in testing scenarios when you know your test isn't going to do
  /// anything to increase the size of the file, so having an Error returned if
  /// it were to happen would catch a programming error
  ///
  /// \returns an llvm::Error representing whether the operation succeeded or
  /// failed.  Currently the only way this can fail is if an invalid block size
  /// is specified, or `MinBlockCount` does not leave enough room for the
  /// mandatory reserved blocks required by an MSF file.
  LLVM_ABI static Expected<MSFBuilder> create(BumpPtrAllocator &Allocator,
                                              uint32_t BlockSize,
                                              uint32_t MinBlockCount = 0,
                                              bool CanGrow = true);

  /// Request the block map to be at a specific block address.  This is useful
  /// when editing a MSF and you want the layout to be as stable as possible.
  ///
  /// \param Addr The block index at which to place the block map.
  ///
  /// \returns Success, or an error if the address cannot be reserved.
  LLVM_ABI Error setBlockMapAddr(uint32_t Addr);

  /// Hint which blocks should hold the MSF directory.
  ///
  /// \param DirBlocks The block indices to reserve for the directory.
  ///
  /// \returns Success, or an error if the blocks cannot be reserved.
  LLVM_ABI Error setDirectoryBlocksHint(ArrayRef<uint32_t> DirBlocks);

  /// Set the free page map block index written into the SuperBlock.
  ///
  /// \param Fpm The free page map block index.
  LLVM_ABI void setFreePageMap(uint32_t Fpm);

  /// Set the SuperBlock Unknown1 field.
  ///
  /// \param Unk1 The value to store in the SuperBlock Unknown1 field.
  LLVM_ABI void setUnknown1(uint32_t Unk1);

  /// Add a stream that occupies a specific list of blocks.
  ///
  /// This is useful when reading a MSF file and you want a particular stream
  /// to occupy the original set of blocks.  If the given blocks are already
  /// allocated, or if the number of blocks specified is incorrect for the
  /// given stream size, this function will return an Error.
  ///
  /// \param Size The size in bytes of the stream to add.
  /// \param Blocks The block indices the stream should occupy.
  ///
  /// \returns The stream index on success, or an error if the blocks cannot be
  /// assigned.
  LLVM_ABI Expected<uint32_t> addStream(uint32_t Size,
                                        ArrayRef<uint32_t> Blocks);

  /// Add a stream that occupies any available blocks.
  ///
  /// This is useful when building a new PDB file from scratch and you don't
  /// care what blocks a stream occupies but you just want it to work.
  ///
  /// \param Size The size in bytes of the stream to add.
  ///
  /// \returns The stream index on success, or an error if allocation fails.
  LLVM_ABI Expected<uint32_t> addStream(uint32_t Size);

  /// Update the size of an existing stream.
  ///
  /// This will allocate or deallocate blocks as needed to match the requested
  /// size.  This can fail if `CanGrow` was set to false when initializing the
  /// `MSFBuilder`.
  ///
  /// \param Idx The index of the stream to resize.
  /// \param Size The new size in bytes of the stream.
  ///
  /// \returns Success, or an error if the stream cannot be resized.
  LLVM_ABI Error setStreamSize(uint32_t Idx, uint32_t Size);

  /// Get the total number of streams in the MSF layout.  This should return 1
  /// for every call to `addStream`.
  ///
  /// \returns The number of streams currently in the layout.
  LLVM_ABI uint32_t getNumStreams() const;

  /// Get the size of a stream by index.
  ///
  /// \param StreamIdx The index of the stream whose size is requested.
  ///
  /// \returns The size in bytes of the stream.
  LLVM_ABI uint32_t getStreamSize(uint32_t StreamIdx) const;

  /// Get the list of blocks allocated to a particular stream.
  ///
  /// \param StreamIdx The index of the stream whose blocks are requested.
  ///
  /// \returns The block indices that comprise the stream.
  LLVM_ABI ArrayRef<uint32_t> getStreamBlocks(uint32_t StreamIdx) const;

  /// Get the total number of blocks that will be allocated to actual data in
  /// this MSF file.
  ///
  /// \returns The number of used blocks.
  LLVM_ABI uint32_t getNumUsedBlocks() const;

  /// Get the total number of blocks that exist in the MSF file but are not
  /// allocated to any valid data.
  ///
  /// \returns The number of free blocks.
  LLVM_ABI uint32_t getNumFreeBlocks() const;

  /// Get the total number of blocks in the MSF file.  In practice this is equal
  /// to `getNumUsedBlocks() + getNumFreeBlocks()`.
  ///
  /// \returns The total block count.
  LLVM_ABI uint32_t getTotalBlockCount() const;

  /// Check whether a particular block is allocated or free.
  ///
  /// \param Idx The block index to query.
  ///
  /// \returns True if the block is free, false if it is allocated.
  LLVM_ABI bool isBlockFree(uint32_t Idx) const;

  /// Finalize the layout and build the headers and structures that describe the
  /// MSF layout and can be written directly to the MSF file.
  ///
  /// \returns The finalized MSF layout on success, or an error on failure.
  LLVM_ABI Expected<MSFLayout> generateLayout();

  /// Write the MSF layout to the underlying file.
  ///
  /// \param Path The path of the file to write.
  /// \param Layout On success, filled with the finalized MSF layout.
  ///
  /// \returns A writable byte stream for the file on success, or an error on
  /// failure.
  LLVM_ABI Expected<FileBufferByteStream> commit(StringRef Path,
                                                 MSFLayout &Layout);

  /// Return the bump-pointer allocator used by this builder.
  ///
  /// \returns The bump-pointer allocator used by this builder.
  BumpPtrAllocator &getAllocator() { return Allocator; }

private:
  MSFBuilder(uint32_t BlockSize, uint32_t MinBlockCount, bool CanGrow,
             BumpPtrAllocator &Allocator);

  Error allocateBlocks(uint32_t NumBlocks, MutableArrayRef<uint32_t> Blocks);
  uint32_t computeDirectoryByteSize() const;

  using BlockList = std::vector<uint32_t>;

  BumpPtrAllocator &Allocator;

  bool IsGrowable;
  uint32_t FreePageMap;
  uint32_t Unknown1 = 0;
  uint32_t BlockSize;
  uint32_t BlockMapAddr;
  BitVector FreeBlocks;
  std::vector<uint32_t> DirectoryBlocks;
  std::vector<std::pair<uint32_t, BlockList>> StreamData;
};

} // end namespace msf
} // end namespace llvm

#endif // LLVM_DEBUGINFO_MSF_MSFBUILDER_H
