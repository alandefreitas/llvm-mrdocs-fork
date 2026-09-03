//===-- ELFAttributes.h - ELF Attributes ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ELFATTRIBUTES_H
#define LLVM_SUPPORT_ELFATTRIBUTES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {

/// Mapping from an ELF compact build-attribute tag to its display name.
struct TagNameItem {
  unsigned attr;      ///< Attribute tag number.
  StringRef tagName;  ///< Display name for the tag (for example "Tag_CPU_name").
};

/// Array of \c TagNameItem entries for looking up compact build-attribute names.
using TagNameMap = ArrayRef<TagNameItem>;

/// One tag/value pair from an ELF extended build-attributes subsection.
struct BuildAttributeItem {
  /// Encoding kind of the attribute value.
  enum Types : uint8_t {
    NumericAttribute = 0, ///< ULEB128-encoded integer value.
    TextAttribute,        ///< Null-terminated byte string value.
  } Type;                 ///< Value encoding for this attribute.
  unsigned Tag;           ///< Attribute tag number.
  unsigned IntValue;      ///< Integer value when \c Type is \c NumericAttribute.
  std::string StringValue; ///< String value when \c Type is \c TextAttribute.
  /// Construct an attribute item with the given type, tag, and values.
  ///
  /// \param Ty Value encoding (\c NumericAttribute or \c TextAttribute).
  /// \param Tg Attribute tag number.
  /// \param IV Integer value (used for numeric attributes).
  /// \param SV String value (used for text attributes).
  BuildAttributeItem(Types Ty, unsigned Tg, unsigned IV, std::string SV)
      : Type(Ty), Tag(Tg), IntValue(IV), StringValue(std::move(SV)) {}
};

/// One vendor subsection of an ELF extended build-attributes section.
struct BuildAttributeSubSection {
  std::string Name;  ///< Vendor / subsection name (NTBS).
  unsigned IsOptional; ///< 0 = required, 1 = optional.
  unsigned ParameterType; ///< 0 = ULEB128 values, 1 = NTBS values.
  SmallVector<BuildAttributeItem, 64> Content; ///< Attributes in this subsection.
};

/// Mapping from a subsection name and tag to a display name for extended
/// build attributes.
struct SubsectionAndTagToTagName {
  StringRef SubsectionName; ///< Vendor / subsection name.
  unsigned Tag;             ///< Attribute tag number within the subsection.
  StringRef TagName;        ///< Display name for the tag.
};

/// Shared helpers and constants for ELF build-attribute sections.
namespace ELFAttrs {

/// Scope of a compact ELF build-attributes subsection.
enum AttrType : unsigned {
  File = 1,    ///< File-scope subsection.
  Section = 2, ///< Section-scope subsection.
  Symbol = 3   ///< Symbol-scope subsection.
};

/// Return the display name for \p attr from \p tagNameMap.
///
/// \param attr Attribute tag number to look up.
/// \param tagNameMap Map from tag numbers to display names.
/// \param hasTagPrefix If true, return the full name (for example
///        "Tag_CPU_name"); if false, drop the leading \c "Tag_" prefix.
/// \return The display name for \p attr, or an empty string if not found.
LLVM_ABI StringRef attrTypeAsString(unsigned attr, TagNameMap tagNameMap,
                                    bool hasTagPrefix = true);

/// Return the attribute tag number for \p tag from \p tagNameMap.
///
/// \param tag Display name, with or without a leading \c "Tag_" prefix.
/// \param tagNameMap Map from tag numbers to display names.
/// \return The matching tag number, or \c std::nullopt if not found.
LLVM_ABI std::optional<unsigned> attrTypeFromString(StringRef tag,
                                                    TagNameMap tagNameMap);

/// Magic numbers for ELF attributes.
enum AttrMagic {
  Format_Version = 0x41 ///< Format version byte \c 'A' (0x41).
};

} // namespace ELFAttrs
} // namespace llvm
#endif
