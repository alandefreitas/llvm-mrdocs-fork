//===-- DataExtractor.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DATAEXTRACTOR_H
#define LLVM_SUPPORT_DATAEXTRACTOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Error.h"

namespace llvm {

/// An auxiliary type to facilitate extraction of 3-byte entities.
struct Uint24 {
  /// Three raw bytes of the 24-bit value in extraction endianness.
  uint8_t Bytes[3];
  /// Construct with all three bytes set to the same value.
  ///
  /// \param U Value copied into each of the three bytes.
  Uint24(uint8_t U) : Bytes{U, U, U} {}
  /// Construct from three individual bytes.
  ///
  /// \param U0 First byte.
  /// \param U1 Second byte.
  /// \param U2 Third byte.
  Uint24(uint8_t U0, uint8_t U1, uint8_t U2) : Bytes{U0, U1, U2} {}
  /// Interpret the three bytes as a 24-bit unsigned integer.
  ///
  /// \param IsLittleEndian Whether to treat the bytes as little-endian.
  ///
  /// \return
  ///     The 24-bit value as a uint32_t.
  uint32_t getAsUint32(bool IsLittleEndian) const {
    int LoIx = IsLittleEndian ? 0 : 2;
    return Bytes[LoIx] + (Bytes[1] << 8) + (Bytes[2-LoIx] << 16);
  }
};

/// Alias for a packed 24-bit unsigned integer.
using uint24_t = Uint24;
static_assert(sizeof(uint24_t) == 3, "sizeof(uint24_t) != 3");

/// Needed by swapByteOrder().
///
/// \param C Value whose bytes should be reversed.
///
/// \return
///     A uint24_t with the three bytes in reverse order.
inline uint24_t getSwappedBytes(uint24_t C) {
  return uint24_t(C.Bytes[2], C.Bytes[1], C.Bytes[0]);
}

/// Helper for extracting integers and strings from a binary buffer.
class DataExtractor {
  StringRef Data;
  uint8_t IsLittleEndian;

public:
  /// Position in a DataExtractor together with any extraction error.
  ///
  /// It enables one to extract a sequence of values without error-checking and
  /// then checking for errors in bulk at the end. The class holds an Error
  /// object, so failing to check the result of the parse will result in a
  /// runtime error. The error flag is sticky and will cause all subsequent
  /// extraction functions to fail without even attempting to parse and without
  /// updating the Cursor offset. After clearing the error flag, one can again
  /// use the Cursor object for parsing.
  class Cursor {
    uint64_t Offset;
    Error Err;

    friend class DataExtractor;

  public:
    /// Construct a cursor for extraction from the given offset.
    ///
    /// \param Offset Byte offset at which extraction should begin.
    explicit Cursor(uint64_t Offset) : Offset(Offset), Err(Error::success()) {}

    /// Checks whether the cursor is valid (no errors were encountered).
    ///
    /// In case of errors, this does not clear the error flag -- one must call
    /// takeError() instead.
    ///
    /// \return
    ///     \b true if no errors have been encountered, \b false otherwise.
    explicit operator bool() { return !Err; }

    /// Return the current position of this Cursor. In the error state this is
    /// the position of the Cursor before the first error was encountered.
    ///
    /// \return
    ///     The current byte offset, or the offset before the first error.
    uint64_t tell() const { return Offset; }

    /// Set the cursor to the new offset. This does not impact the error state.
    ///
    /// \param NewOffSet Byte offset to move the cursor to.
    void seek(uint64_t NewOffSet) { Offset = NewOffSet; }

    /// Return error contained inside this Cursor, if any. Clears the internal
    /// Cursor state.
    ///
    /// \return
    ///     The Error previously held by this cursor (moved out).
    Error takeError() { return std::move(Err); }
  };

  /// Construct with a buffer that is owned by the caller.
  ///
  /// This constructor allows us to use data that is owned by the
  /// caller. The data must stay around as long as this object is
  /// valid.
  ///
  /// \param Data Buffer whose bytes will be extracted.
  /// \param IsLittleEndian Whether multi-byte values are little-endian.
  DataExtractor(StringRef Data, bool IsLittleEndian)
      : Data(Data), IsLittleEndian(IsLittleEndian) {}

