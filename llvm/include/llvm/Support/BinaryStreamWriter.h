//===- BinaryStreamWriter.h - Writes objects to a BinaryStream ---*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BINARYSTREAMWRITER_H
#define LLVM_SUPPORT_BINARYSTREAMWRITER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamError.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <type_traits>
#include <utility>

namespace llvm {

/// Provides write-only access to a subclass of `WritableBinaryStream`.
///
/// Provides bounds checking and helpers for writing certain common data types
/// such as null-terminated strings, integers in various flavors of endianness,
/// etc.  Can be subclassed to provide reading and writing of custom datatypes,
/// although no methods are overridable.
class BinaryStreamWriter {
public:
  /// Construct an empty BinaryStreamWriter with no underlying stream.
  BinaryStreamWriter() = default;

  /// Construct a BinaryStreamWriter over a writable stream reference.
  ///
  /// \param Ref The writable stream to write into.
  LLVM_ABI explicit BinaryStreamWriter(WritableBinaryStreamRef Ref);

  /// Construct a BinaryStreamWriter over a writable stream.
  ///
  /// \param Stream The writable stream to write into.
  LLVM_ABI explicit BinaryStreamWriter(WritableBinaryStream &Stream);

  /// Construct a BinaryStreamWriter over a mutable byte buffer.
  ///
  /// \param Data The mutable buffer to write into.
  /// \param Endian Endianness of multi-byte values written to the stream.
  LLVM_ABI explicit BinaryStreamWriter(MutableArrayRef<uint8_t> Data,
                                       llvm::endianness Endian);

  /// Copy-construct a BinaryStreamWriter.
  ///
  /// \param Other The writer to copy.
  BinaryStreamWriter(const BinaryStreamWriter &Other) = default;

  /// Copy-assign a BinaryStreamWriter.
  ///
  /// \param Other The writer to copy from.
  /// \returns A reference to this writer.
  BinaryStreamWriter &operator=(const BinaryStreamWriter &Other) = default;

  /// Destroy a BinaryStreamWriter.
  virtual ~BinaryStreamWriter() = default;

  /// Write the bytes in \p Buffer to the underlying stream.
  ///
  /// On success, updates the offset so that subsequent writes will occur at the
  /// next unwritten position.
  ///
  /// \param Buffer The bytes to write.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  LLVM_ABI Error writeBytes(ArrayRef<uint8_t> Buffer);

  /// Write the integer \p Value in the stream's endianness.
  ///
  /// On success, updates the offset so that subsequent writes occur at the next
  /// unwritten position.
  ///
  /// \param Value The integral value to write.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  template <typename T> Error writeInteger(T Value) {
    static_assert(std::is_integral_v<T>,
                  "Cannot call writeInteger with non-integral value!");
    uint8_t Buffer[sizeof(T)];
    llvm::support::endian::write<T>(Buffer, Value, Stream.getEndian());
    return writeBytes(Buffer);
  }

  /// Write the enum \p Num as its underlying integer value.
  ///
  /// \param Num The enum value to write.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  template <typename T> Error writeEnum(T Num) {
    static_assert(std::is_enum<T>::value,
                  "Cannot call writeEnum with non-Enum type");

