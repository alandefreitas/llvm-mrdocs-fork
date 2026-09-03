//===- CodeViewRecordIO.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CODEVIEWRECORDIO_H
#define LLVM_DEBUGINFO_CODEVIEW_CODEVIEWRECORDIO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <cstdint>
#include <type_traits>

namespace llvm {

template <typename T> class ArrayRef;
class APSInt;

namespace codeview {
class TypeIndex;
struct GUID;

/// Abstract interface for emitting CodeView record bytes to an assembly stream.
class CodeViewRecordStreamer {
public:
  /// Emit the raw bytes in \p Data to the output stream.
  ///
  /// \param Data Bytes to emit.
  virtual void emitBytes(StringRef Data) = 0;
  /// Emit integer \p Value occupying \p Size bytes.
  ///
  /// \param Value Integer value to emit.
  /// \param Size Number of bytes to write for \p Value.
  virtual void emitIntValue(uint64_t Value, unsigned Size) = 0;
  /// Emit \p Data as binary data in the assembly output.
  ///
  /// \param Data Bytes to emit as binary data.
  virtual void emitBinaryData(StringRef Data) = 0;
  /// Attach an assembly comment built from \p T.
  ///
  /// \param T Comment text to add.
  virtual void AddComment(const Twine &T) = 0;
  /// Attach a raw (unformatted) assembly comment built from \p T.
  ///
  /// \param T Raw comment text to add.
  virtual void AddRawComment(const Twine &T) = 0;
  /// Return true if the streamer is producing verbose assembly comments.
  ///
  /// \returns True if verbose assembly comments are enabled.
  virtual bool isVerboseAsm() = 0;
  /// Return a human-readable name for type index \p TI.
  ///
  /// \param TI Type index whose name should be resolved.
  ///
  /// \returns Human-readable name for \p TI.
  virtual std::string getTypeName(TypeIndex TI) = 0;
  /// Destroy the streamer.
  virtual ~CodeViewRecordStreamer() = default;
};

/// Reads, writes, or streams CodeView records through a shared field-mapping
/// interface.
class CodeViewRecordIO {
  uint32_t getCurrentOffset() const {
    if (isWriting())
      return Writer->getOffset();
    else if (isReading())
      return Reader->getOffset();
    else
      return 0;
  }

public:
  /// Construct a record I/O object that deserializes records from \p Reader.
  ///
  /// \param Reader Binary stream reader supplying record bytes.
  explicit CodeViewRecordIO(BinaryStreamReader &Reader) : Reader(&Reader) {}

  /// Construct a record I/O object that serializes records into \p Writer.
  ///
  /// \param Writer Binary stream writer receiving record bytes.
  explicit CodeViewRecordIO(BinaryStreamWriter &Writer) : Writer(&Writer) {}

  /// Construct a record I/O object that streams records via \p Streamer.
  ///
  /// \param Streamer Assembly streamer used to emit record bytes and comments.
  explicit CodeViewRecordIO(CodeViewRecordStreamer &Streamer)
      : Streamer(&Streamer) {}

  /// Begin a record with an optional maximum payload length.
  ///
  /// \param MaxLength Optional hard limit on the number of bytes in this
  /// record; when set, nested field reads/writes are bounded by it.
  ///
  /// \returns Success, or an Error if the record cannot be started.
  LLVM_ABI Error beginRecord(std::optional<uint32_t> MaxLength);
  /// Finish the current record and restore the previous length limit.
  ///
  /// When streaming, also emits LF_PAD padding so the record ends on a 4-byte
  /// boundary.
  ///
  /// \returns Success, or an Error if finishing the record fails.
  LLVM_ABI Error endRecord();

  /// Map a CodeView type index field, optionally emitting \p Comment.
  ///
  /// \param TypeInd Type index value to read, write, or stream.
  /// \param Comment Optional assembly comment describing the field.
  ///
  /// \returns Success, or an Error if the field cannot be mapped.
  LLVM_ABI Error mapInteger(TypeIndex &TypeInd, const Twine &Comment = "");

