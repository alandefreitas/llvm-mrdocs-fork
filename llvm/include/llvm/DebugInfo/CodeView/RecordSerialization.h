//===- RecordSerialization.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_RECORDSERIALIZATION_H
#define LLVM_DEBUGINFO_CODEVIEW_RECORDSERIALIZATION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cinttypes>

namespace llvm {
class APSInt;
namespace codeview {
/// Little-endian signed 32-bit integer type.
using llvm::support::little32_t;
/// Little-endian unsigned 16-bit integer type.
using llvm::support::ulittle16_t;
using llvm::support::ulittle32_t;

/// Limit on the size of all codeview symbol and type records, including the
/// RecordPrefix. MSVC does not emit any records larger than this.
enum : unsigned {
  MaxRecordLength = 0xFF00 ///< Maximum CodeView record length in bytes.
};

/// Two-byte length and kind prefix shared by all CodeView records.
struct RecordPrefix {
  /// Construct a zero-initialized record prefix.
  RecordPrefix() = default;
  /// Construct a prefix for \p Kind with a length that covers the kind field.
  ///
  /// \param Kind Record kind enum value (SymRecordKind or TypeRecordKind).
  explicit RecordPrefix(uint16_t Kind) : RecordLen(2), RecordKind(Kind) {}

  /// Record length in bytes, starting from &RecordKind.
  ulittle16_t RecordLen;
  /// Record kind enum (SymRecordKind or TypeRecordKind).
  ulittle16_t RecordKind;
};

/// Reinterpret a byte array as an array of characters. Does not interpret as
/// a C string, as StringRef has several helpers (split) that make that easy.
///
/// \param LeafData Bytes to reinterpret as characters.
///
/// \returns The same bytes viewed as a character StringRef.
LLVM_ABI StringRef getBytesAsCharacters(ArrayRef<uint8_t> LeafData);
/// Reinterpret a byte array as a null-terminated C string.
///
/// \param LeafData Bytes containing a C string payload.
///
/// \returns The bytes up to the first null terminator as a StringRef.
LLVM_ABI StringRef getBytesAsCString(ArrayRef<uint8_t> LeafData);

/// No-op consume that always succeeds; base case for variadic consume.
///
/// \param Reader Stream reader (unused).
///
/// \returns Success.
inline Error consume(BinaryStreamReader &Reader) { return Error::success(); }

/// Decode a numeric leaf value from the type stream into \p Num.
///
/// These are integer literals encountered in the type stream. If the value is
/// positive and less than LF_NUMERIC (1 << 15), it is emitted directly in Data.
/// Otherwise, it has a tag like LF_CHAR that indicates the bitwidth and sign of
/// the numeric data.
///
/// \param Reader Stream positioned at the numeric leaf.
/// \param Num Receives the decoded arbitrary-precision integer.
///
/// \returns Success, or an Error if the numeric leaf cannot be decoded.
LLVM_ABI Error consume(BinaryStreamReader &Reader, APSInt &Num);

/// Decode a numeric leaf value that is known to be a particular type.
///
/// \param Reader Stream positioned at the numeric leaf.
/// \param Value Receives the decoded unsigned integer value.
///
/// \returns Success, or an Error if the numeric leaf cannot be decoded.
LLVM_ABI Error consume_numeric(BinaryStreamReader &Reader, uint64_t &Value);

/// Decode an unsigned fixed-length 32-bit integer.
///
/// \param Reader Stream positioned at the integer.
/// \param Item Receives the decoded unsigned value.
///
/// \returns Success, or an Error if the integer cannot be read.
LLVM_ABI Error consume(BinaryStreamReader &Reader, uint32_t &Item);
/// Decode a signed fixed-length 32-bit integer.
///
/// \param Reader Stream positioned at the integer.
/// \param Item Receives the decoded signed value.
///
/// \returns Success, or an Error if the integer cannot be read.
LLVM_ABI Error consume(BinaryStreamReader &Reader, int32_t &Item);

/// Decode a null-terminated string.
///
/// \param Reader Stream positioned at the string.
/// \param Item Receives the decoded string (without the terminator).
///
/// \returns Success, or an Error if the string cannot be read.
LLVM_ABI Error consume(BinaryStreamReader &Reader, StringRef &Item);

/// Decode a numeric leaf value from a string buffer into \p Num.
///
/// \param Data Buffer advanced past the consumed bytes on success.
/// \param Num Receives the decoded arbitrary-precision integer.
///
/// \returns Success, or an Error if the numeric leaf cannot be decoded.
LLVM_ABI Error consume(StringRef &Data, APSInt &Num);
/// Decode an unsigned 32-bit integer from a string buffer.
///
/// \param Data Buffer advanced past the consumed bytes on success.
/// \param Item Receives the decoded unsigned value.
///
/// \returns Success, or an Error if the integer cannot be read.
LLVM_ABI Error consume(StringRef &Data, uint32_t &Item);

/// Decode an object whose layout matches the underlying byte sequence.
///
/// \param Reader Stream positioned at the object.
/// \param Item Receives a pointer into the stream's underlying data.
///
/// \returns An error on failure, or success with \p Item pointing at the object.
template <typename T> Error consume(BinaryStreamReader &Reader, T *&Item) {
  return Reader.readObject(Item);
}

/// Helper that optionally deserializes \c Item when a predicate is true.
template <typename T, typename U> struct serialize_conditional_impl {
  /// Bind \p Item and the predicate \p Func used to decide whether to read it.
  ///
  /// \param Item Object to deserialize when the predicate is true.
  /// \param Func Callable returning true if \p Item should be consumed.
  serialize_conditional_impl(T &Item, U Func) : Item(Item), Func(Func) {}

  /// Deserialize \c Item from \p Reader when the predicate returns true.
  ///
  /// \param Reader Stream to read from.
  ///
  /// \returns Success, or an Error if \c Item cannot be consumed.
  Error deserialize(BinaryStreamReader &Reader) const {
    if (!Func())
      return Error::success();
    return consume(Reader, Item);
  }

  /// Object to deserialize when the predicate is true.
  T &Item;
  /// Predicate that decides whether \c Item should be read.
  U Func;
};

/// Build a conditional serialization helper for \p Item.
///
/// \param Item Object to deserialize when \p Func returns true.
/// \param Func Callable returning true if \p Item should be consumed.
///
/// \returns A helper that deserializes \p Item only when \p Func is true.
template <typename T, typename U>
serialize_conditional_impl<T, U> serialize_conditional(T &Item, U Func) {
  return serialize_conditional_impl<T, U>(Item, Func);
}

/// Helper that deserializes a fixed-size array whose length comes from \c Func.
template <typename T, typename U> struct serialize_array_impl {
  /// Bind \p Item and the length callback \p Func.
  ///
  /// \param Item Array reference filled with the deserialized elements.
  /// \param Func Callable returning the number of elements to read.
  serialize_array_impl(ArrayRef<T> &Item, U Func) : Item(Item), Func(Func) {}

  /// Deserialize \c Func() elements into \c Item from \p Reader.
  ///
  /// \param Reader Stream to read from.
  ///
  /// \returns Success, or an Error if the array cannot be read.
  Error deserialize(BinaryStreamReader &Reader) const {
    return Reader.readArray(Item, Func());
  }

  /// Array reference filled with the deserialized elements.
  ArrayRef<T> &Item;
  /// Callable returning the number of elements to read.
  U Func;
};

/// Helper that deserializes a trailing sequence of values into a vector.
template <typename T> struct serialize_vector_tail_impl {
  /// Bind the destination vector \p Item.
  ///
  /// \param Item Vector that receives deserialized elements until padding.
  serialize_vector_tail_impl(std::vector<T> &Item) : Item(Item) {}

  /// Append elements from \p Reader until the stream ends or padding begins.
  ///
  /// \param Reader Stream to read from.
  ///
  /// \returns Success, or an Error if an element cannot be consumed.
  Error deserialize(BinaryStreamReader &Reader) const {
    T Field;
    // Stop when we run out of bytes or we hit record padding bytes.
    while (!Reader.empty() && Reader.peek() < LF_PAD0) {
      if (auto EC = consume(Reader, Field))
        return EC;
      Item.push_back(Field);
    }
    return Error::success();
  }

  /// Vector that receives deserialized elements until padding.
  std::vector<T> &Item;
};

/// Helper that deserializes a sequence of null-terminated strings.
struct serialize_null_term_string_array_impl {
  /// Bind the destination string vector \p Item.
  ///
  /// \param Item Vector that receives each null-terminated string.
  serialize_null_term_string_array_impl(std::vector<StringRef> &Item)
      : Item(Item) {}