  /// Construct from a byte array with the given endianness.
  ///
  /// \param Data Byte array whose contents will be extracted.
  /// \param IsLittleEndian Whether multi-byte values are little-endian.
  DataExtractor(ArrayRef<uint8_t> Data, bool IsLittleEndian)
      : Data(StringRef(reinterpret_cast<const char *>(Data.data()),
                       Data.size())),
        IsLittleEndian(IsLittleEndian) {}

  /// Get the data pointed to by this extractor.
  ///
  /// \return
  ///     The underlying data buffer as a StringRef.
  StringRef getData() const { return Data; }
  /// Get the endianness for this extractor.
  ///
  /// \return
  ///     \b true if multi-byte values are little-endian, \b false otherwise.
  bool isLittleEndian() const { return IsLittleEndian; }

  /// Extract a C string from \a *offset_ptr.
  ///
  /// Returns a pointer to a C String from the data at the offset
  /// pointed to by \a offset_ptr. A variable length NULL terminated C
  /// string will be extracted and the \a offset_ptr will be
  /// updated with the offset of the byte that follows the NULL
  /// terminator byte.
  ///
  /// @param[in,out] OffsetPtr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[in,out] Err
  ///     A pointer to an Error object. Upon return the Error object is set to
  ///     indicate the result (success/failure) of the function. If the Error
  ///     object is already set when calling this function, no extraction is
  ///     performed.
  ///
  /// @return
  ///     A pointer to the C string value in the data. If the offset
  ///     pointed to by \a offset_ptr is out of bounds, or if the
  ///     offset plus the length of the C string is out of bounds,
  ///     NULL will be returned.
  const char *getCStr(uint64_t *OffsetPtr, Error *Err = nullptr) const {
    return getCStrRef(OffsetPtr, Err).data();
  }

  /// Extract a C string from the location given by the cursor.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, a nullptr is returned.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     A pointer to the C string, or nullptr on failure.
  const char *getCStr(Cursor &C) const { return getCStrRef(C).data(); }

  /// Extract a C string from \a *offset_ptr.
  ///
  /// Returns a StringRef for the C String from the data at the offset
  /// pointed to by \a offset_ptr. A variable length NULL terminated C
  /// string will be extracted and the \a offset_ptr will be
  /// updated with the offset of the byte that follows the NULL
  /// terminator byte.
  ///
  /// \param[in,out] OffsetPtr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[in,out] Err
  ///     A pointer to an Error object. Upon return the Error object is set to
  ///     indicate the result (success/failure) of the function. If the Error
  ///     object is already set when calling this function, no extraction is
  ///     performed.
  ///
  /// \return
  ///     A StringRef for the C string value in the data. If the offset
  ///     pointed to by \a offset_ptr is out of bounds, or if the
  ///     offset plus the length of the C string is out of bounds,
  ///     a default-initialized StringRef will be returned.
  LLVM_ABI StringRef getCStrRef(uint64_t *OffsetPtr,
                                Error *Err = nullptr) const;

  /// Extract a C string (as a StringRef) from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, a default-initialized StringRef is returned.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     A StringRef for the C string, or a default-initialized StringRef
  ///     on failure.
  StringRef getCStrRef(Cursor &C) const {
    return getCStrRef(&C.Offset, &C.Err);
  }

  /// Extract a fixed length string from \a *OffsetPtr and consume \a Length
  /// bytes.
  ///
  /// Returns a StringRef for the string from the data at the offset
  /// pointed to by \a OffsetPtr. A fixed length C string will be extracted
  /// and the \a OffsetPtr will be advanced by \a Length bytes.
  ///
  /// \param[in,out] OffsetPtr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// \param[in] Length
  ///     The length of the fixed length string to extract. If there are not
  ///     enough bytes in the data to extract the full string, the offset will
  ///     be left unmodified.
  ///
  /// \param[in] TrimChars
  ///     A set of characters to trim from the end of the string. Fixed length
  ///     strings are commonly either NULL terminated by one or more zero
  ///     bytes. Some clients have one or more spaces at the end of the string,
  ///     but a good default is to trim the NULL characters.
  ///
  /// \return
  ///     A StringRef for the C string value in the data. If the offset
  ///     pointed to by \a OffsetPtr is out of bounds, or if the
  ///     offset plus the length of the C string is out of bounds,
  ///     a default-initialized StringRef will be returned.
  LLVM_ABI StringRef getFixedLengthString(uint64_t *OffsetPtr, uint64_t Length,
                                          StringRef TrimChars = {"\0",
                                                                 1}) const;