    return writeInteger(llvm::to_underlying(Num));
  }

  /// Write \p Value to the underlying stream using ULEB128 encoding.
  ///
  /// \param Value The unsigned integer to encode and write.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  LLVM_ABI Error writeULEB128(uint64_t Value);

  /// Write \p Value to the underlying stream using SLEB128 encoding.
  ///
  /// \param Value The signed integer to encode and write.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  LLVM_ABI Error writeSLEB128(int64_t Value);

  /// Write \p Str followed by a null terminator.
  ///
  /// On success, updates the offset so that subsequent writes occur at the next
  /// unwritten position.  \p Str need not be null terminated on input.
  ///
  /// \param Str The string to write.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  LLVM_ABI Error writeCString(StringRef Str);

  /// Write \p Str without a null terminator.
  ///
  /// On success, updates the offset so that subsequent writes occur at the next
  /// unwritten position.
  ///
  /// \param Str The string to write.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  LLVM_ABI Error writeFixedString(StringRef Str);

  /// Write all data from \p Ref into this stream without copying.
  ///
  /// This operation will not invoke any copies of the source data, regardless
  /// of the source stream's implementation.
  ///
  /// \param Ref The stream whose contents are written.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  LLVM_ABI Error writeStreamRef(BinaryStreamRef Ref);

  /// Write \p Size bytes from \p Ref into this stream without copying.
  ///
  /// This operation will not invoke any copies of the source data, regardless
  /// of the source stream's implementation.
  ///
  /// \param Ref The stream to read from.
  /// \param Size Number of bytes to read from \p Ref and write.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  LLVM_ABI Error writeStreamRef(BinaryStreamRef Ref, uint64_t Size);

  /// Write \p Obj to the underlying stream as if by memcpy.
  ///
  /// It is up to the caller to ensure that type of \p Obj can be safely copied
  /// in this fashion, as no checks are made to ensure that this is safe.
  ///
  /// \param Obj The object whose bytes are written.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  template <typename T> Error writeObject(const T &Obj) {
    static_assert(!std::is_pointer<T>::value,
                  "writeObject should not be used with pointers, to write "
                  "the pointed-to value dereference the pointer before calling "
                  "writeObject");
    return writeBytes(
        ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(&Obj), sizeof(T)));
  }

  /// Write an array of objects of type T as if by memcpy.
  ///
  /// It is up to the caller to ensure that objects of type T can be safely
  /// copied in this fashion, as no checks are made to ensure that this is safe.
  ///
  /// \param Array The array of objects to write.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  template <typename T> Error writeArray(ArrayRef<T> Array) {
    if (Array.empty())
      return Error::success();
    if (Array.size() > UINT32_MAX / sizeof(T))
      return make_error<BinaryStreamError>(
          stream_error_code::invalid_array_size);

    return writeBytes(
        ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(Array.data()),
                          Array.size() * sizeof(T)));
  }

  /// Write all data from the VarStreamArray \p Array.
  ///
  /// \param Array The variable-length stream array to write.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  template <typename T, typename U>
  Error writeArray(VarStreamArray<T, U> Array) {
    return writeStreamRef(Array.getUnderlyingStream());
  }

  /// Write all elements from the FixedStreamArray \p Array.
  ///
  /// \param Array The fixed-length stream array to write.
  ///
  /// \returns a success error code if the data was successfully written,
  /// otherwise returns an appropriate error code.
  template <typename T> Error writeArray(FixedStreamArray<T> Array) {
    return writeStreamRef(Array.getUnderlyingStream());
  }

  /// Split this writer into two writers at \p Off.
  ///
  /// \param Off Byte offset at which to split.
  ///
  /// \returns a pair of writers covering the ranges before and after \p Off.
  LLVM_ABI std::pair<BinaryStreamWriter, BinaryStreamWriter>
  split(uint64_t Off) const;

  /// Set the current write offset.
  ///
  /// \param Off The new byte offset for subsequent writes.
  void setOffset(uint64_t Off) { Offset = Off; }

  /// Return the current write offset.
  ///
  /// \returns the current write offset in bytes.
  uint64_t getOffset() const { return Offset; }

  /// Return the length of the underlying stream.
  ///
  /// \returns the total length of the underlying stream in bytes.
  uint64_t getLength() const { return Stream.getLength(); }

  /// Return the number of bytes remaining from the current offset.
  ///
  /// \returns the number of unwritten bytes remaining in the stream.
  uint64_t bytesRemaining() const { return getLength() - getOffset(); }

  /// Pad the stream with zeros up to the next multiple of \p Align.
  ///
  /// \param Align The alignment boundary to pad to.
  ///
  /// \returns a success error code if the padding was successfully written,
  /// otherwise returns an appropriate error code.
  LLVM_ABI Error padToAlignment(uint32_t Align);

protected:
  /// The underlying writable stream being written to.
  WritableBinaryStreamRef Stream;
  /// The current byte offset for subsequent writes.
  uint64_t Offset = 0;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_BINARYSTREAMWRITER_H
