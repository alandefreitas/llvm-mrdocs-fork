//===- BinaryByteStream.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
// A BinaryStream which stores data in a single continguous memory buffer.
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BINARYBYTESTREAM_H
#define LLVM_SUPPORT_BINARYBYTESTREAM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/BinaryStream.h"
#include "llvm/Support/BinaryStreamError.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstdint>
#include <cstring>
#include <memory>

namespace llvm {

/// BinaryStream backed by a single contiguous buffer that never copies on read.
///
/// BinaryByteStream does not own the underlying buffer.
class BinaryByteStream : public BinaryStream {
public:
  /// Construct an empty BinaryByteStream.
  BinaryByteStream() = default;

  /// Construct a BinaryByteStream over the bytes in \p Data.
  ///
  /// \param Data Contiguous bytes to expose as the stream contents.
  /// \param Endian Endianness of multi-byte values in the stream.
  BinaryByteStream(ArrayRef<uint8_t> Data, llvm::endianness Endian)
      : Endian(Endian), Data(Data) {}

  /// Construct a BinaryByteStream over the bytes of string \p Data.
  ///
  /// \param Data String whose bytes become the stream contents.
  /// \param Endian Endianness of multi-byte values in the stream.
  BinaryByteStream(StringRef Data, llvm::endianness Endian)
      : Endian(Endian), Data(Data.bytes_begin(), Data.bytes_end()) {}

  /// Return the endianness of multi-byte values in this stream.
  ///
  /// \returns The endianness of multi-byte values in this stream.
  llvm::endianness getEndian() const override { return Endian; }

  /// Read \p Size bytes starting at \p Offset without copying.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Size Number of bytes to read.
  /// \param Buffer Set to the requested slice of the underlying buffer.
  ///
  /// \returns Error::success() on success, or an error if the range is invalid.
  Error readBytes(uint64_t Offset, uint64_t Size,
                  ArrayRef<uint8_t> &Buffer) override {
    if (auto EC = checkOffsetForRead(Offset, Size))
      return EC;
    Buffer = Data.slice(Offset, Size);
    return Error::success();
  }

  /// Read the longest contiguous chunk starting at \p Offset without copying.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Buffer Set to the remaining contiguous bytes from \p Offset.
  ///
  /// \returns Error::success() on success, or an error if \p Offset is invalid.
  Error readLongestContiguousChunk(uint64_t Offset,
                                   ArrayRef<uint8_t> &Buffer) override {
    if (auto EC = checkOffsetForRead(Offset, 1))
      return EC;
    Buffer = Data.slice(Offset);
    return Error::success();
  }

  /// Return the number of bytes in this stream.
  ///
  /// \returns The number of bytes in this stream.
  uint64_t getLength() override { return Data.size(); }

  /// Return the underlying contiguous data buffer.
  ///
  /// \returns The underlying contiguous data buffer.
  ArrayRef<uint8_t> data() const { return Data; }

  /// Return the underlying buffer as a StringRef.
  ///
  /// \returns The underlying buffer as a StringRef.
  StringRef str() const {
    const char *CharData = reinterpret_cast<const char *>(Data.data());
    return StringRef(CharData, Data.size());
  }

protected:
  /// Endianness of multi-byte values in the stream.
  llvm::endianness Endian;

  /// Contiguous bytes that back this stream.
  ArrayRef<uint8_t> Data;
};

/// BinaryStream whose data is owned by an llvm MemoryBuffer.
///
/// MemoryBufferByteStream owns the MemoryBuffer in question. As with
/// BinaryByteStream, reading from a MemoryBufferByteStream will never cause a
/// copy.
class MemoryBufferByteStream : public BinaryByteStream {
public:
  /// Construct a stream that owns \p Buffer.
  ///
  /// \param Buffer MemoryBuffer providing the stream bytes; ownership is taken.
  /// \param Endian Endianness of multi-byte values in the stream.
  MemoryBufferByteStream(std::unique_ptr<MemoryBuffer> Buffer,
                         llvm::endianness Endian)
      : BinaryByteStream(Buffer->getBuffer(), Endian),
        MemBuffer(std::move(Buffer)) {}

