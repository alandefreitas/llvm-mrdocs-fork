//===- ELF AttributeParser.h - ELF Attribute Parser -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ELFEXTENDEDATTRPARSER_H
#define LLVM_SUPPORT_ELFEXTENDEDATTRPARSER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/ELFAttributeParser.h"
#include "llvm/Support/ELFAttributes.h"
#include "llvm/Support/Error.h"
#include <optional>
#include <vector>

namespace llvm {
class StringRef;
class ScopedPrinter;

/// Parser for ELF extended build-attribute sections.
class LLVM_ABI ELFExtendedAttrParser : public ELFAttributeParser {
protected:
  /// Printer for attribute comments, or null to suppress output.
  ScopedPrinter *Sw;
  /// Extractor over the section currently being parsed.
  DataExtractor De{ArrayRef<uint8_t>{}, true};
  /// Current offset and sticky error while parsing.
  DataExtractor::Cursor Cursor{0};

  /// Parsed extended ELF build-attribute subsections.
  SmallVector<BuildAttributeSubSection, 8> SubSectionVec;
  /// Map from subsection name and tag to display names used when printing.
  const std::vector<SubsectionAndTagToTagName> TagsNamesMap;
  /// Return the display name for \p Tag in the named subsection.
  ///
  /// \param BuildAttrSubsectionName Vendor / subsection name.
  /// \param Tag Attribute tag number within that subsection.
  /// \return The mapped name, or an empty string if not found.
  StringRef getTagName(const StringRef &BuildAttrSubsectionName,
                       const unsigned Tag);

public:
  /// Destroy the parser and consume any pending cursor error.
  ~ELFExtendedAttrParser() override { static_cast<void>(!Cursor.takeError()); }
  /// Parse an ELF extended build-attributes section.
  ///
  /// Populates \c SubSectionVec and, when \c Sw is non-null, prints each
  /// subsection. The section must start with format-version \c 'A' (0x41).
  ///
  /// \param Section Raw section contents beginning with the format-version
  ///        byte.
  /// \param Endian Endianness of multi-byte fields in \p Section.
  /// \return Success, or an error describing a malformed section.
  Error parse(ArrayRef<uint8_t> Section, llvm::endianness Endian) override;

  /// Unsupported tag-only integer lookup; always returns \c std::nullopt.
  ///
  /// Extended attributes are keyed by subsection name. This overload asserts
  /// and is not used; call the overload that takes a subsection name.
  ///
  /// \param Tag Attribute tag number (unused).
  /// \return Always \c std::nullopt.
  std::optional<unsigned> getAttributeValue(unsigned Tag) const override;
  /// Return the integer value of \p Tag in the named subsection, if present.
  ///
  /// \param BuildAttrSubsectionName Vendor / subsection name to search.
  /// \param Tag Attribute tag number within that subsection.
  /// \return The integer value, or \c std::nullopt if not found.
  std::optional<unsigned> getAttributeValue(StringRef BuildAttrSubsectionName,
                                            unsigned Tag) const override;
  /// Unsupported tag-only string lookup; always returns \c std::nullopt.
  ///
  /// Extended attributes are keyed by subsection name. This overload asserts
  /// and is not used; call the overload that takes a subsection name.
  ///
  /// \param Tag Attribute tag number (unused).
  /// \return Always \c std::nullopt.
  std::optional<StringRef> getAttributeString(unsigned Tag) const override;
  /// Return the string value of \p Tag in the named subsection, if present.
  ///
  /// \param BuildAttrSubsectionName Vendor / subsection name to search.
  /// \param Tag Attribute tag number within that subsection.
  /// \return The string value, or \c std::nullopt if not found.
  std::optional<StringRef> getAttributeString(StringRef BuildAttrSubsectionName,
                                              unsigned Tag) const override;

  /// Construct a parser that prints attribute details to \p Sw.
  ///
  /// \param Sw Printer used for attribute comments, or null to suppress output.
  /// \param TagsNamesMap Map from subsection name and tag to display names.
  ELFExtendedAttrParser(
      ScopedPrinter *Sw,
      const std::vector<SubsectionAndTagToTagName> TagsNamesMap)
      : Sw(Sw), TagsNamesMap(TagsNamesMap) {}
  /// Construct a parser that does not print attribute details.
  ///
  /// \param TagsNamesMap Map from subsection name and tag to display names.
  ELFExtendedAttrParser(
      const std::vector<SubsectionAndTagToTagName> TagsNamesMap)
      : Sw(nullptr), TagsNamesMap(TagsNamesMap) {}
};
} // namespace llvm
#endif // LLVM_SUPPORT_ELFEXTENDEDATTRPARSER_H
