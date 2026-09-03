//===- BinaryStreamReader.h - Reads objects from a binary stream *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BINARYSTREAMREADER_H
#define LLVM_SUPPORT_BINARYSTREAMREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <type_traits>

namespace llvm {

/// Read-only accessor for a BinaryStream with bounds-checked typed reads.
///
/// Provides bounds checking and helpers for reading certain common data types
/// such as null-terminated strings, integers in various flavors of endianness,
/// etc. Can be subclassed to provide reading of custom datatypes, although no
/// methods are overridable.
class BinaryStreamReader {
public:
  /// Construct an empty BinaryStreamReader with no underlying stream.
  BinaryStreamReader() = default;

  /// Construct a BinaryStreamReader over the given stream reference.
  ///
  /// \param Ref Stream reference to read from.
  LLVM_ABI explicit BinaryStreamReader(BinaryStreamRef Ref);

  /// Construct a BinaryStreamReader over the given stream.
  ///
  /// \param Stream Stream to read from.
  LLVM_ABI explicit BinaryStreamReader(BinaryStream &Stream);

  /// Construct a BinaryStreamReader over a contiguous byte buffer.
  ///
  /// \param Data Contiguous bytes to expose as the stream contents.
  /// \param Endian Endianness of multi-byte values in the stream.
  LLVM_ABI explicit BinaryStreamReader(ArrayRef<uint8_t> Data,
                                       llvm::endianness Endian);

  /// Construct a BinaryStreamReader over the bytes of a string.
  ///
  /// \param Data String whose bytes become the stream contents.
  /// \param Endian Endianness of multi-byte values in the stream.
  LLVM_ABI explicit BinaryStreamReader(StringRef Data, llvm::endianness Endian);

  /// Copy-construct a BinaryStreamReader from another reader.
  ///
  /// \param Other Reader to copy.
  BinaryStreamReader(const BinaryStreamReader &Other) = default;

  /// Copy-assign from another BinaryStreamReader.
  ///
  /// \param Other Reader to copy from.
  /// \returns A reference to this reader.
  BinaryStreamReader &operator=(const BinaryStreamReader &Other) = default;

  /// Destroy this BinaryStreamReader.
  virtual ~BinaryStreamReader() = default;

  /// Read the longest contiguous chunk at the current offset without copying.
  ///
  /// Read as much as possible from the underlying stream at the current offset
  /// without invoking a copy, and set \p Buffer to the resulting data slice.
  /// Updates the stream's offset to point after the newly read data.
  ///
  /// \param Buffer Set to the resulting contiguous data slice.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  LLVM_ABI Error readLongestContiguousChunk(ArrayRef<uint8_t> &Buffer);

  /// Read a fixed number of bytes at the current offset into a buffer slice.
  ///
  /// Read \p Size bytes from the underlying stream at the current offset and
  /// set \p Buffer to the resulting data slice. Whether a copy occurs depends
  /// on the implementation of the underlying stream. Updates the stream's
  /// offset to point after the newly read data.
  ///
  /// \param Buffer Set to the resulting data slice.
  /// \param Size Number of bytes to read.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  LLVM_ABI Error readBytes(ArrayRef<uint8_t> &Buffer, uint32_t Size);

  /// Read an integer of the stream's endianness into \p Dest.
  ///
  /// The data is always copied from the stream's underlying buffer into
  /// \p Dest. Updates the stream's offset to point after the newly read data.
  ///
  /// \param Dest Set to the integer value read from the stream.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T> Error readInteger(T &Dest) {
    static_assert(std::is_integral_v<T>,
                  "Cannot call readInteger with non-integral value!");

    ArrayRef<uint8_t> Bytes;
    if (auto EC = readBytes(Bytes, sizeof(T)))
      return EC;

    Dest = llvm::support::endian::read<T>(Bytes.data(), Stream.getEndian());
    return Error::success();
  }