  /// Owned MemoryBuffer that backs this stream.
  std::unique_ptr<MemoryBuffer> MemBuffer;
};

/// Writable BinaryStream backed by a single contiguous mutable buffer.
///
/// As with BinaryByteStream, the mutable version also guarantees that no read
/// operation will ever incur a copy, and similarly it does not own the
/// underlying buffer.
class MutableBinaryByteStream : public WritableBinaryStream {
public:
  /// Construct an empty MutableBinaryByteStream.
  MutableBinaryByteStream() = default;

  /// Construct a MutableBinaryByteStream over the bytes in \p Data.
  ///
  /// \param Data Contiguous mutable bytes to expose as the stream contents.
  /// \param Endian Endianness of multi-byte values in the stream.
  MutableBinaryByteStream(MutableArrayRef<uint8_t> Data,
                          llvm::endianness Endian)
      : Data(Data), ImmutableStream(Data, Endian) {}

  /// Return the endianness of multi-byte values in this stream.
  ///
  /// \returns The endianness of multi-byte values in this stream.
  llvm::endianness getEndian() const override {
    return ImmutableStream.getEndian();
  }

  /// Read \p Size bytes starting at \p Offset without copying.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Size Number of bytes to read.
  /// \param Buffer Set to the requested slice of the underlying buffer.
  ///
  /// \returns Error::success() on success, or an error if the range is invalid.
  Error readBytes(uint64_t Offset, uint64_t Size,
                  ArrayRef<uint8_t> &Buffer) override {
    return ImmutableStream.readBytes(Offset, Size, Buffer);
  }

  /// Read the longest contiguous chunk starting at \p Offset without copying.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Buffer Set to the remaining contiguous bytes from \p Offset.
  ///
  /// \returns Error::success() on success, or an error if \p Offset is invalid.
  Error readLongestContiguousChunk(uint64_t Offset,
                                   ArrayRef<uint8_t> &Buffer) override {
    return ImmutableStream.readLongestContiguousChunk(Offset, Buffer);
  }

  /// Return the number of bytes in this stream.
  ///
  /// \returns The number of bytes in this stream.
  uint64_t getLength() override { return ImmutableStream.getLength(); }

  /// Write \p Buffer into the stream at \p Offset without resizing.
  ///
  /// This always copies into existing allocated space and cannot shrink or grow
  /// the stream.
  ///
  /// \param Offset Byte offset at which to begin writing.
  /// \param Buffer Bytes to copy into the stream.
  ///
  /// \returns Error::success() on success, or an error if the range is invalid.
  Error writeBytes(uint64_t Offset, ArrayRef<uint8_t> Buffer) override {
    if (Buffer.empty())
      return Error::success();

    if (auto EC = checkOffsetForWrite(Offset, Buffer.size()))
      return EC;

    uint8_t *DataPtr = const_cast<uint8_t *>(Data.data());
    ::memcpy(DataPtr + Offset, Buffer.data(), Buffer.size());
    return Error::success();
  }

  /// Commit buffered changes; always succeeds for this in-memory stream.
  ///
  /// \returns Error::success().
  Error commit() override { return Error::success(); }

  /// Return the underlying contiguous mutable data buffer.
  ///
  /// \returns The underlying contiguous mutable data buffer.
  MutableArrayRef<uint8_t> data() const { return Data; }

private:
  MutableArrayRef<uint8_t> Data;
  BinaryByteStream ImmutableStream;
};

/// An implementation of WritableBinaryStream which can write at its end
/// causing the underlying data to grow.  This class owns the underlying data.
class AppendingBinaryByteStream : public WritableBinaryStream {
  std::vector<uint8_t> Data;
  llvm::endianness Endian = llvm::endianness::little;

public:
  /// Construct an empty AppendingBinaryByteStream with little endianness.
  AppendingBinaryByteStream() = default;

