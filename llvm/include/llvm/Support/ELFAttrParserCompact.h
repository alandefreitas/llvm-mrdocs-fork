//===- ELF AttributeParser.h - ELF Attribute Parser -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ELFCOMPACTATTRPARSER_H
#define LLVM_SUPPORT_ELFCOMPACTATTRPARSER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/ELFAttributeParser.h"
#include "llvm/Support/ELFAttributes.h"
#include "llvm/Support/Error.h"

#include <optional>

namespace llvm {
class StringRef;
class ScopedPrinter;

/// Parser for ELF build attributes held in a single compact attributes
/// subsection.
class LLVM_ABI ELFCompactAttrParser : public ELFAttributeParser {
  StringRef vendor;
  DenseMap<unsigned, unsigned> attributes;
  DenseMap<unsigned, StringRef> attributesStr;

  virtual Error handler(uint64_t tag, bool &handled) = 0;

protected:
  ScopedPrinter *sw; ///< Optional printer for decoded attribute details.
  TagNameMap tagToStringMap; ///< Map from attribute tags to display names.
  DataExtractor de{ArrayRef<uint8_t>{}, true}; ///< Extractor over the section.
  DataExtractor::Cursor cursor{0}; ///< Read position within \c de.

  /// Record \p tag / \p value and optionally print them with \p valueDesc.
  ///
  /// \param tag Attribute tag number.
  /// \param value Integer attribute value.
  /// \param valueDesc Human-readable description of \p value, or empty.
  void printAttribute(unsigned tag, unsigned value, StringRef valueDesc);

  /// Parse a ULEB128 attribute whose value indexes into \p strings.
  ///
  /// \param name Attribute name used in error messages.
  /// \param tag Attribute tag number.
  /// \param strings Table of legal value descriptions indexed by the encoded
  ///        integer.
  /// \return Success, or an error if the encoded value is out of range.
  Error parseStringAttribute(const char *name, unsigned tag,
                             ArrayRef<const char *> strings);
  /// Parse a sequence of attributes occupying \p length bytes from the cursor.
  ///
  /// \param length Byte length of the attribute list.
  /// \return Success, or an error if a tag or value is invalid.
  Error parseAttributeList(uint32_t length);
  /// Read a ULEB128-terminated index list into \p indexList.
  ///
  /// \param indexList Destination for non-zero indices until a zero terminator.
  void parseIndexList(SmallVectorImpl<uint8_t> &indexList);
  /// Parse one vendor subsection of \p length bytes starting at the cursor.
  ///
  /// \param length Byte length of the subsection, including the length field.
  /// \return Success, or an error if the subsection is malformed.
  Error parseSubsection(uint32_t length);

  /// Store a string attribute value for later lookup by \p tag.
  ///
  /// \param tag Attribute tag number.
  /// \param value Null-terminated string value from the attributes section.
  void setAttributeString(unsigned tag, StringRef value) {
    attributesStr.try_emplace(tag, value);
  }

public:
  /// Destroy the parser, discarding any pending cursor error.
  ~ELFCompactAttrParser() override { static_cast<void>(!cursor.takeError()); }
  /// Parse a ULEB128 integer attribute and store it under \p tag.
  ///
  /// \param tag Attribute tag number.
  /// \return Success, or an error if reading the value fails.
  Error integerAttribute(unsigned tag);
  /// Parse a null-terminated string attribute and store it under \p tag.
  ///
  /// \param tag Attribute tag number.
  /// \return Success, or an error if reading the value fails.
  Error stringAttribute(unsigned tag);

  /// Construct a parser that prints attribute details to \p sw.
  ///
  /// \param sw Printer used for attribute comments, or null to suppress output.
  /// \param tagNameMap Map from attribute tags to display names.
  /// \param vendor Vendor name string matching subsections this parser handles.
  ELFCompactAttrParser(ScopedPrinter *sw, TagNameMap tagNameMap,
                       StringRef vendor)
      : vendor(vendor), sw(sw), tagToStringMap(tagNameMap) {}
  /// Construct a parser that does not print attribute details.
  ///
  /// \param tagNameMap Map from attribute tags to display names.
  /// \param vendor Vendor name string matching subsections this parser handles.
  ELFCompactAttrParser(TagNameMap tagNameMap, StringRef vendor)
      : vendor(vendor), sw(nullptr), tagToStringMap(tagNameMap) {}

  /// Parse a compact ELF build-attributes \p section with the given \p endian.
  ///
  /// \param section Raw bytes of the attributes section.
  /// \param endian Endianness of multi-byte fields in \p section.
  /// \return Success, or an error if the section is malformed.
  Error parse(ArrayRef<uint8_t> section, llvm::endianness endian) override;

  /// Return the integer value stored for \p tag, if any.
  ///
  /// \param tag Attribute tag number.
  /// \return The stored value, or \c std::nullopt if \p tag was not seen.
  std::optional<unsigned> getAttributeValue(unsigned tag) const override {
    auto I = attributes.find(tag);
    if (I == attributes.end())
      return std::nullopt;
    return I->second;
  }
  /// Return the integer value for \p tag; subsection name must be empty.
  ///
  /// Compact attributes have a single subsection, so
  /// \p buildAttributeSubsectionName must be empty.
  ///
  /// \param buildAttributeSubsectionName Must be an empty string.
  /// \param tag Attribute tag number.
  /// \return The stored value, or \c std::nullopt if \p tag was not seen.
  std::optional<unsigned>
  getAttributeValue(StringRef buildAttributeSubsectionName,
                    unsigned tag) const override {
    assert("" == buildAttributeSubsectionName &&
           "buildAttributeSubsectionName must be an empty string");
    return getAttributeValue(tag);
  }
  /// Return the string value stored for \p tag, if any.
  ///
  /// \param tag Attribute tag number.
  /// \return The stored string, or \c std::nullopt if \p tag was not seen.
  std::optional<StringRef> getAttributeString(unsigned tag) const override {
    auto I = attributesStr.find(tag);
    if (I == attributesStr.end())
      return std::nullopt;
    return I->second;
  }
  /// Return the string value for \p tag; subsection name must be empty.
  ///
  /// Compact attributes have a single subsection, so
  /// \p buildAttributeSubsectionName must be empty.
  ///
  /// \param buildAttributeSubsectionName Must be an empty string.
  /// \param tag Attribute tag number.
  /// \return The stored string, or \c std::nullopt if \p tag was not seen.
  std::optional<StringRef>
  getAttributeString(StringRef buildAttributeSubsectionName,
                     unsigned tag) const override {
    assert("" == buildAttributeSubsectionName &&
           "buildAttributeSubsectionName must be an empty string");
    return getAttributeString(tag);
  }
};

} // namespace llvm
#endif // LLVM_SUPPORT_ELFCOMPACTATTRPARSER_H
