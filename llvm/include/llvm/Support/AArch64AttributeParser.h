//=== - AArch64AttributeParser.h-AArch64 Attribute Information Printer - ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===--------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_AARCH64ATTRIBUTEPARSER_H
#define LLVM_SUPPORT_AARCH64ATTRIBUTEPARSER_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/ELFAttrParserExtended.h"
#include "llvm/Support/ELFAttributes.h"

namespace llvm {

/// Parser for AArch64 ELF build attributes.
class AArch64AttributeParser : public ELFExtendedAttrParser {
  LLVM_ABI static std::vector<SubsectionAndTagToTagName> &returnTagsNamesMap();

public:
  /// Construct a parser that prints attribute details to \p Sw.
  ///
  /// \param Sw Printer used for attribute comments, or null to suppress output.
  AArch64AttributeParser(ScopedPrinter *Sw)
      : ELFExtendedAttrParser(Sw, returnTagsNamesMap()) {}
  /// Construct a parser that does not print attribute details.
  AArch64AttributeParser()
      : ELFExtendedAttrParser(nullptr, returnTagsNamesMap()) {}
};

/// Extracted AArch64 build-attribute subsection values.
///
/// Used when pulling aeabi_pauthabi and aeabi_feature_and_bits data out of a
/// parsed attribute section.
struct AArch64BuildAttrSubsections {
  /// Values from the aeabi_pauthabi subsection.
  struct PauthSubSection {
    /// Tag_PAuth_Platform value, or 0 when absent.
    uint64_t TagPlatform = 0;
    /// Tag_PAuth_Schema value, or 0 when absent.
    uint64_t TagSchema = 0;
  } Pauth; ///< Pointer-authentication ABI tags from aeabi_pauthabi.
  /// Bitmask of Tag_Feature_BTI/PAC/GCS from aeabi_feature_and_bits.
  uint32_t AndFeatures = 0;
};

/// Extract AArch64 build-attribute subsections from a parsed attribute section.
///
/// \param Attributes Parsed AArch64 attribute section to read from.
/// \return The PAuth and feature-and-bits subsection values found in
/// \p Attributes, with missing tags left at zero.
LLVM_ABI AArch64BuildAttrSubsections
extractBuildAttributesSubsections(const llvm::AArch64AttributeParser &Attributes);
} // namespace llvm

#endif // LLVM_SUPPORT_AARCH64ATTRIBUTEPARSER_H
