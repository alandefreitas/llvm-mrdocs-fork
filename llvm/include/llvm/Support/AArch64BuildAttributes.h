//===-- AArch64BuildAttributes.h - AARch64 Build Attributes -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains enumerations and support routines for AArch64 build
// attributes as defined in Build Attributes for the AArch64 document.
//
// Build Attributes for the Arm® 64-bit Architecture (AArch64) 2024Q1
//
// https://github.com/ARM-software/abi-aa/pull/230
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_AARCH64BUILDATTRIBUTES_H
#define LLVM_SUPPORT_AARCH64BUILDATTRIBUTES_H

#include "llvm/ADT/StringRef.h"

namespace llvm {

/// Enumerations and helpers for AArch64 ELF build attributes.
namespace AArch64BuildAttributes {

/// AArch64 build attributes vendors IDs (a.k.a subsection name)
enum VendorID : unsigned {
  AEABI_FEATURE_AND_BITS = 0, ///< aeabi_feature_and_bits subsection.
  AEABI_PAUTHABI = 1,         ///< aeabi_pauthabi subsection.
  /// Unknown or private subsection name.
  VENDOR_UNKNOWN = 404
};

/// Return the subsection vendor name for the given vendor ID.
///
/// \param Vendor Vendor identifier (\c VendorID value).
/// \return The vendor name string, or an empty string if unknown.
LLVM_ABI StringRef getVendorName(unsigned const Vendor);

/// Return the vendor ID for the given subsection vendor name.
///
/// \param Vendor Vendor name string (for example "aeabi_pauthabi").
/// \return The matching \c VendorID, or \c VENDOR_UNKNOWN if unrecognized.
LLVM_ABI VendorID getVendorID(StringRef const Vendor);

/// Whether a build-attributes subsection is required or optional.
enum SubsectionOptional : unsigned {
  REQUIRED = 0,           ///< Subsection must be present.
  OPTIONAL = 1,           ///< Subsection may be omitted.
  OPTIONAL_NOT_FOUND = 404 ///< Unknown optionality string.
};

/// Return the optionality spelling for the given subsection optionality ID.
///
/// \param Optional Subsection optionality identifier.
/// \return The optionality string ("required" or "optional"), or empty if unknown.
LLVM_ABI StringRef getOptionalStr(unsigned Optional);

/// Return the subsection optionality ID for the given spelling.
///
/// \param Optional Optionality string ("required" or "optional").
/// \return The matching \c SubsectionOptional, or \c OPTIONAL_NOT_FOUND if unrecognized.
LLVM_ABI SubsectionOptional getOptionalID(StringRef Optional);

/// Return the diagnostic text for an unrecognized subsection optionality.
///
/// \return A human-readable error message for an unknown optionality value.
LLVM_ABI StringRef getSubsectionOptionalUnknownError();

/// Encoding type of values in a build-attributes subsection.
enum SubsectionType : unsigned {
  ULEB128 = 0,         ///< Unsigned LEB128-encoded values.
  NTBS = 1,            ///< Null-terminated byte string values.
  TYPE_NOT_FOUND = 404 ///< Unknown type string.
};

/// Return the type spelling for the given subsection type ID.
///
/// \param Type Subsection type identifier.
/// \return The type string ("uleb128" or "ntbs"), or empty if unknown.
LLVM_ABI StringRef getTypeStr(unsigned Type);

/// Return the subsection type ID for the given spelling.
///
/// \param Type Type string ("uleb128" or "ntbs").
/// \return The matching \c SubsectionType, or \c TYPE_NOT_FOUND if unrecognized.
LLVM_ABI SubsectionType getTypeID(StringRef Type);

/// Return the diagnostic text for an unrecognized subsection type.
///
/// \return A human-readable error message for an unknown type value.
LLVM_ABI StringRef getSubsectionTypeUnknownError();

/// Tags in the aeabi_pauthabi build-attributes subsection.
enum PauthABITags : unsigned {
  TAG_PAUTH_PLATFORM = 1,     ///< Tag_PAuth_Platform.
  TAG_PAUTH_SCHEMA = 2,       ///< Tag_PAuth_Schema.
  PAUTHABI_TAG_NOT_FOUND = 404 ///< Unknown PAuth ABI tag string.
};

/// Return the tag name for the given PAuth ABI tag ID.
///
/// \param PauthABITag PAuth ABI tag identifier.
/// \return The PAuth ABI tag name string, or empty if unknown.
LLVM_ABI StringRef getPauthABITagsStr(unsigned PauthABITag);

/// Return the PAuth ABI tag ID for the given tag name.
///
/// \param PauthABITag PAuth ABI tag name string.
/// \return The matching \c PauthABITags, or \c PAUTHABI_TAG_NOT_FOUND if unrecognized.
LLVM_ABI PauthABITags getPauthABITagsID(StringRef PauthABITag);

/// Tags in the aeabi_feature_and_bits build-attributes subsection.
enum FeatureAndBitsTags : unsigned {
  TAG_FEATURE_BTI = 0, ///< Tag_Feature_BTI (Branch Target Identification).
  TAG_FEATURE_PAC = 1, ///< Tag_Feature_PAC (Pointer Authentication Codes).
  TAG_FEATURE_GCS = 2, ///< Tag_Feature_GCS (Guarded Control Stack).
  FEATURE_AND_BITS_TAG_NOT_FOUND = 404 ///< Unknown feature-and-bits tag string.
};

/// Return the tag name for the given feature-and-bits tag ID.
///
/// \param FeatureAndBitsTag Feature-and-bits tag identifier.
/// \return The feature-and-bits tag name string, or empty if unknown.
LLVM_ABI StringRef getFeatureAndBitsTagsStr(unsigned FeatureAndBitsTag);

/// Return the feature-and-bits tag ID for the given tag name.
///
/// \param FeatureAndBitsTag Feature-and-bits tag name string.
/// \return The matching \c FeatureAndBitsTags, or \c FEATURE_AND_BITS_TAG_NOT_FOUND if unrecognized.
LLVM_ABI FeatureAndBitsTags
getFeatureAndBitsTagsID(StringRef FeatureAndBitsTag);

/// Bit flags describing which AArch64 security features are set.
enum FeatureAndBitsFlag : unsigned {
  Feature_BTI_Flag = 1 << 0, ///< Branch Target Identification is enabled.
  Feature_PAC_Flag = 1 << 1, ///< Pointer Authentication Codes are enabled.
  Feature_GCS_Flag = 1 << 2  ///< Guarded Control Stack is enabled.
};
} // namespace AArch64BuildAttributes
} // namespace llvm

#endif // LLVM_SUPPORT_AARCH64BUILDATTRIBUTES_H