  /// Construct an empty AppendingBinaryByteStream with the given endianness.
  ///
  /// \param Endian Endianness of multi-byte values in the stream.
  AppendingBinaryByteStream(llvm::endianness Endian) : Endian(Endian) {}

  /// Clear all bytes from the underlying buffer.
  void clear() { Data.clear(); }

  /// Return the endianness of multi-byte values in this stream.
  ///
  /// \returns The endianness of multi-byte values in this stream.
  llvm::endianness getEndian() const override { return Endian; }

  /// Read \p Size bytes starting at \p Offset without copying.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Size Number of bytes to read.
  /// \param Buffer Set to the requested slice of the underlying buffer.
  ///
  /// \returns Error::success() on success, or an error if the range is invalid.
  Error readBytes(uint64_t Offset, uint64_t Size,
                  ArrayRef<uint8_t> &Buffer) override {
    if (auto EC = checkOffsetForWrite(Offset, Buffer.size()))
      return EC;

    Buffer = ArrayRef(Data).slice(Offset, Size);
    return Error::success();
  }

  /// Insert \p Bytes into the stream at \p Offset, shifting existing data.
  ///
  /// \param Offset Byte offset at which to insert.
  /// \param Bytes Bytes to insert into the underlying buffer.
  void insert(uint64_t Offset, ArrayRef<uint8_t> Bytes) {
    Data.insert(Data.begin() + Offset, Bytes.begin(), Bytes.end());
  }

  /// Read the longest contiguous chunk starting at \p Offset without copying.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Buffer Set to the remaining contiguous bytes from \p Offset.
  ///
  /// \returns Error::success() on success, or an error if \p Offset is invalid.
  Error readLongestContiguousChunk(uint64_t Offset,
                                   ArrayRef<uint8_t> &Buffer) override {
    if (auto EC = checkOffsetForWrite(Offset, 1))
      return EC;

    Buffer = ArrayRef(Data).slice(Offset);
    return Error::success();
  }

  /// Return the number of bytes in this stream.
  ///
  /// \returns The number of bytes in this stream.
  uint64_t getLength() override { return Data.size(); }

  /// Write \p Buffer into the stream at \p Offset, growing if needed.
  ///
  /// Writing at the current length appends. Writing beyond the current length
  /// is an error because intermediate uninitialized bytes would be undefined.
  ///
  /// \param Offset Byte offset at which to begin writing.
  /// \param Buffer Bytes to copy into the stream.
  ///
  /// \returns Error::success() on success, or an error if \p Offset is beyond
  /// the current length.
  Error writeBytes(uint64_t Offset, ArrayRef<uint8_t> Buffer) override {
    if (Buffer.empty())
      return Error::success();

    // This is well-defined for any case except where offset is strictly
    // greater than the current length.  If offset is equal to the current
    // length, we can still grow.  If offset is beyond the current length, we
    // would have to decide how to deal with the intermediate uninitialized
    // bytes.  So we punt on that case for simplicity and just say it's an
    // error.
    if (Offset > getLength())
      return make_error<BinaryStreamError>(stream_error_code::invalid_offset);

    uint64_t RequiredSize = Offset + Buffer.size();
    if (RequiredSize > Data.size())
      Data.resize(RequiredSize);

    ::memcpy(Data.data() + Offset, Buffer.data(), Buffer.size());
    return Error::success();
  }

  /// Commit buffered changes; always succeeds for this in-memory stream.
  ///
  /// \returns Error::success().
  Error commit() override { return Error::success(); }

  /// Return the properties of this stream.
  ///
  /// \returns The stream flags, including BSF_Write and BSF_Append.
  BinaryStreamFlags getFlags() const override { return BSF_Write | BSF_Append; }