  /// Return true if this I/O object is streaming records to an assembler.
  ///
  /// \returns True if streaming to an assembly streamer.
  bool isStreaming() const {
    return (Streamer != nullptr) && (Reader == nullptr) && (Writer == nullptr);
  }
  /// Return true if this I/O object is reading records from a binary stream.
  ///
  /// \returns True if reading from a binary stream reader.
  bool isReading() const {
    return (Reader != nullptr) && (Streamer == nullptr) && (Writer == nullptr);
  }
  /// Return true if this I/O object is writing records to a binary stream.
  ///
  /// \returns True if writing through a binary stream writer.
  bool isWriting() const {
    return (Writer != nullptr) && (Streamer == nullptr) && (Reader == nullptr);
  }

  /// Return the maximum number of bytes the next field may occupy.
  ///
  /// \returns Maximum length in bytes available for the next field.
  LLVM_ABI uint32_t maxFieldLength() const;

  /// Map a trivially copyable object by reading, writing, or streaming its
  /// bytes.
  ///
  /// \param Value Object whose representation is mapped.
  ///
  /// \returns Success, or an Error if the object cannot be mapped.
  template <typename T> Error mapObject(T &Value) {
    if (isStreaming()) {
      StringRef BytesSR =
          StringRef((reinterpret_cast<const char *>(&Value)), sizeof(Value));
      Streamer->emitBytes(BytesSR);
      incrStreamedLen(sizeof(T));
      return Error::success();
    }

    if (isWriting())
      return Writer->writeObject(Value);

    const T *ValuePtr;
    if (auto EC = Reader->readObject(ValuePtr))
      return EC;
    Value = *ValuePtr;
    return Error::success();
  }

  /// Map an integer field of type \c T, optionally emitting \p Comment.
  ///
  /// \param Value Integer value to read, write, or stream.
  /// \param Comment Optional assembly comment describing the field.
  ///
  /// \returns Success, or an Error if the field cannot be mapped.
  template <typename T> Error mapInteger(T &Value, const Twine &Comment = "") {
    if (isStreaming()) {
      emitComment(Comment);
      Streamer->emitIntValue((int)Value, sizeof(T));
      incrStreamedLen(sizeof(T));
      return Error::success();
    }

    if (isWriting())
      return Writer->writeInteger(Value);

    return Reader->readInteger(Value);
  }

  /// Map an enumeration field via its underlying integer type.
  ///
  /// \param Value Enumeration value to read, write, or stream.
  /// \param Comment Optional assembly comment describing the field.
  ///
  /// \returns Success, or an Error if the field cannot be mapped.
  template <typename T> Error mapEnum(T &Value, const Twine &Comment = "") {
    if (!isStreaming() && sizeof(Value) > maxFieldLength())
      return make_error<CodeViewError>(cv_error_code::insufficient_buffer);

    using U = std::underlying_type_t<T>;
    U X;

    if (isWriting() || isStreaming())
      X = static_cast<U>(Value);

    if (auto EC = mapInteger(X, Comment))
      return EC;

    if (isReading())
      Value = static_cast<T>(X);

    return Error::success();
  }