  /// Extract a fixed number of bytes from the specified offset.
  ///
  /// Returns a StringRef for the bytes from the data at the offset
  /// pointed to by \a OffsetPtr. A fixed length C string will be extracted
  /// and the \a OffsetPtr will be advanced by \a Length bytes.
  ///
  /// \param[in,out] OffsetPtr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// \param[in] Length
  ///     The number of bytes to extract. If there are not enough bytes in the
  ///     data to extract all of the bytes, the offset will be left unmodified.
  ///
  /// @param[in,out] Err
  ///     A pointer to an Error object. Upon return the Error object is set to
  ///     indicate the result (success/failure) of the function. If the Error
  ///     object is already set when calling this function, no extraction is
  ///     performed.
  ///
  /// \return
  ///     A StringRef for the extracted bytes. If the offset pointed to by
  ///     \a OffsetPtr is out of bounds, or if the offset plus the length
  ///     is out of bounds, a default-initialized StringRef will be returned.
  LLVM_ABI StringRef getBytes(uint64_t *OffsetPtr, uint64_t Length,
                              Error *Err = nullptr) const;

  /// Extract a fixed number of bytes from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, a default-initialized StringRef is returned.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  /// \param[in] Length
  ///     Number of bytes to extract.
  ///
  /// \return
  ///     A StringRef for the extracted bytes, or a default-initialized
  ///     StringRef on failure.
  StringRef getBytes(Cursor &C, uint64_t Length) {
    return getBytes(&C.Offset, Length, &C.Err);
  }

  /// Extract an unsigned integer of size \a byte_size from \a
  /// *offset_ptr.
  ///
  /// Extract a single unsigned integer value and update the offset
  /// pointed to by \a offset_ptr. The size of the extracted integer
  /// is specified by the \a byte_size argument. \a byte_size should
  /// have a value greater than or equal to one and less than or equal
  /// to eight since the return value is 64 bits wide. Any
  /// \a byte_size values less than 1 or greater than 8 will result in
  /// nothing being extracted, and zero being returned.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[in] byte_size
  ///     The size in byte of the integer to extract.
  ///
  /// @param[in,out] Err
  ///     A pointer to an Error object. Upon return the Error object is set to
  ///     indicate the result (success/failure) of the function. If the Error
  ///     object is already set when calling this function, no extraction is
  ///     performed.
  ///
  /// @return
  ///     The unsigned integer value that was extracted, or zero on
  ///     failure.
  LLVM_ABI uint64_t getUnsigned(uint64_t *offset_ptr, uint32_t byte_size,
                                Error *Err = nullptr) const;

  /// Extract an unsigned integer of the given size from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, zero is returned.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  /// \param[in] Size
  ///     Size in bytes of the integer to extract (1 through 8).
  ///
  /// \return
  ///     The extracted unsigned integer, or zero on failure.
  uint64_t getUnsigned(Cursor &C, uint32_t Size) const {
    return getUnsigned(&C.Offset, Size, &C.Err);
  }

  /// Extract an signed integer of size \a byte_size from \a *offset_ptr.
  ///
  /// Extract a single signed integer value (sign extending if required)
  /// and update the offset pointed to by \a offset_ptr. The size of
  /// the extracted integer is specified by the \a byte_size argument.
  /// \a byte_size should have a value greater than or equal to one
  /// and less than or equal to eight since the return value is 64
  /// bits wide. Any \a byte_size values less than 1 or greater than
  /// 8 will result in nothing being extracted, and zero being returned.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[in] size
  ///     The size in bytes of the integer to extract.
  ///
  /// @return
  ///     The sign extended signed integer value that was extracted,
  ///     or zero on failure.
  LLVM_ABI int64_t getSigned(uint64_t *offset_ptr, uint32_t size) const;

  /// Extract a uint8_t value from \a *offset_ptr.
  ///
  /// Extract a single uint8_t from the binary data at the offset
  /// pointed to by \a offset_ptr, and advance the offset on success.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[in,out] Err
  ///     A pointer to an Error object. Upon return the Error object is set to
  ///     indicate the result (success/failure) of the function. If the Error
  ///     object is already set when calling this function, no extraction is
  ///     performed.
  ///
  /// @return
  ///     The extracted uint8_t value.
  LLVM_ABI uint8_t getU8(uint64_t *offset_ptr, Error *Err = nullptr) const;

