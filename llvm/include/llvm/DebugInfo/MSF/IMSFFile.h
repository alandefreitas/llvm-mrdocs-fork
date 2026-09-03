//===- IMSFFile.h - Abstract base class for an MSF file ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_MSF_IMSFFILE_H
#define LLVM_DEBUGINFO_MSF_IMSFFILE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {
/// Namespace for Microsoft Symbol File (MSF) support.
namespace msf {

/// Abstract interface for reading and writing an MSF file.
class IMSFFile {
public:
  /// Destroy an IMSFFile.
  virtual ~IMSFFile() = default;

  /// Return the size in bytes of each block in the MSF file.
  ///
  /// \returns The size in bytes of each block.
  virtual uint32_t getBlockSize() const = 0;

  /// Return the total number of blocks in the MSF file.
  ///
  /// \returns The total number of blocks in the file.
  virtual uint32_t getBlockCount() const = 0;

  /// Return the number of streams in the MSF file.
  ///
  /// \returns The number of streams in the file.
  virtual uint32_t getNumStreams() const = 0;

  /// Return the size in bytes of the stream at \p StreamIndex.
  ///
  /// \param StreamIndex Index of the stream whose size is requested.
  /// \returns The size in bytes of the stream.
  virtual uint32_t getStreamByteSize(uint32_t StreamIndex) const = 0;

  /// Return the list of block indices that make up the stream at
  /// \p StreamIndex.
  ///
  /// \param StreamIndex Index of the stream whose block list is requested.
  /// \returns The block indices that comprise the stream.
  virtual ArrayRef<support::ulittle32_t>
  getStreamBlockList(uint32_t StreamIndex) const = 0;

  /// Return a view of \p NumBytes of data from the block at \p BlockIndex.
  ///
  /// \param BlockIndex Index of the block to read from.
  /// \param NumBytes Number of bytes to read from the start of the block.
  /// \returns The block data on success, or an error if the read fails.
  virtual Expected<ArrayRef<uint8_t>> getBlockData(uint32_t BlockIndex,
                                                   uint32_t NumBytes) const = 0;

  /// Write \p Data into the block at \p BlockIndex starting at \p Offset.
  ///
  /// \param BlockIndex Index of the block to write to.
  /// \param Offset Byte offset within the block at which to begin writing.
  /// \param Data Bytes to write into the block.
  /// \returns Success, or an error if the write fails.
  virtual Error setBlockData(uint32_t BlockIndex, uint32_t Offset,
                             ArrayRef<uint8_t> Data) const = 0;
};

} // end namespace msf
} // end namespace llvm

#endif // LLVM_DEBUGINFO_MSF_IMSFFILE_H