  /// Read an enum value by reading its underlying integral type.
  ///
  /// \param Dest Set to the enum value read from the stream.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T> Error readEnum(T &Dest) {
    static_assert(std::is_enum<T>::value,
                  "Cannot call readEnum with non-enum value!");
    std::underlying_type_t<T> N;
    if (auto EC = readInteger(N))
      return EC;
    Dest = static_cast<T>(N);
    return Error::success();
  }

  /// Read an unsigned LEB128 encoded value.
  ///
  /// \param Dest Set to the decoded unsigned value.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  LLVM_ABI Error readULEB128(uint64_t &Dest);

  /// Read a signed LEB128 encoded value.
  ///
  /// \param Dest Set to the decoded signed value.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  LLVM_ABI Error readSLEB128(int64_t &Dest);

  /// Read a null-terminated string into \p Dest.
  ///
  /// Whether a copy occurs depends on the implementation of the underlying
  /// stream. Updates the stream's offset to point after the newly read data.
  ///
  /// \param Dest Set to the null-terminated string read from the stream.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  LLVM_ABI Error readCString(StringRef &Dest);

  /// Read a null-terminated UTF16 string into \p Dest.
  ///
  /// \param Dest Set to the null-terminated UTF16 string read from the stream.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  LLVM_ABI Error readWideString(ArrayRef<UTF16> &Dest);

  /// Read a fixed-length string of \p Length bytes into \p Dest.
  ///
  /// Whether a copy occurs depends on the implementation of the underlying
  /// stream. Updates the stream's offset to point after the newly read data.
  ///
  /// \param Dest Set to the string of \p Length bytes read from the stream.
  /// \param Length Number of bytes to read.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  LLVM_ABI Error readFixedString(StringRef &Dest, uint32_t Length);

  /// Read the remainder of the stream into a BinaryStreamRef.
  ///
  /// This is equivalent to calling getUnderlyingStream().slice(Offset).
  /// Updates the stream's offset to point to the end of the stream. Never
  /// causes a copy.
  ///
  /// \param Ref Set to a reference covering the remainder of the stream.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  LLVM_ABI Error readStreamRef(BinaryStreamRef &Ref);

  /// Read \p Length bytes into a BinaryStreamRef.
  ///
  /// This is equivalent to calling getUnderlyingStream().slice(Offset, Length).
  /// Updates the stream's offset to point after the newly read object. Never
  /// causes a copy.
  ///
  /// \param Ref Set to a reference covering the requested bytes.
  /// \param Length Number of bytes to include in the reference.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  LLVM_ABI Error readStreamRef(BinaryStreamRef &Ref, uint32_t Length);

  /// Read \p Length bytes into a BinarySubstreamRef.
  ///
  /// This is equivalent to calling getUnderlyingStream().slice(Offset, Length).
  /// Updates the stream's offset to point after the newly read object. Never
  /// causes a copy.
  ///
  /// \param Ref Set to a substream reference covering the requested bytes.
  /// \param Length Number of bytes to include in the reference.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  LLVM_ABI Error readSubstream(BinarySubstreamRef &Ref, uint32_t Length);

  /// Read a pointer to an object of type T from the stream as if by memcpy.
  ///
  /// Store the result into \p Dest. It is up to the caller to ensure that
  /// objects of type T can be safely treated in this manner. Updates the
  /// stream's offset to point after the newly read object. Whether a copy
  /// occurs depends upon the implementation of the underlying stream.
  ///
  /// \param Dest Set to a pointer into the stream data for the object.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T> Error readObject(const T *&Dest) {
    ArrayRef<uint8_t> Buffer;
    if (auto EC = readBytes(Buffer, sizeof(T)))
      return EC;
    Dest = reinterpret_cast<const T *>(Buffer.data());
    return Error::success();
  }

  /// Read an ArrayRef of \p NumElements objects of type T as if by memcpy.
  ///
  /// Store the resulting array slice into \p Array. It is up to the caller to
  /// ensure that objects of type T can be safely treated in this manner.
  /// Updates the stream's offset to point after the newly read object. Whether
  /// a copy occurs depends upon the implementation of the underlying stream.
  ///
  /// \param Array Set to the array slice of \p NumElements elements.
  /// \param NumElements Number of elements of type T to read.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T>
  Error readArray(ArrayRef<T> &Array, uint32_t NumElements) {
    ArrayRef<uint8_t> Bytes;
    if (NumElements == 0) {
      Array = ArrayRef<T>();
      return Error::success();
    }

    if (NumElements > UINT32_MAX / sizeof(T))
      return make_error<BinaryStreamError>(
          stream_error_code::invalid_array_size);

    if (auto EC = readBytes(Bytes, NumElements * sizeof(T)))
      return EC;

    assert(isAddrAligned(Align::Of<T>(), Bytes.data()) &&
           "Reading at invalid alignment!");

    Array = ArrayRef<T>(reinterpret_cast<const T *>(Bytes.data()), NumElements);
    return Error::success();
  }

  /// Read a VarStreamArray of \p Size bytes into \p Array.
  ///
  /// Updates the stream's offset to point after the newly read array. Never
  /// causes a copy (although iterating the elements of the VarStreamArray may,
  /// depending upon the implementation of the underlying stream).
  ///
  /// \param Array Set to the VarStreamArray covering the requested bytes.
  /// \param Size Number of bytes to include in the array's underlying stream.
  /// \param Skew Byte skew passed to setUnderlyingStream; defaults to 0.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T, typename U>
  Error readArray(VarStreamArray<T, U> &Array, uint32_t Size,
                  uint32_t Skew = 0) {
    BinaryStreamRef S;
    if (auto EC = readStreamRef(S, Size))
      return EC;
    Array.setUnderlyingStream(S, Skew);
    return Error::success();
  }

  /// Read a FixedStreamArray of \p NumItems elements into \p Array.
  ///
  /// Updates the stream's offset to point after the newly read array. Never
  /// causes a copy (although iterating the elements of the FixedStreamArray
  /// may, depending upon the implementation of the underlying stream).
  ///
  /// \param Array Set to the FixedStreamArray of \p NumItems elements.
  /// \param NumItems Number of elements of type T to read.
  ///
  /// \returns a success error code if the data was successfully read, otherwise
  /// returns an appropriate error code.
  template <typename T>
  Error readArray(FixedStreamArray<T> &Array, uint32_t NumItems) {
    if (NumItems == 0) {
      Array = FixedStreamArray<T>();
      return Error::success();
    }

    if (NumItems > UINT32_MAX / sizeof(T))
      return make_error<BinaryStreamError>(
          stream_error_code::invalid_array_size);

    BinaryStreamRef View;
    if (auto EC = readStreamRef(View, NumItems * sizeof(T)))
      return EC;

    Array = FixedStreamArray<T>(View);
    return Error::success();
  }

  /// Return true if no unread bytes remain in the stream.
  ///
  /// \returns true if no unread bytes remain, false otherwise.
  bool empty() const { return bytesRemaining() == 0; }

  /// Set the current read offset within the stream.
  ///
  /// \param Off Byte offset to seek to.
  void setOffset(uint64_t Off) { Offset = Off; }

  /// Return the current read offset within the stream.
  ///
  /// \returns the current read offset in bytes.
  uint64_t getOffset() const { return Offset; }

  /// Return the total length of the underlying stream in bytes.
  ///
  /// \returns the total length of the underlying stream in bytes.
  uint64_t getLength() const { return Stream.getLength(); }

  /// Return the number of unread bytes remaining in the stream.
  ///
  /// \returns the number of unread bytes remaining in the stream.
  uint64_t bytesRemaining() const { return getLength() - getOffset(); }

  /// Advance the stream's offset by \p Amount bytes.
  ///
  /// \param Amount Number of bytes to skip.
  ///
  /// \returns a success error code if at least \p Amount bytes remain in the
  /// stream, otherwise returns an appropriate error code.
  LLVM_ABI Error skip(uint64_t Amount);

  /// Examine the next byte of the underlying stream without advancing the
  /// stream's offset.  If the stream is empty the behavior is undefined.
  ///
  /// \returns the next byte in the stream.
  LLVM_ABI uint8_t peek() const;

  /// Advance the offset to the next multiple of \p Align.
  ///
  /// \param Align Alignment boundary to pad up to, in bytes.
  ///
  /// \returns a success error code if enough bytes remain to reach the
  /// alignment, otherwise returns an appropriate error code.
  LLVM_ABI Error padToAlignment(uint32_t Align);

  /// Split the remaining stream into two readers at relative offset \p Offset.
  ///
  /// The first reader covers \p Offset bytes from the current position; the
  /// second covers the remainder. Neither shares this reader's offset state.
  ///
  /// \param Offset Relative byte offset from the current position at which to
  ///        split.
  /// \returns A pair of readers for the left and right portions.
  LLVM_ABI std::pair<BinaryStreamReader, BinaryStreamReader>
  split(uint64_t Offset) const;

private:
  BinaryStreamRef Stream;
  uint64_t Offset = 0;
};
} // namespace llvm

#endif // LLVM_SUPPORT_BINARYSTREAMREADER_H