  /// Extract a single uint8_t value from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, zero is returned.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     The extracted uint8_t value, or zero on failure.
  uint8_t getU8(Cursor &C) const { return getU8(&C.Offset, &C.Err); }

  /// Extract \a count uint8_t values from \a *offset_ptr.
  ///
  /// Extract \a count uint8_t values from the binary data at the
  /// offset pointed to by \a offset_ptr, and advance the offset on
  /// success. The extracted values are copied into \a dst.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[out] dst
  ///     A buffer to copy \a count uint8_t values into. \a dst must
  ///     be large enough to hold all requested data.
  ///
  /// @param[in] count
  ///     The number of uint8_t values to extract.
  ///
  /// @return
  ///     \a dst if all values were properly extracted and copied,
  ///     NULL otherise.
  LLVM_ABI uint8_t *getU8(uint64_t *offset_ptr, uint8_t *dst,
                          uint32_t count) const;

  /// Extract uint8_t values from the cursor into a destination buffer.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, a nullptr is returned and the destination buffer is left unchanged.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  /// \param[out] Dst
  ///     Buffer large enough to hold \p Count extracted bytes.
  /// \param[in] Count
  ///     Number of uint8_t values to extract.
  ///
  /// \return
  ///     \p Dst if all values were extracted, or nullptr on failure.
  LLVM_ABI uint8_t *getU8(Cursor &C, uint8_t *Dst, uint32_t Count) const;

  /// Extract uint8_t values from the cursor into a destination vector.
  ///
  /// The vector is resized to fit the extracted data. In case of an extraction
  /// error, or if the cursor is already in an error state, the destination
  /// vector is left unchanged and the cursor is placed into an error state.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  /// \param[out] Dst
  ///     Vector resized and filled with the extracted bytes on success.
  /// \param[in] Count
  ///     Number of uint8_t values to extract.
  void getU8(Cursor &C, SmallVectorImpl<uint8_t> &Dst, uint32_t Count) const {
    if (isValidOffsetForDataOfSize(C.Offset, Count))
      Dst.resize(Count);

    // This relies on the fact that getU8 will not attempt to write to the
    // buffer if isValidOffsetForDataOfSize(C.Offset, Count) is false.
    getU8(C, Dst.data(), Count);
  }

  /// Extract a int8_t value from \a *OffsetPtr.
  ///
  /// In case of an extraction error, or if error is already set, zero is
  /// returned and the offset is left unmodified.
  ///
  /// \param[in,out] OffsetPtr
  ///     A pointer to an offset within the data that will be advanced by one
  ///     byte on success, or left unmodified on failure.
  /// \param[in,out] Err
  ///     Optional Error set to indicate success or failure. If already set,
  ///     no extraction is performed.
  ///
  /// \return
  ///     The extracted int8_t value, or zero on failure.
  int8_t getS8(uint64_t *OffsetPtr, Error *Err = nullptr) const {
    return static_cast<int8_t>(getU8(OffsetPtr, Err));
  }

  /// Extract a int8_t value from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, zero is returned and the offset is left unmodified.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     The extracted int8_t value, or zero on failure.
  int8_t getS8(Cursor &C) const { return static_cast<int8_t>(getU8(C)); }

  //------------------------------------------------------------------
  /// Extract a uint16_t value from \a *offset_ptr.
  ///
  /// Extract a single uint16_t from the binary data at the offset
  /// pointed to by \a offset_ptr, and update the offset on success.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[in,out] Err
  ///     A pointer to an Error object. Upon return the Error object is set to
  ///     indicate the result (success/failure) of the function. If the Error
  ///     object is already set when calling this function, no extraction is
  ///     performed.
  ///
  /// @return
  ///     The extracted uint16_t value.
  //------------------------------------------------------------------
  LLVM_ABI uint16_t getU16(uint64_t *offset_ptr, Error *Err = nullptr) const;

  /// Extract a single uint16_t value from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, zero is returned.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     The extracted uint16_t value, or zero on failure.
  uint16_t getU16(Cursor &C) const { return getU16(&C.Offset, &C.Err); }