  /// Return the underlying contiguous mutable data buffer.
  ///
  /// \returns The underlying contiguous mutable data buffer.
  MutableArrayRef<uint8_t> data() { return Data; }
};

/// An implementation of WritableBinaryStream backed by an llvm
/// FileOutputBuffer.
class FileBufferByteStream : public WritableBinaryStream {
private:
  class StreamImpl : public MutableBinaryByteStream {
  public:
    StreamImpl(std::unique_ptr<FileOutputBuffer> Buffer,
               llvm::endianness Endian)
        : MutableBinaryByteStream(
              MutableArrayRef<uint8_t>(Buffer->getBufferStart(),
                                       Buffer->getBufferEnd()),
              Endian),
          FileBuffer(std::move(Buffer)) {}

    Error commit() override {
      if (FileBuffer->commit())
        return make_error<BinaryStreamError>(
            stream_error_code::filesystem_error);
      return Error::success();
    }

    /// Returns a pointer to the start of the buffer.
    uint8_t *getBufferStart() const { return FileBuffer->getBufferStart(); }

    /// Returns a pointer to the end of the buffer.
    uint8_t *getBufferEnd() const { return FileBuffer->getBufferEnd(); }

  private:
    std::unique_ptr<FileOutputBuffer> FileBuffer;
  };

public:
  /// Construct a stream backed by owned FileOutputBuffer \p Buffer.
  ///
  /// \param Buffer FileOutputBuffer providing the writable bytes; ownership is
  /// taken.
  /// \param Endian Endianness of multi-byte values in the stream.
  FileBufferByteStream(std::unique_ptr<FileOutputBuffer> Buffer,
                       llvm::endianness Endian)
      : Impl(std::move(Buffer), Endian) {}

  /// Return the endianness of multi-byte values in this stream.
  ///
  /// \returns The endianness of multi-byte values in this stream.
  llvm::endianness getEndian() const override { return Impl.getEndian(); }

  /// Read \p Size bytes starting at \p Offset without copying.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Size Number of bytes to read.
  /// \param Buffer Set to the requested slice of the underlying buffer.
  ///
  /// \returns Error::success() on success, or an error if the range is invalid.
  Error readBytes(uint64_t Offset, uint64_t Size,
                  ArrayRef<uint8_t> &Buffer) override {
    return Impl.readBytes(Offset, Size, Buffer);
  }

  /// Read the longest contiguous chunk starting at \p Offset without copying.
  ///
  /// \param Offset Byte offset into the stream at which to begin reading.
  /// \param Buffer Set to the remaining contiguous bytes from \p Offset.
  ///
  /// \returns Error::success() on success, or an error if \p Offset is invalid.
  Error readLongestContiguousChunk(uint64_t Offset,
                                   ArrayRef<uint8_t> &Buffer) override {
    return Impl.readLongestContiguousChunk(Offset, Buffer);
  }

  /// Return the number of bytes in this stream.
  ///
  /// \returns The number of bytes in this stream.
  uint64_t getLength() override { return Impl.getLength(); }

  /// Write \p Data into the stream at \p Offset without resizing.
  ///
  /// \param Offset Byte offset at which to begin writing.
  /// \param Data Bytes to copy into the stream.
  ///
  /// \returns Error::success() on success, or an error if the range is invalid.
  Error writeBytes(uint64_t Offset, ArrayRef<uint8_t> Data) override {
    return Impl.writeBytes(Offset, Data);
  }

  /// Commit buffered changes to the backing FileOutputBuffer.
  ///
  /// \returns Error::success() on success, or a filesystem error on failure.
  Error commit() override { return Impl.commit(); }

  /// Returns a pointer to the start of the buffer.
  ///
  /// \returns A pointer to the start of the buffer.
  uint8_t *getBufferStart() const { return Impl.getBufferStart(); }

  /// Returns a pointer to the end of the buffer.
  ///
  /// \returns A pointer to the end of the buffer.
  uint8_t *getBufferEnd() const { return Impl.getBufferEnd(); }

private:
  StreamImpl Impl;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_BINARYBYTESTREAM_H