  /// Map a signed integer encoded in CodeView's variable-length form.
  ///
  /// \param Value Signed integer to read, write, or stream.
  /// \param Comment Optional assembly comment describing the field.
  ///
  /// \returns Success, or an Error if the encoded integer cannot be mapped.
  LLVM_ABI Error mapEncodedInteger(int64_t &Value, const Twine &Comment = "");
  /// Map an unsigned integer encoded in CodeView's variable-length form.
  ///
  /// \param Value Unsigned integer to read, write, or stream.
  /// \param Comment Optional assembly comment describing the field.
  ///
  /// \returns Success, or an Error if the encoded integer cannot be mapped.
  LLVM_ABI Error mapEncodedInteger(uint64_t &Value, const Twine &Comment = "");
  /// Map an arbitrary-precision integer encoded in CodeView's variable-length
  /// form.
  ///
  /// \param Value Arbitrary-precision integer to read, write, or stream.
  /// \param Comment Optional assembly comment describing the field.
  ///
  /// \returns Success, or an Error if the encoded integer cannot be mapped.
  LLVM_ABI Error mapEncodedInteger(APSInt &Value, const Twine &Comment = "");
  /// Map a null-terminated string field.
  ///
  /// \param Value Null-terminated string to read, write, or stream.
  /// \param Comment Optional assembly comment describing the field.
  ///
  /// \returns Success, or an Error if the string cannot be mapped.
  LLVM_ABI Error mapStringZ(StringRef &Value, const Twine &Comment = "");
  /// Map a 16-byte GUID field.
  ///
  /// \param Guid GUID value to read, write, or stream.
  /// \param Comment Optional assembly comment describing the field.
  ///
  /// \returns Success, or an Error if the GUID cannot be mapped.
  LLVM_ABI Error mapGuid(GUID &Guid, const Twine &Comment = "");

  /// Map a sequence of null-terminated strings ended by an empty string.
  ///
  /// \param Value Vector of strings to read, write, or stream.
  /// \param Comment Optional assembly comment describing the field.
  ///
  /// \returns Success, or an Error if the string vector cannot be mapped.
  LLVM_ABI Error mapStringZVectorZ(std::vector<StringRef> &Value,
                                   const Twine &Comment = "");

  /// Map a length-prefixed vector of \p Items using \p Mapper for each element.
  ///
  /// The element count is serialized as \c SizeType before the elements.
  ///
  /// \param Items Container of elements to read, write, or stream.
  /// \param Mapper Callable that maps one element given this I/O object.
  /// \param Comment Optional assembly comment describing the vector.
  ///
  /// \returns Success, or an Error if the vector cannot be mapped.
  template <typename SizeType, typename T, typename ElementMapper>
  Error mapVectorN(T &Items, const ElementMapper &Mapper,
                   const Twine &Comment = "") {
    SizeType Size;
    if (isStreaming()) {
      Size = static_cast<SizeType>(Items.size());
      emitComment(Comment);
      Streamer->emitIntValue(Size, sizeof(Size));
      incrStreamedLen(sizeof(Size)); // add 1 for the delimiter

      for (auto &X : Items) {
        if (auto EC = Mapper(*this, X))
          return EC;
      }
    } else if (isWriting()) {
      Size = static_cast<SizeType>(Items.size());
      if (auto EC = Writer->writeInteger(Size))
        return EC;

      for (auto &X : Items) {
        if (auto EC = Mapper(*this, X))
          return EC;
      }
    } else {
      if (auto EC = Reader->readInteger(Size))
        return EC;
      for (SizeType I = 0; I < Size; ++I) {
        typename T::value_type Item;
        if (auto EC = Mapper(*this, Item))
          return EC;
        Items.push_back(Item);
      }
    }

    return Error::success();
  }

  /// Map a trailing vector of \p Items with no explicit length prefix.
  ///
  /// When reading, elements are consumed until the stream ends or padding
  /// bytes are reached.
  ///
  /// \param Items Container of elements to read, write, or stream.
  /// \param Mapper Callable that maps one element given this I/O object.
  /// \param Comment Optional assembly comment describing the vector.
  ///
  /// \returns Success, or an Error if the vector cannot be mapped.
  template <typename T, typename ElementMapper>
  Error mapVectorTail(T &Items, const ElementMapper &Mapper,
                      const Twine &Comment = "") {
    emitComment(Comment);
    if (isStreaming() || isWriting()) {
      for (auto &Item : Items) {
        if (auto EC = Mapper(*this, Item))
          return EC;
      }
    } else {
      typename T::value_type Field;
      // Stop when we run out of bytes or we hit record padding bytes.
      while (!Reader->empty() && Reader->peek() < 0xf0 /* LF_PAD0 */) {
        if (auto EC = Mapper(*this, Field))
          return EC;
        Items.push_back(Field);
      }
    }
    return Error::success();
  }