  /// Extract \a count uint16_t values from \a *offset_ptr.
  ///
  /// Extract \a count uint16_t values from the binary data at the
  /// offset pointed to by \a offset_ptr, and advance the offset on
  /// success. The extracted values are copied into \a dst.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[out] dst
  ///     A buffer to copy \a count uint16_t values into. \a dst must
  ///     be large enough to hold all requested data.
  ///
  /// @param[in] count
  ///     The number of uint16_t values to extract.
  ///
  /// @return
  ///     \a dst if all values were properly extracted and copied,
  ///     NULL otherise.
  LLVM_ABI uint16_t *getU16(uint64_t *offset_ptr, uint16_t *dst,
                            uint32_t count) const;

  /// Extract a int16_t value from \a *OffsetPtr.
  ///
  /// In case of an extraction error, or if error is already set, zero is
  /// returned and the offset is left unmodified.
  ///
  /// \param[in,out] OffsetPtr
  ///     A pointer to an offset within the data that will be advanced on
  ///     success, or left unmodified on failure.
  /// \param[in,out] Err
  ///     Optional Error set to indicate success or failure. If already set,
  ///     no extraction is performed.
  ///
  /// \return
  ///     The extracted int16_t value, or zero on failure.
  int16_t getS16(uint64_t *OffsetPtr, Error *Err = nullptr) const {
    return static_cast<int16_t>(getU16(OffsetPtr, Err));
  }

  /// Extract a int16_t value from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, zero is returned and the offset is left unmodified.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     The extracted int16_t value, or zero on failure.
  int16_t getS16(Cursor &C) const { return static_cast<int16_t>(getU16(C)); }

  /// Extract a 24-bit unsigned value from \a *offset_ptr and return it
  /// in a uint32_t.
  ///
  /// Extract 3 bytes from the binary data at the offset pointed to by
  /// \a offset_ptr, construct a uint32_t from them and update the offset
  /// on success.
  ///
  /// @param[in,out] OffsetPtr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the 3 bytes if the value is extracted correctly. If the offset
  ///     is out of bounds or there are not enough bytes to extract this value,
  ///     the offset will be left unmodified.
  ///
  /// @param[in,out] Err
  ///     A pointer to an Error object. Upon return the Error object is set to
  ///     indicate the result (success/failure) of the function. If the Error
  ///     object is already set when calling this function, no extraction is
  ///     performed.
  ///
  /// @return
  ///     The extracted 24-bit value represented in a uint32_t.
  LLVM_ABI uint32_t getU24(uint64_t *OffsetPtr, Error *Err = nullptr) const;

  /// Extract a single 24-bit unsigned value from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, zero is returned.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     The extracted 24-bit value as a uint32_t, or zero on failure.
  uint32_t getU24(Cursor &C) const { return getU24(&C.Offset, &C.Err); }

  /// Extract a uint32_t value from \a *offset_ptr.
  ///
  /// Extract a single uint32_t from the binary data at the offset
  /// pointed to by \a offset_ptr, and update the offset on success.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[in,out] Err
  ///     A pointer to an Error object. Upon return the Error object is set to
  ///     indicate the result (success/failure) of the function. If the Error
  ///     object is already set when calling this function, no extraction is
  ///     performed.
  ///
  /// @return
  ///     The extracted uint32_t value.
  LLVM_ABI uint32_t getU32(uint64_t *offset_ptr, Error *Err = nullptr) const;

  /// Extract a single uint32_t value from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, zero is returned.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     The extracted uint32_t value, or zero on failure.
  uint32_t getU32(Cursor &C) const { return getU32(&C.Offset, &C.Err); }

  /// Extract \a count uint32_t values from \a *offset_ptr.
  ///
  /// Extract \a count uint32_t values from the binary data at the
  /// offset pointed to by \a offset_ptr, and advance the offset on
  /// success. The extracted values are copied into \a dst.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[out] dst
  ///     A buffer to copy \a count uint32_t values into. \a dst must
  ///     be large enough to hold all requested data.
  ///
  /// @param[in] count
  ///     The number of uint32_t values to extract.
  ///
  /// @return
  ///     \a dst if all values were properly extracted and copied,
  ///     NULL otherise.
  LLVM_ABI uint32_t *getU32(uint64_t *offset_ptr, uint32_t *dst,
                            uint32_t count) const;

