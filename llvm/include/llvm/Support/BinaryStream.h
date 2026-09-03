//===- BinaryStream.h - Base interface for a stream of data -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BINARYSTREAM_H
#define LLVM_SUPPORT_BINARYSTREAM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/Support/BinaryStreamError.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

/// Properties of a BinaryStream implementation.
enum BinaryStreamFlags {
  /// No special stream properties.
  BSF_None = 0,
  /// Stream supports writing.
  BSF_Write = 1,
  /// Writing can occur at offset == length.
  BSF_Append = 2,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ BSF_Append)
};

/// An interface for accessing data in a stream-like format without copying.
///
/// Instead of specifying a buffer in which to copy data on a read, the API
/// returns an ArrayRef to data owned by the stream's implementation.  Since
/// implementations may not necessarily store data in a single contiguous
/// buffer (or even in memory at all), in such cases a it may be necessary for
/// an implementation to cache such a buffer so that it can return it.
class BinaryStream {
public:
  /// Destroy a BinaryStream.
  virtual ~BinaryStream() = default;

  /// Return the endianness of multi-byte values in this stream.
  ///
  /// \returns the endianness of multi-byte values in this stream.
  virtual llvm::endianness getEndian() const = 0;

  /// Given an offset into the stream and a number of bytes, attempt to
  /// read the bytes and set the output ArrayRef to point to data owned by the
  /// stream.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Size Number of bytes to read.
  /// \param Buffer Set to data owned by the stream covering the requested
  ///        range.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// an appropriate error code.
  virtual Error readBytes(uint64_t Offset, uint64_t Size,
                          ArrayRef<uint8_t> &Buffer) = 0;

  /// Given an offset into the stream, read as much as possible without
  /// copying any data.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Buffer Set to the longest contiguous chunk starting at \p Offset.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// an appropriate error code.
  virtual Error readLongestContiguousChunk(uint64_t Offset,
                                           ArrayRef<uint8_t> &Buffer) = 0;

  /// Return the number of bytes of data in this stream.
  ///
  /// \returns the number of bytes of data in this stream.
  virtual uint64_t getLength() = 0;

  /// Return the properties of this stream.
  ///
  /// \returns the BinaryStreamFlags for this stream.
  virtual BinaryStreamFlags getFlags() const { return BSF_None; }

protected:
  /// Check that \p Offset and \p DataSize form a valid read range.
  ///
  /// \param Offset Byte offset into the stream at which the read would begin.
  /// \param DataSize Number of bytes that would be read.
  ///
  /// \returns a success error code if the read range is valid, otherwise an
  /// appropriate error code.
  Error checkOffsetForRead(uint64_t Offset, uint64_t DataSize) {
    if (Offset > getLength())
      return make_error<BinaryStreamError>(stream_error_code::invalid_offset);
    if (getLength() < DataSize + Offset)
      return make_error<BinaryStreamError>(stream_error_code::stream_too_short);
    return Error::success();
  }
};

/// A BinaryStream that supports both reading and writing.
///
/// Note that writing to a BinaryStream always necessitates copying from the
/// input buffer to the stream's backing store.  Streams are assumed to be
/// buffered so that to be portable it is necessary to call commit() on the
/// stream when all data has been written.
class WritableBinaryStream : public BinaryStream {
public:
  /// Destroy a WritableBinaryStream.
  ~WritableBinaryStream() override = default;

  /// Write bytes into the stream at the given offset.
  ///
  /// This will always necessitate a copy.  Cannot shrink or grow the stream,
  /// only writes into existing allocated space.
  ///
  /// \param Offset Byte offset into the stream at which to begin writing.
  /// \param Data Bytes to copy into the stream.
  ///
  /// \returns a success error code if the data could be written, otherwise an
  /// appropriate error code.
  virtual Error writeBytes(uint64_t Offset, ArrayRef<uint8_t> Data) = 0;

  /// For buffered streams, commits changes to the backing store.
  ///
  /// \returns a success error code if the commit succeeded, otherwise an
  /// appropriate error code.
  virtual Error commit() = 0;

  /// Return the properties of this stream.
  ///
  /// \returns the BinaryStreamFlags for this writable stream.
  BinaryStreamFlags getFlags() const override { return BSF_Write; }

protected:
  /// Check that \p Offset and \p DataSize form a valid write range.
  ///
  /// \param Offset Byte offset into the stream at which the write would begin.
  /// \param DataSize Number of bytes that would be written.
  ///
  /// \returns a success error code if the write range is valid, otherwise an
  /// appropriate error code.
  Error checkOffsetForWrite(uint64_t Offset, uint64_t DataSize) {
    if (!(getFlags() & BSF_Append))
      return checkOffsetForRead(Offset, DataSize);

    if (Offset > getLength())
      return make_error<BinaryStreamError>(stream_error_code::invalid_offset);
    return Error::success();
  }
};

} // end namespace llvm

#endif // LLVM_SUPPORT_BINARYSTREAM_H
