//===- BitCodes.h - Enum values for the bitstream format --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines bitstream enum values.
//
// The enum values defined in this file should be considered permanent.  If
// new features are added, they should have values added at the end of the
// respective lists.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITSTREAM_BITCODES_H
#define LLVM_BITSTREAM_BITCODES_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitstream/BitCodeEnums.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>

namespace llvm {
/// One or more operands in a bitstream abbreviation.
///
/// This is actually a union of two different things:
///   1. It could be a literal integer value ("the operand is always 17").
///   2. It could be an encoding specification ("this operand encoded like so").
class BitCodeAbbrevOp {
public:
  /// How a non-literal abbreviation operand is encoded in the bitstream.
  enum Encoding {
    Fixed = 1,  ///< A fixed width field, Val specifies number of bits.
    VBR   = 2,  ///< A VBR field where Val specifies the width of each chunk.
    Array = 3,  ///< A sequence of fields, next field species elt encoding.
    Char6 = 4,  ///< A 6-bit fixed field which maps to [a-zA-Z0-9._].
    Blob  = 5   ///< 32-bit aligned array of 8-bit characters.
  };

protected:
  uint64_t Val;           ///< A literal value or data for an encoding.
  /// True if this operand is a literal value.
  LLVM_PREFERRED_TYPE(bool)
  uint64_t IsLiteral : 1; // Indicate whether this is a literal value or not.
  /// Encoding kind when IsLiteral is false.
  LLVM_PREFERRED_TYPE(Encoding)
  uint64_t Enc : 3;       // The encoding to use.

public:
  /// True if \p E is a valid Encoding enumerator (Fixed..Blob).
  ///
  /// \param E Encoding value to validate.
  /// \return True if \p E is a valid Encoding enumerator (Fixed..Blob).
  static bool isValidEncoding(uint64_t E) {
    return E >= 1 && E <= 5;
  }

  /// Construct a literal abbreviation operand with value \p V.
  ///
  /// \param V Literal integer value for this operand.
  explicit BitCodeAbbrevOp(uint64_t V) :  Val(V), IsLiteral(true) {}
  /// Construct an encoding abbreviation operand of kind \p E with optional data.
  ///
  /// \param E Encoding kind for this operand.
  /// \param Data Encoding-specific data (e.g. bit width for Fixed/VBR).
  explicit BitCodeAbbrevOp(Encoding E, uint64_t Data = 0)
    : Val(Data), IsLiteral(false), Enc(E) {}

  /// True if this operand is a fixed literal value.
  ///
  /// \return True if this operand is a fixed literal value.
  bool isLiteral() const  { return IsLiteral; }
  /// True if this operand specifies an encoding rather than a literal.
  ///
  /// \return True if this operand specifies an encoding rather than a literal.
  bool isEncoding() const { return !IsLiteral; }

  // Accessors for literals.
  /// Return the literal integer value (requires isLiteral()).
  ///
  /// \return The literal integer value (requires isLiteral()).
  uint64_t getLiteralValue() const { assert(isLiteral()); return Val; }

  // Accessors for encoding info.
  /// Return the encoding kind when this operand is an encoding (not a literal).
  ///
  /// \return The encoding kind when this operand is an encoding (not a literal).
  Encoding getEncoding() const { assert(isEncoding()); return (Encoding)Enc; }
  /// Return encoding-specific data (e.g. bit width for Fixed/VBR).
  ///
  /// \return Encoding-specific data (e.g. bit width for Fixed/VBR).
  uint64_t getEncodingData() const {
    assert(isEncoding() && hasEncodingData());
    return Val;
  }

  /// True if this encoding operand carries additional data in Val.
  ///
  /// \return True if this encoding operand carries additional data in Val.
  bool hasEncodingData() const { return hasEncodingData(getEncoding()); }
  /// True if encoding \p E stores extra data (Fixed and VBR do).
  ///
  /// \param E Encoding kind to query.
  /// \return True if encoding \p E stores extra data (Fixed and VBR do).
  static bool hasEncodingData(Encoding E) {
    switch (E) {
    case Fixed:
    case VBR:
      return true;
    case Array:
    case Char6:
    case Blob:
      return false;
    }
    report_fatal_error("Invalid encoding");
  }

  /// Return true if this character is legal in the Char6 encoding.
  ///
  /// \param C Character to test.
  /// \return True if \p C is legal in the Char6 encoding.
  static bool isChar6(char C) { return isAlnum(C) || C == '.' || C == '_'; }
  /// Encode a Char6 character as a 6-bit value.
  ///
  /// \param C Character in [a-zA-Z0-9._] to encode.
  /// \return The 6-bit encoding of \p C.
  static unsigned EncodeChar6(char C) {
    if (C >= 'a' && C <= 'z') return C-'a';
    if (C >= 'A' && C <= 'Z') return C-'A'+26;
    if (C >= '0' && C <= '9') return C-'0'+26+26;
    if (C == '.')             return 62;
    if (C == '_')             return 63;
    llvm_unreachable("Not a value Char6 character!");
  }

  /// Decode a 6-bit Char6 value to its character.
  ///
  /// \param V Encoded Char6 value in the range [0, 63].
  /// \return The decoded character corresponding to \p V.
  static char DecodeChar6(unsigned V) {
    assert((V & ~63) == 0 && "Not a Char6 encoded character!");
    return "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._"
        [V];
  }

};

/// Abbreviation record that stores a complex record in a specialized format.
///
/// An abbreviation allows a complex record that has redundancy to be stored in a
/// specialized format instead of the fully-general, fully-vbr, format.
class BitCodeAbbrev {
  SmallVector<BitCodeAbbrevOp, 32> OperandList;

public:
  /// Construct an empty abbreviation with no operands.
  BitCodeAbbrev() = default;

  /// Construct an abbreviation from the given operand list.
  ///
  /// \param OperandList Initial operands for this abbreviation.
  explicit BitCodeAbbrev(std::initializer_list<BitCodeAbbrevOp> OperandList)
      : OperandList(OperandList) {}

  /// Return the number of operands in this abbreviation.
  ///
  /// \return The number of operands in this abbreviation.
  unsigned getNumOperandInfos() const {
    return static_cast<unsigned>(OperandList.size());
  }
  /// Return the operand descriptor at index \p N.
  ///
  /// \param N Zero-based index into the operand list.
  /// \return The operand descriptor at index \p N.
  const BitCodeAbbrevOp &getOperandInfo(unsigned N) const {
    return OperandList[N];
  }

  /// Append an operand descriptor to this abbreviation.
  ///
  /// \param OpInfo Operand descriptor to append.
  void Add(const BitCodeAbbrevOp &OpInfo) {
    OperandList.push_back(OpInfo);
  }
};
} // namespace llvm

#endif