  /// Extract a int32_t value from \a *OffsetPtr.
  ///
  /// In case of an extraction error, or if error is already set, zero is
  /// returned and the offset is left unmodified.
  ///
  /// \param[in,out] OffsetPtr
  ///     A pointer to an offset within the data that will be advanced on
  ///     success, or left unmodified on failure.
  /// \param[in,out] Err
  ///     Optional Error set to indicate success or failure. If already set,
  ///     no extraction is performed.
  ///
  /// \return
  ///     The extracted int32_t value, or zero on failure.
  int32_t getS32(uint64_t *OffsetPtr, Error *Err = nullptr) const {
    return static_cast<int32_t>(getU32(OffsetPtr, Err));
  }

  /// Extract a int32_t value from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, zero is returned and the offset is left unmodified.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     The extracted int32_t value, or zero on failure.
  int32_t getS32(Cursor &C) const { return static_cast<int32_t>(getU32(C)); }

  /// Extract a uint64_t value from \a *offset_ptr.
  ///
  /// Extract a single uint64_t from the binary data at the offset
  /// pointed to by \a offset_ptr, and update the offset on success.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[in,out] Err
  ///     A pointer to an Error object. Upon return the Error object is set to
  ///     indicate the result (success/failure) of the function. If the Error
  ///     object is already set when calling this function, no extraction is
  ///     performed.
  ///
  /// @return
  ///     The extracted uint64_t value.
  LLVM_ABI uint64_t getU64(uint64_t *offset_ptr, Error *Err = nullptr) const;

  /// Extract a single uint64_t value from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, zero is returned.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     The extracted uint64_t value, or zero on failure.
  uint64_t getU64(Cursor &C) const { return getU64(&C.Offset, &C.Err); }

  /// Extract \a count uint64_t values from \a *offset_ptr.
  ///
  /// Extract \a count uint64_t values from the binary data at the
  /// offset pointed to by \a offset_ptr, and advance the offset on
  /// success. The extracted values are copied into \a dst.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[out] dst
  ///     A buffer to copy \a count uint64_t values into. \a dst must
  ///     be large enough to hold all requested data.
  ///
  /// @param[in] count
  ///     The number of uint64_t values to extract.
  ///
  /// @return
  ///     \a dst if all values were properly extracted and copied,
  ///     NULL otherise.
  LLVM_ABI uint64_t *getU64(uint64_t *offset_ptr, uint64_t *dst,
                            uint32_t count) const;

  /// Extract a int64_t value from \a *OffsetPtr.
  ///
  /// In case of an extraction error, or if error is already set, zero is
  /// returned and the offset is left unmodified.
  ///
  /// \param[in,out] OffsetPtr
  ///     A pointer to an offset within the data that will be advanced on
  ///     success, or left unmodified on failure.
  /// \param[in,out] Err
  ///     Optional Error set to indicate success or failure. If already set,
  ///     no extraction is performed.
  ///
  /// \return
  ///     The extracted int64_t value, or zero on failure.
  int64_t getS64(uint64_t *OffsetPtr, Error *Err = nullptr) const {
    return static_cast<int64_t>(getU64(OffsetPtr, Err));
  }

  /// Extract a int64_t value from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, zero is returned and the offset is left unmodified.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     The extracted int64_t value, or zero on failure.
  int64_t getS64(Cursor &C) const { return static_cast<int64_t>(getU64(C)); }

  /// Extract a signed LEB128 value from \a *offset_ptr.
  ///
  /// Extracts an signed LEB128 number from this object's data
  /// starting at the offset pointed to by \a offset_ptr. The offset
  /// pointed to by \a offset_ptr will be updated with the offset of
  /// the byte following the last extracted byte.
  ///
  /// @param[in,out] OffsetPtr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[in,out] Err
  ///     A pointer to an Error object. Upon return the Error object is set to
  ///     indicate the result (success/failure) of the function. If the Error
  ///     object is already set when calling this function, no extraction is
  ///     performed.
  ///
  /// @return
  ///     The extracted signed integer value.
  LLVM_ABI int64_t getSLEB128(uint64_t *OffsetPtr, Error *Err = nullptr) const;

  /// Extract a signed LEB128 value from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, zero is returned.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     The extracted signed LEB128 value, or zero on failure.
  int64_t getSLEB128(Cursor &C) const { return getSLEB128(&C.Offset, &C.Err); }