  /// Read successive C strings from \p Reader until a final empty terminator.
  ///
  /// \param Reader Stream to read from.
  ///
  /// \returns Success, or an Error if a string cannot be read.
  Error deserialize(BinaryStreamReader &Reader) const {
    if (Reader.empty())
      return make_error<CodeViewError>(cv_error_code::insufficient_buffer,
                                       "Null terminated string is empty!");

    while (Reader.peek() != 0) {
      StringRef Field;
      if (auto EC = Reader.readCString(Field))
        return EC;
      Item.push_back(Field);
    }
    return Reader.skip(1);
  }

  /// Vector that receives each null-terminated string.
  std::vector<StringRef> &Item;
};

/// Helper that deserializes the remaining stream bytes as an array of \c T.
template <typename T> struct serialize_arrayref_tail_impl {
  /// Bind the destination array reference \p Item.
  ///
  /// \param Item Array reference filled with all remaining elements.
  serialize_arrayref_tail_impl(ArrayRef<T> &Item) : Item(Item) {}

  /// Fill \c Item with as many \c T values as fit in the remaining bytes.
  ///
  /// \param Reader Stream to read from.
  ///
  /// \returns Success, or an Error if the remaining array cannot be read.
  Error deserialize(BinaryStreamReader &Reader) const {
    uint32_t Count = Reader.bytesRemaining() / sizeof(T);
    return Reader.readArray(Item, Count);
  }

  /// Array reference filled with all remaining elements.
  ArrayRef<T> &Item;
};

/// Helper that deserializes a numeric leaf into \c Item.
template <typename T> struct serialize_numeric_impl {
  /// Bind the destination numeric value \p Item.
  ///
  /// \param Item Receives the decoded numeric leaf value.
  serialize_numeric_impl(T &Item) : Item(Item) {}

  /// Decode a numeric leaf from \p Reader into \c Item.
  ///
  /// \param Reader Stream positioned at the numeric leaf.
  ///
  /// \returns Success, or an Error if the numeric leaf cannot be decoded.
  Error deserialize(BinaryStreamReader &Reader) const {
    return consume_numeric(Reader, Item);
  }

  /// Receives the decoded numeric leaf value.
  T &Item;
};

/// Build an array serialization helper whose length comes from \p Func.
///
/// \param Item Array reference filled with the deserialized elements.
/// \param Func Callable returning the number of elements to read.
///
/// \returns A helper that deserializes \p Func() elements into \p Item.
template <typename T, typename U>
serialize_array_impl<T, U> serialize_array(ArrayRef<T> &Item, U Func) {
  return serialize_array_impl<T, U>(Item, Func);
}

/// Build a helper that deserializes a sequence of null-terminated strings.
///
/// \param Item Vector that receives each null-terminated string.
///
/// \returns A helper that deserializes null-terminated strings into \p Item.
inline serialize_null_term_string_array_impl
serialize_null_term_string_array(std::vector<StringRef> &Item) {
  return serialize_null_term_string_array_impl(Item);
}

/// Build a helper that deserializes a trailing sequence into a vector.
///
/// \param Item Vector that receives deserialized elements until padding.
///
/// \returns A helper that appends trailing elements into \p Item.
template <typename T>
serialize_vector_tail_impl<T> serialize_array_tail(std::vector<T> &Item) {
  return serialize_vector_tail_impl<T>(Item);
}

/// Build a helper that deserializes the remaining stream as an array of \c T.
///
/// \param Item Array reference filled with all remaining elements.
///
/// \returns A helper that fills \p Item from the remaining stream bytes.
template <typename T>
serialize_arrayref_tail_impl<T> serialize_array_tail(ArrayRef<T> &Item) {
  return serialize_arrayref_tail_impl<T>(Item);
}

/// Build a helper that deserializes a numeric leaf into \p Item.
///
/// \param Item Receives the decoded numeric leaf value.
///
/// \returns A helper that decodes a numeric leaf into \p Item.
template <typename T> serialize_numeric_impl<T> serialize_numeric(T &Item) {
  return serialize_numeric_impl<T>(Item);
}

/// Consume a conditional serialization helper from \p Reader.
///
/// \param Reader Stream to read from.
/// \param Item Conditional serialization helper to deserialize.
///
/// \returns Success, or an Error if deserialization fails.
template <typename T, typename U>
Error consume(BinaryStreamReader &Reader,
              const serialize_conditional_impl<T, U> &Item) {
  return Item.deserialize(Reader);
}

/// Consume an array serialization helper from \p Reader.
///
/// \param Reader Stream to read from.
/// \param Item Array serialization helper to deserialize.
///
/// \returns Success, or an Error if deserialization fails.
template <typename T, typename U>
Error consume(BinaryStreamReader &Reader,
              const serialize_array_impl<T, U> &Item) {
  return Item.deserialize(Reader);
}

/// Consume a null-terminated string-array helper from \p Reader.
///
/// \param Reader Stream to read from.
/// \param Item String-array serialization helper to deserialize.
///
/// \returns Success, or an Error if deserialization fails.
inline Error consume(BinaryStreamReader &Reader,
                     const serialize_null_term_string_array_impl &Item) {
  return Item.deserialize(Reader);
}

/// Consume a vector-tail serialization helper from \p Reader.
///
/// \param Reader Stream to read from.
/// \param Item Vector-tail serialization helper to deserialize.
///
/// \returns Success, or an Error if deserialization fails.
template <typename T>
Error consume(BinaryStreamReader &Reader,
              const serialize_vector_tail_impl<T> &Item) {
  return Item.deserialize(Reader);
}

/// Consume an arrayref-tail serialization helper from \p Reader.
///
/// \param Reader Stream to read from.
/// \param Item Arrayref-tail serialization helper to deserialize.
///
/// \returns Success, or an Error if deserialization fails.
template <typename T>
Error consume(BinaryStreamReader &Reader,
              const serialize_arrayref_tail_impl<T> &Item) {
  return Item.deserialize(Reader);
}

/// Consume a numeric serialization helper from \p Reader.
///
/// \param Reader Stream to read from.
/// \param Item Numeric serialization helper to deserialize.
///
/// \returns Success, or an Error if deserialization fails.
template <typename T>
Error consume(BinaryStreamReader &Reader,
              const serialize_numeric_impl<T> &Item) {
  return Item.deserialize(Reader);
}

/// Consume multiple fields from \p Reader in order.
///
/// \param Reader Stream to read from.
/// \param X First field or helper to consume.
/// \param Y Second field or helper to consume.
/// \param Rest Remaining fields or helpers to consume.
///
/// \returns Success, or an Error if any field cannot be consumed.
template <typename T, typename U, typename... Args>
Error consume(BinaryStreamReader &Reader, T &&X, U &&Y, Args &&... Rest) {
  if (auto EC = consume(Reader, X))
    return EC;
  return consume(Reader, Y, std::forward<Args>(Rest)...);
}

}
}

#endif
