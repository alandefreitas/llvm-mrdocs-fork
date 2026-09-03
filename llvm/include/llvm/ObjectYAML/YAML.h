//===- YAML.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_YAML_H
#define LLVM_OBJECTYAML_YAML_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>

namespace llvm {

class raw_ostream;

namespace yaml {

/// Specialized YAMLIO scalar type for representing a binary blob.
///
/// A typical use case would be to represent the content of a section in a
/// binary file.
/// This class has custom YAMLIO traits for convenient reading and writing.
/// It renders as a string of hex digits in a YAML file.
/// For example, it might render as `DEADBEEFCAFEBABE` (YAML does not
/// require the quotation marks, so for simplicity when outputting they are
/// omitted).
/// When reading, any string whose content is an even number of hex digits
/// will be accepted.
/// For example, all of the following are acceptable:
/// `DEADBEEF`, `"DeADbEeF"`, `"\x44EADBEEF"` (Note: '\x44' == 'D')
///
/// A significant advantage of using this class is that it never allocates
/// temporary strings or buffers for any of its functionality.
///
/// Example:
///
/// The YAML mapping:
/// \code
/// Foo: DEADBEEFCAFEBABE
/// \endcode
///
/// Could be modeled in YAMLIO by the struct:
/// \code
/// struct FooHolder {
///   BinaryRef Foo;
/// };
/// namespace llvm {
/// namespace yaml {
/// template <>
/// struct MappingTraits<FooHolder> {
///   static void mapping(IO &IO, FooHolder &FH) {
///     IO.mapRequired("Foo", FH.Foo);
///   }
/// };
/// } // end namespace yaml
/// } // end namespace llvm
/// \endcode
class BinaryRef {
  friend bool operator==(const BinaryRef &LHS, const BinaryRef &RHS);

  /// Either raw binary data, or a string of hex bytes (must always
  /// be an even number of characters).
  ArrayRef<uint8_t> Data;

  /// Discriminator between the two states of the `Data` member.
  bool DataIsHexString = true;

public:
  /// Construct an empty BinaryRef.
  BinaryRef() = default;
  /// Construct a BinaryRef that references raw binary data.
  ///
  /// \param Data Raw bytes to reference (not interpreted as hex).
  BinaryRef(ArrayRef<uint8_t> Data) : Data(Data), DataIsHexString(false) {}
  /// Construct a BinaryRef that references a string of hex digits.
  ///
  /// \param Data Hex digit string (must contain an even number of characters).
  BinaryRef(StringRef Data) : Data(arrayRefFromStringRef(Data)) {}

  /// The number of bytes that are represented by this BinaryRef.
  ///
  /// This is the number of bytes that writeAsBinary() will write.
  ///
  /// \returns The number of bytes of binary content.
  ArrayRef<uint8_t>::size_type binary_size() const {
    if (DataIsHexString)
      return Data.size() / 2;
    return Data.size();
  }

  /// Write up to \p N bytes of binary content to \p OS.
  ///
  /// The contents are written as binary regardless of whether this
  /// BinaryRef holds raw binary data or a hex string.
  ///
  /// \param OS Destination stream for the binary bytes.
  /// \param N Maximum number of bytes to write.
  LLVM_ABI void writeAsBinary(raw_ostream &OS, uint64_t N = UINT64_MAX) const;

  /// Write the contents (regardless of whether it is binary or a
  /// hex string) as hex to the given raw_ostream.
  ///
  /// For example, a possible output could be `DEADBEEFCAFEBABE`.
  ///
  /// \param OS Destination stream for the hex digits.
  LLVM_ABI void writeAsHex(raw_ostream &OS) const;
};

/// Compare two BinaryRefs for equality of mode and data.
///
/// Empty default-constructed BinaryRefs compare equal.
///
/// \param LHS Left-hand BinaryRef.
/// \param RHS Right-hand BinaryRef.
/// \returns True if both are empty, or they share the same hex/binary
/// mode and the same underlying data.
inline bool operator==(const BinaryRef &LHS, const BinaryRef &RHS) {
  // Special case for default constructed BinaryRef.
  if (LHS.Data.empty() && RHS.Data.empty())
    return true;

  return LHS.DataIsHexString == RHS.DataIsHexString && LHS.Data == RHS.Data;
}

/// YAMLIO scalar traits specialization for BinaryRef.
template <> struct ScalarTraits<BinaryRef> {
  /// Write \p Val as a hex string to \p Out.
  ///
  /// \param Val BinaryRef to serialize.
  /// \param Ctx Unused client context.
  /// \param Out Destination stream.
  LLVM_ABI static void output(const BinaryRef &Val, void *Ctx,
                              raw_ostream &Out);
  /// Parse hex string \p Scalar into \p Val.
  ///
  /// \param Scalar Hex digit string from YAML.
  /// \param Ctx Unused client context.
  /// \param Val Destination BinaryRef.
  /// \returns Empty StringRef on success, or an error message.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, BinaryRef &Val);
  /// Decide whether \p S must be quoted in YAML output.
  ///
  /// \param S Scalar text to inspect.
  /// \returns The quoting style required for \p S.
  static QuotingType mustQuote(StringRef S) { return needsQuotes(S); }
};

} // end namespace yaml

} // end namespace llvm

#endif // LLVM_OBJECTYAML_YAML_H