  /// Map the remaining record bytes as a byte vector into \p Bytes.
  ///
  /// \param Bytes Byte span filled when reading, or sourced when writing or
  /// streaming.
  /// \param Comment Optional assembly comment describing the field.
  ///
  /// \returns Success, or an Error if the byte vector cannot be mapped.
  LLVM_ABI Error mapByteVectorTail(ArrayRef<uint8_t> &Bytes,
                                   const Twine &Comment = "");
  /// Map the remaining record bytes as a byte vector into \p Bytes.
  ///
  /// \param Bytes Byte vector filled when reading, or sourced when writing or
  /// streaming.
  /// \param Comment Optional assembly comment describing the field.
  ///
  /// \returns Success, or an Error if the byte vector cannot be mapped.
  LLVM_ABI Error mapByteVectorTail(std::vector<uint8_t> &Bytes,
                                   const Twine &Comment = "");

  /// Pad the current offset forward to the next multiple of \p Align.
  ///
  /// \param Align Alignment boundary in bytes.
  ///
  /// \returns Success, or an Error if padding fails.
  LLVM_ABI Error padToAlignment(uint32_t Align);
  /// Skip trailing CodeView LF_PAD padding bytes when reading.
  ///
  /// \returns Success, or an Error if padding bytes cannot be skipped.
  LLVM_ABI Error skipPadding();

  /// Return the number of bytes streamed so far, or zero when not streaming.
  ///
  /// \returns Bytes streamed so far, or zero when not streaming.
  uint64_t getStreamedLen() {
    if (isStreaming())
      return StreamedLen;
    return 0;
  }

  /// Emit raw comment \p T when streaming verbose assembly.
  ///
  /// \param T Raw comment text to emit.
  void emitRawComment(const Twine &T) {
    if (isStreaming() && Streamer->isVerboseAsm())
      Streamer->AddRawComment(T);
  }

private:
  void emitEncodedSignedInteger(const int64_t &Value,
                                const Twine &Comment = "");
  void emitEncodedUnsignedInteger(const uint64_t &Value,
                                  const Twine &Comment = "");
  Error writeEncodedSignedInteger(const int64_t &Value);
  Error writeEncodedUnsignedInteger(const uint64_t &Value);

  void incrStreamedLen(const uint64_t &Len) {
    if (isStreaming())
      StreamedLen += Len;
  }

  void resetStreamedLen() {
    if (isStreaming())
      StreamedLen = 4; // The record prefix is 4 bytes long
  }

  void emitComment(const Twine &Comment) {
    if (isStreaming() && Streamer->isVerboseAsm()) {
      Twine TComment(Comment);
      if (!TComment.isTriviallyEmpty())
        Streamer->AddComment(TComment);
    }
  }

  struct RecordLimit {
    uint32_t BeginOffset;
    std::optional<uint32_t> MaxLength;

    std::optional<uint32_t> bytesRemaining(uint32_t CurrentOffset) const {
      if (!MaxLength)
        return std::nullopt;
      assert(CurrentOffset >= BeginOffset);

      uint32_t BytesUsed = CurrentOffset - BeginOffset;
      if (BytesUsed >= *MaxLength)
        return 0;
      return *MaxLength - BytesUsed;
    }
  };

  SmallVector<RecordLimit, 2> Limits;

  BinaryStreamReader *Reader = nullptr;
  BinaryStreamWriter *Writer = nullptr;
  CodeViewRecordStreamer *Streamer = nullptr;
  uint64_t StreamedLen = 0;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_CODEVIEWRECORDIO_H
