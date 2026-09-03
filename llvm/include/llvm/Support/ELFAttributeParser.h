//===- ELF AttributeParser.h - ELF Attribute Parser -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ELFATTRIBUTEPARSER_H
#define LLVM_SUPPORT_ELFATTRIBUTEPARSER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/Error.h"

namespace llvm {

/// Abstract parser for ELF build-attribute sections.
///
/// Target-specific subclasses implement compact format (ARM, RISC-V, CSKY,
/// Hexagon, MSP430) or extended format (AArch64). Call \c parse, then query
/// attributes with \c getAttributeValue and \c getAttributeString.
class ELFAttributeParser {
public:
  /// Virtual destructor for polymorphic ELFAttributeParser subclasses.
  virtual ~ELFAttributeParser() = default;

  /// Parse an ELF build-attribute section.
  ///
  /// The default implementation is a no-op that reports success. Compact and
  /// extended subclasses override this to decode the section and populate
  /// attribute lookups.
  ///
  /// \param Section Raw bytes of the attributes section.
  /// \param Endian Byte order of multi-byte fields in \p Section.
  /// \return Success, or an error if the section is malformed.
  virtual Error parse(ArrayRef<uint8_t> Section, llvm::endianness Endian) {
    return llvm::Error::success();
  }
  /// Look up a numeric build-attribute value by subsection name and tag.
  ///
  /// Compact-format parsers require \p BuildAttrSubsectionName to be empty.
  /// Extended-format parsers match the vendor/subsection name. The default
  /// implementation returns \c std::nullopt.
  ///
  /// \param BuildAttrSubsectionName Vendor/subsection name, or empty for
  ///        compact format.
  /// \param Tag Attribute tag number.
  /// \return The integer value, or \c std::nullopt if the tag is absent.
  virtual std::optional<unsigned>
  getAttributeValue(StringRef BuildAttrSubsectionName, unsigned Tag) const {
    return std::nullopt;
  }
  /// Look up a numeric build-attribute value by tag.
  ///
  /// Used by compact-format parsers. Extended-format parsers expect the
  /// overload that also takes a subsection name. The default implementation
  /// returns \c std::nullopt.
  ///
  /// \param Tag Attribute tag number.
  /// \return The integer value, or \c std::nullopt if the tag is absent.
  virtual std::optional<unsigned> getAttributeValue(unsigned Tag) const {
    return std::nullopt;
  }
  /// Look up a string build-attribute value by subsection name and tag.
  ///
  /// Compact-format parsers require \p BuildAttrSubsectionName to be empty.
  /// Extended-format parsers match the vendor/subsection name. The default
  /// implementation returns \c std::nullopt.
  ///
  /// \param BuildAttrSubsectionName Vendor/subsection name, or empty for
  ///        compact format.
  /// \param Tag Attribute tag number.
  /// \return The string value, or \c std::nullopt if the tag is absent.
  virtual std::optional<StringRef>
  getAttributeString(StringRef BuildAttrSubsectionName, unsigned Tag) const {
    return std::nullopt;
  }
  /// Look up a string build-attribute value by tag.
  ///
  /// Used by compact-format parsers. Extended-format parsers expect the
  /// overload that also takes a subsection name. The default implementation
  /// returns \c std::nullopt.
  ///
  /// \param Tag Attribute tag number.
  /// \return The string value, or \c std::nullopt if the tag is absent.
  virtual std::optional<StringRef> getAttributeString(unsigned Tag) const {
    return std::nullopt;
  }
};

} // namespace llvm
#endif // LLVM_SUPPORT_ELFATTRIBUTEPARSER_H