  /// Extract a unsigned LEB128 value from \a *offset_ptr.
  ///
  /// Extracts an unsigned LEB128 number from this object's data
  /// starting at the offset pointed to by \a offset_ptr. The offset
  /// pointed to by \a offset_ptr will be updated with the offset of
  /// the byte following the last extracted byte.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[in,out] Err
  ///     A pointer to an Error object. Upon return the Error object is set to
  ///     indicate the result (success/failure) of the function. If the Error
  ///     object is already set when calling this function, no extraction is
  ///     performed.
  ///
  /// @return
  ///     The extracted unsigned integer value.
  LLVM_ABI uint64_t getULEB128(uint64_t *offset_ptr,
                               llvm::Error *Err = nullptr) const;

  /// Extract an unsigned LEB128 value from the cursor location.
  ///
  /// In case of an extraction error, or if the cursor is already in an error
  /// state, zero is returned.
  ///
  /// \param[in,out] C
  ///     Cursor providing the extraction offset and sticky error state.
  ///
  /// \return
  ///     The extracted unsigned LEB128 value, or zero on failure.
  uint64_t getULEB128(Cursor &C) const { return getULEB128(&C.Offset, &C.Err); }

  /// Advance the Cursor position by the given number of bytes. No-op if the
  /// cursor is in an error state.
  ///
  /// \param[in,out] C
  ///     Cursor whose offset is advanced on success.
  /// \param[in] Length
  ///     Number of bytes to skip.
  LLVM_ABI void skip(Cursor &C, uint64_t Length) const;

  /// Return true if the cursor is at the end of the buffer.
  ///
  /// This ignores the error state of the cursor. The only way both eof and
  /// error states can be true is if one attempts a read while the cursor is at
  /// the very end of the data buffer.
  ///
  /// \param[in] C
  ///     Cursor whose offset is compared against the buffer size.
  ///
  /// \return
  ///     \b true if the cursor offset equals the buffer size, \b false
  ///     otherwise.
  bool eof(const Cursor &C) const { return size() == C.Offset; }

  /// Test the validity of \a offset.
  ///
  /// \param[in] offset
  ///     Byte offset to test against the size of this object's data.
  ///
  /// @return
  ///     \b true if \a offset is a valid offset into the data in this
  ///     object, \b false otherwise.
  bool isValidOffset(uint64_t offset) const { return size() > offset; }

  /// Test the availability of \a length bytes of data from \a offset.
  ///
  /// \param[in] offset
  ///     Starting byte offset within this object's data.
  /// \param[in] length
  ///     Number of bytes that must be available at \p offset.
  ///
  /// @return
  ///     \b true if \a offset is a valid offset and there are \a
  ///     length bytes available at that offset, \b false otherwise.
  bool isValidOffsetForDataOfSize(uint64_t offset, uint64_t length) const {
    return offset + length >= offset && isValidOffset(offset + length - 1);
  }

  /// Return the number of bytes in the underlying buffer.
  ///
  /// \return
  ///     The size in bytes of this extractor's data.
  size_t size() const { return Data.size(); }

protected:
  // Make it possible for subclasses to access these fields without making them
  // public.
  /// Access the mutable extraction offset of cursor \p C.
  ///
  /// \param[in,out] C
  ///     Cursor whose offset reference is returned.
  ///
  /// \return
  ///     A mutable reference to the cursor's byte offset.
  static uint64_t &getOffset(Cursor &C) { return C.Offset; }
  /// Access the mutable error state of cursor \p C.
  ///
  /// \param[in,out] C
  ///     Cursor whose Error reference is returned.
  ///
  /// \return
  ///     A mutable reference to the cursor's Error object.
  static Error &getError(Cursor &C) { return C.Err; }

private:
  /// If it is possible to read \a Size bytes at offset \a Offset, returns \b
  /// true. Otherwise, returns \b false. If \a E is not nullptr, also sets the
  /// error object to indicate an error.
  bool prepareRead(uint64_t Offset, uint64_t Size, Error *E) const;

  template <typename T> T getU(uint64_t *OffsetPtr, Error *Err) const;
  template <typename T>
  T *getUs(uint64_t *OffsetPtr, T *Dst, uint32_t Count, Error *Err) const;
};

} // namespace llvm

#endif
