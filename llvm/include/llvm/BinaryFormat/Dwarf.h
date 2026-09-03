//===-- llvm/BinaryFormat/Dwarf.h ---Dwarf Constants-------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains constants used for implementing Dwarf
/// debug support.
///
/// For details on the Dwarf specfication see the latest DWARF Debugging
/// Information Format standard document on http://www.dwarfstd.org. This
/// file often includes support for non-released standard features.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_DWARF_H
#define LLVM_BINARYFORMAT_DWARF_H

#include "llvm/Support/AMDGPUAddrSpace.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadicDetails.h"
#include "llvm/TargetParser/Triple.h"

#include <limits>

namespace llvm {
class StringRef;

/// DWARF constants, encodings, and helpers for debug-info consumers and producers.
namespace dwarf {

//===----------------------------------------------------------------------===//
// DWARF constants as gleaned from the DWARF Debugging Information Format V.5
// reference manual http://www.dwarfstd.org/.
//

// Do not mix the following two enumerations sets.  DW_TAG_invalid changes the
// enumeration base type.

/// LLVM-specific DWARF constants and vendor identifiers.
enum LLVMConstants : uint32_t {
  /// LLVM mock tags (see also llvm/BinaryFormat/Dwarf.def).
  /// \{
  DW_TAG_invalid = ~0U,             ///< Tag for invalid results.
  DW_VIRTUALITY_invalid = ~0U,      ///< Virtuality for invalid results.
  DW_MACINFO_invalid = ~0U,         ///< Macinfo type for invalid results.
  DW_APPLE_ENUM_KIND_invalid = ~0U, ///< Enum kind for invalid results.
  /// \}

  /// Special values for an initial length field.
  /// \{
  DW_LENGTH_lo_reserved = 0xfffffff0, ///< Lower bound of the reserved range.
  DW_LENGTH_DWARF64 = 0xffffffff,     ///< Indicator of 64-bit DWARF format.
  DW_LENGTH_hi_reserved = 0xffffffff, ///< Upper bound of the reserved range.
  /// \}

  /// Other constants.
  /// \{
  DWARF_VERSION = 4,       ///< Default dwarf version we output.
  DW_PUBTYPES_VERSION = 2, ///< Section version number for .debug_pubtypes.
  DW_PUBNAMES_VERSION = 2, ///< Section version number for .debug_pubnames.
  DW_ARANGES_VERSION = 2,  ///< Section version number for .debug_aranges.
  /// \}

  /// Identifiers we use to distinguish vendor extensions.
  /// \{
  DWARF_VENDOR_DWARF = 0, ///< Defined in v2 or later of the DWARF standard.
  DWARF_VENDOR_APPLE = 1,   ///< Apple vendor extensions.
  DWARF_VENDOR_BORLAND = 2, ///< Borland vendor extensions.
  DWARF_VENDOR_GNU = 3,     ///< GNU vendor extensions.
  DWARF_VENDOR_GOOGLE = 4,  ///< Google vendor extensions.
  DWARF_VENDOR_LLVM = 5,    ///< LLVM vendor extensions.
  DWARF_VENDOR_MIPS = 6,    ///< MIPS vendor extensions.
  DWARF_VENDOR_WASM = 7,    ///< WebAssembly vendor extensions.
  DWARF_VENDOR_ALTIUM,      ///< Altium vendor extensions.
  DWARF_VENDOR_COMPAQ,      ///< Compaq vendor extensions.
  DWARF_VENDOR_GHS,         ///< Green Hills Software vendor extensions.
  DWARF_VENDOR_GO,          ///< Go vendor extensions.
  DWARF_VENDOR_HP,          ///< Hewlett-Packard vendor extensions.
  DWARF_VENDOR_IBM,         ///< IBM vendor extensions.
  DWARF_VENDOR_INTEL,       ///< Intel vendor extensions.
  DWARF_VENDOR_PGI,         ///< Portland Group vendor extensions.
  DWARF_VENDOR_SUN,         ///< Sun Microsystems vendor extensions.
  DWARF_VENDOR_UPC,         ///< Unified Parallel C vendor extensions.
  ///\}
};

/// Constants that define the DWARF format as 32 or 64 bit.
enum DwarfFormat : uint8_t {
  DWARF32, ///< 32-bit DWARF format (4-byte offsets).
  DWARF64  ///< 64-bit DWARF format (8-byte offsets).
};

/// Special ID values that distinguish a CIE from a FDE in DWARF CFI.
/// Not inside an enum because a 64-bit value is needed.
/// @{
const uint32_t DW_CIE_ID = UINT32_MAX;
/// CIE identifier used in the 64-bit DWARF format.
const uint64_t DW64_CIE_ID = UINT64_MAX;
/// @}

/// Identifier of an invalid DIE offset in the .debug_info section.
const uint32_t DW_INVALID_OFFSET = UINT32_MAX;

/// DIE tag encodings (DW_TAG_*).
enum Tag : uint16_t {
#define HANDLE_DW_TAG(ID, NAME, VERSION, VENDOR, KIND) DW_TAG_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_TAG_lo_user = 0x4080, ///< Lower bound of the user tag range.
  DW_TAG_hi_user = 0xffff, ///< Upper bound of the user tag range.
  DW_TAG_user_base = 0x1000 ///< Recommended base for user tags.
};

/// Return true if \p T is a DWARF type tag (as classified in Dwarf.def).
///
/// \param T DWARF tag to test.
/// \return True if \p T is a type tag.
inline bool isType(Tag T) {
  switch (T) {
  default:
    return false;
#define HANDLE_DW_TAG(ID, NAME, VERSION, VENDOR, KIND)                         \
  case DW_TAG_##NAME:                                                          \
    return (KIND == DW_KIND_TYPE);
#include "llvm/BinaryFormat/Dwarf.def"
  }
}

/// Attributes.
enum Attribute : uint16_t {
#define HANDLE_DW_AT(ID, NAME, VERSION, VENDOR) DW_AT_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_AT_lo_user = 0x2000, ///< Lower bound of the user attribute range.
  DW_AT_hi_user = 0x3fff, ///< Upper bound of the user attribute range.
};

/// Form encodings (DW_FORM_*).
enum Form : uint16_t {
#define HANDLE_DW_FORM(ID, NAME, VERSION, VENDOR) DW_FORM_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_FORM_lo_user = 0x1f00, ///< Not specified by DWARF.
};

/// Location expression operation encodings (DW_OP_*).
enum LocationAtom {
#define HANDLE_DW_OP(ID, NAME, OPERANDS, ARITY, VERSION, VENDOR)               \
  DW_OP_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_OP_lo_user = 0xe0, ///< Lower bound of the user operation range.
  DW_OP_hi_user = 0xff, ///< Upper bound of the user operation range.
  DW_OP_LLVM_fragment = 0x1000,          ///< Only used in LLVM metadata.
  DW_OP_LLVM_convert = 0x1001,           ///< Only used in LLVM metadata.
  DW_OP_LLVM_tag_offset = 0x1002,        ///< Only used in LLVM metadata.
  DW_OP_LLVM_entry_value = 0x1003,       ///< Only used in LLVM metadata.
  DW_OP_LLVM_implicit_pointer = 0x1004,  ///< Only used in LLVM metadata.
  DW_OP_LLVM_arg = 0x1005,               ///< Only used in LLVM metadata.
  DW_OP_LLVM_extract_bits_sext = 0x1006, ///< Only used in LLVM metadata.
  DW_OP_LLVM_extract_bits_zext = 0x1007, ///< Only used in LLVM metadata.
};

/// LLVM-specific DWARF location atoms from the DW_OP_LLVM_* user range.
enum LlvmUserLocationAtom {
#define HANDLE_DW_OP_LLVM_USEROP(ID, NAME) DW_OP_LLVM_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// Base type encoding attribute values (DW_ATE_*).
enum TypeKind : uint8_t {
#define HANDLE_DW_ATE(ID, NAME, VERSION, VENDOR) DW_ATE_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_ATE_lo_user = 0x80, ///< Lower bound of the user type encoding range.
  DW_ATE_hi_user = 0xff  ///< Upper bound of the user type encoding range.
};

/// Decimal sign attribute encodings (DW_AT_decimal_sign).
enum DecimalSignEncoding {
  // Decimal sign attribute values
  DW_DS_unsigned = 0x01,              ///< Unsigned decimal value (no sign).
  DW_DS_leading_overpunch = 0x02,     ///< Sign encoded as a leading overpunch.
  DW_DS_trailing_overpunch = 0x03,    ///< Sign encoded as a trailing overpunch.
  DW_DS_leading_separate = 0x04,      ///< Sign is a separate leading character.
  DW_DS_trailing_separate = 0x05      ///< Sign is a separate trailing character.
};

/// Endianity attribute value encodings.
enum EndianityEncoding {
  // Endianity attribute values
#define HANDLE_DW_END(ID, NAME) DW_END_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_END_lo_user = 0x40, ///< Lower bound of the user endianity range.
  DW_END_hi_user = 0xff  ///< Upper bound of the user endianity range.
};

/// Accessibility attribute encodings (DW_AT_accessibility).
enum AccessAttribute {
  // Accessibility codes
  DW_ACCESS_public = 0x01,    ///< Public accessibility.
  DW_ACCESS_protected = 0x02, ///< Protected accessibility.
  DW_ACCESS_private = 0x03    ///< Private accessibility.
};

/// Visibility attribute encodings (DW_AT_visibility).
enum VisibilityAttribute {
  // Visibility codes
  DW_VIS_local = 0x01,     ///< Local (not exported) visibility.
  DW_VIS_exported = 0x02,  ///< Exported visibility.
  DW_VIS_qualified = 0x03  ///< Qualified visibility.
};

/// Virtuality attribute encodings.
enum VirtualityAttribute {
#define HANDLE_DW_VIRTUALITY(ID, NAME) DW_VIRTUALITY_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_VIRTUALITY_max = 0x02 ///< Highest defined virtuality value.
};

/// Apple enum-kind attribute encodings (DW_AT_APPLE_enum_kind).
enum EnumKindAttribute {
#define HANDLE_DW_APPLE_ENUM_KIND(ID, NAME) DW_APPLE_ENUM_KIND_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_APPLE_ENUM_KIND_max = 0x01 ///< Highest defined enum kind value.
};

/// LLVM language-dialect attribute encodings (DW_AT_LLVM_language_dialect).
enum LanguageDialectAttribute {
#define HANDLE_DW_LLVM_LANG_DIALECT(ID, NAME) DW_LLVM_LANG_DIALECT_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_LLVM_LANG_DIALECT_max = 0x02 ///< Highest defined language dialect value.
};

/// DWARF defaulted-member attribute encodings (DW_DEFAULTED_*).
enum DefaultedMemberAttribute {
#define HANDLE_DW_DEFAULTED(ID, NAME) DW_DEFAULTED_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_DEFAULTED_max = 0x02 ///< Highest defined defaulted-member value.
};

/// Source language encodings (DW_LANG_*).
enum SourceLanguage {
#define HANDLE_DW_LANG(ID, NAME, LOWER_BOUND, VERSION, VENDOR)                 \
  DW_LANG_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_LANG_lo_user = 0x8000, ///< Lower bound of the user language range.
  DW_LANG_hi_user = 0xffff  ///< Upper bound of the user language range.
};

/// DWARF 6 source language name encodings (DW_LNAME_*).
enum SourceLanguageName : uint16_t {
#define HANDLE_DW_LNAME(ID, NAME, DESC, LOWER_BOUND) DW_LNAME_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// Convert a DWARF 6 language name and version to a DWARF 5 DW_LANG value.
///
/// If the version number doesn't exactly match a known version it is
/// rounded up to the next-highest known version number.
///
/// \param name DWARF 6 source language name (DW_LNAME_*).
/// \param version Language version number in the encoding used by DWARF 6.
/// \return Matching DW_LANG_* value, or std::nullopt if unrecognized.
inline std::optional<SourceLanguage> toDW_LANG(SourceLanguageName name,
                                               uint32_t version) {
  switch (name) {
  case DW_LNAME_Ada: // YYYY
    if (version <= 1983)
      return DW_LANG_Ada83;
    if (version <= 1995)
      return DW_LANG_Ada95;
    if (version <= 2005)
      return DW_LANG_Ada2005;
    if (version <= 2012)
      return DW_LANG_Ada2012;
    return {};
  case DW_LNAME_BLISS:
    return DW_LANG_BLISS;
  case DW_LNAME_C: // YYYYMM, K&R 000000
    if (version == 0)
      return DW_LANG_C;
    if (version <= 198912)
      return DW_LANG_C89;
    if (version <= 199901)
      return DW_LANG_C99;
    if (version <= 201112)
      return DW_LANG_C11;
    if (version <= 201710)
      return DW_LANG_C17;
    if (version <= 202311)
      return DW_LANG_C23;
    return {};
  case DW_LNAME_C_plus_plus: // YYYYMM
    if (version == 0)
      return DW_LANG_C_plus_plus;
    if (version <= 199711)
      return DW_LANG_C_plus_plus;
    if (version <= 200310)
      return DW_LANG_C_plus_plus_03;
    if (version <= 201103)
      return DW_LANG_C_plus_plus_11;
    if (version <= 201402)
      return DW_LANG_C_plus_plus_14;
    if (version <= 201703)
      return DW_LANG_C_plus_plus_17;
    if (version <= 202002)
      return DW_LANG_C_plus_plus_20;
    if (version <= 202302)
      return DW_LANG_C_plus_plus_23;
    return {};
  case DW_LNAME_Cobol: // YYYY
    if (version <= 1974)
      return DW_LANG_Cobol74;
    if (version <= 1985)
      return DW_LANG_Cobol85;
    return {};
  case DW_LNAME_Crystal:
    return DW_LANG_Crystal;
  case DW_LNAME_D:
    return DW_LANG_D;
  case DW_LNAME_Dylan:
    return DW_LANG_Dylan;
  case DW_LNAME_Fortran: // YYYY
    if (version <= 1977)
      return DW_LANG_Fortran77;
    if (version <= 1990)
      return DW_LANG_Fortran90;
    if (version <= 1995)
      return DW_LANG_Fortran95;
    if (version <= 2003)
      return DW_LANG_Fortran03;
    if (version <= 2008)
      return DW_LANG_Fortran08;
    if (version <= 2018)
      return DW_LANG_Fortran18;
    if (version <= 2023)
      return DW_LANG_Fortran23;
    return {};
  case DW_LNAME_Go:
    return DW_LANG_Go;
  case DW_LNAME_Haskell:
    return DW_LANG_Haskell;
  case DW_LNAME_HIP:
    return DW_LANG_HIP;
  case DW_LNAME_Odin:
    return DW_LANG_Odin;
  case DW_LNAME_P4:
    return DW_LANG_P4;
  case DW_LNAME_Java:
    return DW_LANG_Java;
  case DW_LNAME_Julia:
    return DW_LANG_Julia;
  case DW_LNAME_Kotlin:
    return DW_LANG_Kotlin;
  case DW_LNAME_Modula2:
    return DW_LANG_Modula2;
  case DW_LNAME_Modula3:
    return DW_LANG_Modula3;
  case DW_LNAME_ObjC:
    return DW_LANG_ObjC;
  case DW_LNAME_ObjC_plus_plus:
    return DW_LANG_ObjC_plus_plus;
  case DW_LNAME_OCaml:
    return DW_LANG_OCaml;
  case DW_LNAME_OpenCL_C:
    return DW_LANG_OpenCL;
  case DW_LNAME_Pascal:
    return DW_LANG_Pascal83;
  case DW_LNAME_PLI:
    return DW_LANG_PLI;
  case DW_LNAME_Python:
    return DW_LANG_Python;
  case DW_LNAME_RenderScript:
    return DW_LANG_RenderScript;
  case DW_LNAME_Rust:
    return DW_LANG_Rust;
  case DW_LNAME_Swift:
    return DW_LANG_Swift;
  case DW_LNAME_UPC:
    return DW_LANG_UPC;
  case DW_LNAME_Zig:
    return DW_LANG_Zig;
  case DW_LNAME_Assembly:
    return DW_LANG_Assembly;
  case DW_LNAME_C_sharp:
    return DW_LANG_C_sharp;
  case DW_LNAME_Mojo:
    return DW_LANG_Mojo;
  case DW_LNAME_GLSL:
    return DW_LANG_GLSL;
  case DW_LNAME_GLSL_ES:
    return DW_LANG_GLSL_ES;
  case DW_LNAME_HLSL:
    return DW_LANG_HLSL;
  case DW_LNAME_OpenCL_CPP:
    return DW_LANG_OpenCL_CPP;
  case DW_LNAME_CPP_for_OpenCL:
    return {};
  case DW_LNAME_SYCL:
    return DW_LANG_SYCL;
  case DW_LNAME_Ruby:
    return DW_LANG_Ruby;
  case DW_LNAME_Move:
    return DW_LANG_Move;
  case DW_LNAME_Hylo:
    return DW_LANG_Hylo;
  case DW_LNAME_Metal:
    return DW_LANG_Metal;
  case DW_LNAME_V:
    return DW_LANG_V;
  case DW_LNAME_Algol68:
    return DW_LANG_Algol68;
  case DW_LNAME_Nim:
    return DW_LANG_Nim;
  case DW_LNAME_Erlang:
    return DW_LANG_Erlang;
  case DW_LNAME_Elixir:
    return DW_LANG_Elixir;
  case DW_LNAME_Gleam:
    return DW_LANG_Gleam;
  }
  return {};
}

/// Convert a DWARF 5 DW_LANG to a DWARF 6 pair of language name and version.
///
/// \param language DWARF 5 source language encoding (DW_LANG_*).
/// \return Pair of DW_LNAME_* and version, or std::nullopt if unrecognized.
inline std::optional<std::pair<SourceLanguageName, uint32_t>>
toDW_LNAME(SourceLanguage language) {
  switch (language) {
  case DW_LANG_Ada83:
    return {{DW_LNAME_Ada, 1983}};
  case DW_LANG_Ada95:
    return {{DW_LNAME_Ada, 1995}};
  case DW_LANG_Ada2005:
    return {{DW_LNAME_Ada, 2005}};
  case DW_LANG_Ada2012:
    return {{DW_LNAME_Ada, 2012}};
  case DW_LANG_BLISS:
    return {{DW_LNAME_BLISS, 0}};
  case DW_LANG_C:
    return {{DW_LNAME_C, 0}};
  case DW_LANG_C89:
    return {{DW_LNAME_C, 198912}};
  case DW_LANG_C99:
    return {{DW_LNAME_C, 199901}};
  case DW_LANG_C11:
    return {{DW_LNAME_C, 201112}};
  case DW_LANG_C17:
    return {{DW_LNAME_C, 201710}};
  case DW_LANG_C23:
    return {{DW_LNAME_C, 202311}};
  case DW_LANG_C_plus_plus:
    return {{DW_LNAME_C_plus_plus, 0}};
  case DW_LANG_C_plus_plus_03:
    return {{DW_LNAME_C_plus_plus, 200310}};
  case DW_LANG_C_plus_plus_11:
    return {{DW_LNAME_C_plus_plus, 201103}};
  case DW_LANG_C_plus_plus_14:
    return {{DW_LNAME_C_plus_plus, 201402}};
  case DW_LANG_C_plus_plus_17:
    return {{DW_LNAME_C_plus_plus, 201703}};
  case DW_LANG_C_plus_plus_20:
    return {{DW_LNAME_C_plus_plus, 202002}};
  case DW_LANG_C_plus_plus_23:
    return {{DW_LNAME_C_plus_plus, 202302}};
  case DW_LANG_Cobol74:
    return {{DW_LNAME_Cobol, 1974}};
  case DW_LANG_Cobol85:
    return {{DW_LNAME_Cobol, 1985}};
  case DW_LANG_Crystal:
    return {{DW_LNAME_Crystal, 0}};
  case DW_LANG_D:
    return {{DW_LNAME_D, 0}};
  case DW_LANG_Dylan:
    return {{DW_LNAME_Dylan, 0}};
  case DW_LANG_Fortran77:
    return {{DW_LNAME_Fortran, 1977}};
  case DW_LANG_Fortran90:
    return {{DW_LNAME_Fortran, 1990}};
  case DW_LANG_Fortran95:
    return {{DW_LNAME_Fortran, 1995}};
  case DW_LANG_Fortran03:
    return {{DW_LNAME_Fortran, 2003}};
  case DW_LANG_Fortran08:
    return {{DW_LNAME_Fortran, 2008}};
  case DW_LANG_Fortran18:
    return {{DW_LNAME_Fortran, 2018}};
  case DW_LANG_Fortran23:
    return {{DW_LNAME_Fortran, 2023}};
  case DW_LANG_Go:
    return {{DW_LNAME_Go, 0}};
  case DW_LANG_Haskell:
    return {{DW_LNAME_Haskell, 0}};
  case DW_LANG_HIP:
    return {{DW_LNAME_HIP, 0}};
  case DW_LANG_Odin:
    return {{DW_LNAME_Odin, 0}};
  case DW_LANG_P4:
    return {{DW_LNAME_P4, 0}};
  case DW_LANG_Java:
    return {{DW_LNAME_Java, 0}};
  case DW_LANG_Julia:
    return {{DW_LNAME_Julia, 0}};
  case DW_LANG_Kotlin:
    return {{DW_LNAME_Kotlin, 0}};
  case DW_LANG_Modula2:
    return {{DW_LNAME_Modula2, 0}};
  case DW_LANG_Modula3:
    return {{DW_LNAME_Modula3, 0}};
  case DW_LANG_ObjC:
    return {{DW_LNAME_ObjC, 0}};
  case DW_LANG_ObjC_plus_plus:
    return {{DW_LNAME_ObjC_plus_plus, 0}};
  case DW_LANG_OCaml:
    return {{DW_LNAME_OCaml, 0}};
  case DW_LANG_OpenCL:
    return {{DW_LNAME_OpenCL_C, 0}};
  case DW_LANG_Pascal83:
    return {{DW_LNAME_Pascal, 1983}};
  case DW_LANG_PLI:
    return {{DW_LNAME_PLI, 0}};
  case DW_LANG_Python:
    return {{DW_LNAME_Python, 0}};
  case DW_LANG_RenderScript:
  case DW_LANG_GOOGLE_RenderScript:
    return {{DW_LNAME_RenderScript, 0}};
  case DW_LANG_Rust:
    return {{DW_LNAME_Rust, 0}};
  case DW_LANG_Swift:
    return {{DW_LNAME_Swift, 0}};
  case DW_LANG_UPC:
    return {{DW_LNAME_UPC, 0}};
  case DW_LANG_Zig:
    return {{DW_LNAME_Zig, 0}};
  case DW_LANG_Assembly:
  case DW_LANG_Mips_Assembler:
    return {{DW_LNAME_Assembly, 0}};
  case DW_LANG_C_sharp:
    return {{DW_LNAME_C_sharp, 0}};
  case DW_LANG_Mojo:
    return {{DW_LNAME_Mojo, 0}};
  case DW_LANG_GLSL:
    return {{DW_LNAME_GLSL, 0}};
  case DW_LANG_GLSL_ES:
    return {{DW_LNAME_GLSL_ES, 0}};
  case DW_LANG_HLSL:
    return {{DW_LNAME_HLSL, 0}};
  case DW_LANG_OpenCL_CPP:
    return {{DW_LNAME_OpenCL_CPP, 0}};
  case DW_LANG_SYCL:
    return {{DW_LNAME_SYCL, 0}};
  case DW_LANG_Ruby:
    return {{DW_LNAME_Ruby, 0}};
  case DW_LANG_Move:
    return {{DW_LNAME_Move, 0}};
  case DW_LANG_Hylo:
    return {{DW_LNAME_Hylo, 0}};
  case DW_LANG_Metal:
    return {{DW_LNAME_Metal, 0}};
  case DW_LANG_V:
    return {{DW_LNAME_V, 0}};
  case DW_LANG_Algol68:
    return {{DW_LNAME_Algol68, 1968}};
  case DW_LANG_Nim:
    return {{DW_LNAME_Nim, 0}};
  case DW_LANG_Erlang:
    return {{DW_LNAME_Erlang, 0}};
  case DW_LANG_Elixir:
    return {{DW_LNAME_Elixir, 0}};
  case DW_LANG_Gleam:
    return {{DW_LNAME_Gleam, 0}};
  case DW_LANG_BORLAND_Delphi:
  case DW_LANG_CPP_for_OpenCL:
  case DW_LANG_lo_user:
  case DW_LANG_hi_user:
    return {};
  }
  return {};
}

/// Returns a version-independent language name.
///
/// \param name DWARF 6 source language name (DW_LNAME_*).
/// \return Version-independent display name for \p name.
LLVM_ABI llvm::StringRef LanguageDescription(SourceLanguageName name);

/// Return a version-specific display name for a DWARF 6 language.
///
/// If the version is not recognized for the specified language, returns
/// the version-independent name.
///
/// \param Name DWARF 6 source language name (DW_LNAME_*).
/// \param Version Language version number in the encoding used by DWARF 6.
/// \return Version-specific display name, or the version-independent name.
LLVM_ABI llvm::StringRef LanguageDescription(SourceLanguageName Name,
                                             uint32_t Version);

/// Return true if \p S is a C++ DW_LANG_* source language encoding.
///
/// \param S Source language encoding to test.
/// \return True if \p S is a C++ language encoding.
inline bool isCPlusPlus(SourceLanguage S) {
  bool result = false;
  // Deliberately enumerate all the language options so we get a warning when
  // new language options are added (-Wswitch) that'll hopefully help keep this
  // switch up-to-date when new C++ versions are added.
  switch (S) {
  case DW_LANG_C_plus_plus:
  case DW_LANG_C_plus_plus_03:
  case DW_LANG_C_plus_plus_11:
  case DW_LANG_C_plus_plus_14:
  case DW_LANG_C_plus_plus_17:
  case DW_LANG_C_plus_plus_20:
  case DW_LANG_C_plus_plus_23:
    result = true;
    break;
  case DW_LANG_C89:
  case DW_LANG_C:
  case DW_LANG_Ada83:
  case DW_LANG_Cobol74:
  case DW_LANG_Cobol85:
  case DW_LANG_Fortran77:
  case DW_LANG_Fortran90:
  case DW_LANG_Pascal83:
  case DW_LANG_Modula2:
  case DW_LANG_Java:
  case DW_LANG_C99:
  case DW_LANG_Ada95:
  case DW_LANG_Fortran95:
  case DW_LANG_PLI:
  case DW_LANG_ObjC:
  case DW_LANG_ObjC_plus_plus:
  case DW_LANG_UPC:
  case DW_LANG_D:
  case DW_LANG_Python:
  case DW_LANG_OpenCL:
  case DW_LANG_Go:
  case DW_LANG_Modula3:
  case DW_LANG_Haskell:
  case DW_LANG_OCaml:
  case DW_LANG_Rust:
  case DW_LANG_C11:
  case DW_LANG_Swift:
  case DW_LANG_Julia:
  case DW_LANG_Dylan:
  case DW_LANG_Fortran03:
  case DW_LANG_Fortran08:
  case DW_LANG_RenderScript:
  case DW_LANG_BLISS:
  case DW_LANG_Mips_Assembler:
  case DW_LANG_GOOGLE_RenderScript:
  case DW_LANG_BORLAND_Delphi:
  case DW_LANG_lo_user:
  case DW_LANG_hi_user:
  case DW_LANG_Kotlin:
  case DW_LANG_Zig:
  case DW_LANG_Crystal:
  case DW_LANG_C17:
  case DW_LANG_Fortran18:
  case DW_LANG_Ada2005:
  case DW_LANG_Ada2012:
  case DW_LANG_HIP:
  case DW_LANG_Assembly:
  case DW_LANG_C_sharp:
  case DW_LANG_Mojo:
  case DW_LANG_GLSL:
  case DW_LANG_GLSL_ES:
  case DW_LANG_HLSL:
  case DW_LANG_OpenCL_CPP:
  case DW_LANG_CPP_for_OpenCL:
  case DW_LANG_SYCL:
  case DW_LANG_Ruby:
  case DW_LANG_Move:
  case DW_LANG_Hylo:
  case DW_LANG_Metal:
  case DW_LANG_C23:
  case DW_LANG_Fortran23:
  case DW_LANG_Odin:
  case DW_LANG_P4:
  case DW_LANG_V:
  case DW_LANG_Algol68:
  case DW_LANG_Nim:
  case DW_LANG_Erlang:
  case DW_LANG_Elixir:
  case DW_LANG_Gleam:
    result = false;
    break;
  }

  return result;
}

/// Return true if \p S is a Fortran DWARF source language encoding.
///
/// \param S Source language encoding to test.
/// \return True if \p S is a Fortran language encoding.
inline bool isFortran(SourceLanguage S) {
  bool result = false;
  // Deliberately enumerate all the language options so we get a warning when
  // new language options are added (-Wswitch) that'll hopefully help keep this
  // switch up-to-date when new Fortran versions are added.
  switch (S) {
  case DW_LANG_Fortran77:
  case DW_LANG_Fortran90:
  case DW_LANG_Fortran95:
  case DW_LANG_Fortran03:
  case DW_LANG_Fortran08:
  case DW_LANG_Fortran18:
  case DW_LANG_Fortran23:
    result = true;
    break;
  case DW_LANG_C89:
  case DW_LANG_C:
  case DW_LANG_Ada83:
  case DW_LANG_C_plus_plus:
  case DW_LANG_Cobol74:
  case DW_LANG_Cobol85:
  case DW_LANG_Pascal83:
  case DW_LANG_Modula2:
  case DW_LANG_Java:
  case DW_LANG_C99:
  case DW_LANG_Ada95:
  case DW_LANG_PLI:
  case DW_LANG_ObjC:
  case DW_LANG_ObjC_plus_plus:
  case DW_LANG_UPC:
  case DW_LANG_D:
  case DW_LANG_Python:
  case DW_LANG_OpenCL:
  case DW_LANG_Go:
  case DW_LANG_Modula3:
  case DW_LANG_Haskell:
  case DW_LANG_C_plus_plus_03:
  case DW_LANG_C_plus_plus_11:
  case DW_LANG_OCaml:
  case DW_LANG_Rust:
  case DW_LANG_C11:
  case DW_LANG_Swift:
  case DW_LANG_Julia:
  case DW_LANG_Dylan:
  case DW_LANG_C_plus_plus_14:
  case DW_LANG_RenderScript:
  case DW_LANG_BLISS:
  case DW_LANG_Mips_Assembler:
  case DW_LANG_GOOGLE_RenderScript:
  case DW_LANG_BORLAND_Delphi:
  case DW_LANG_lo_user:
  case DW_LANG_hi_user:
  case DW_LANG_Kotlin:
  case DW_LANG_Zig:
  case DW_LANG_Crystal:
  case DW_LANG_C_plus_plus_17:
  case DW_LANG_C_plus_plus_20:
  case DW_LANG_C17:
  case DW_LANG_Ada2005:
  case DW_LANG_Ada2012:
  case DW_LANG_HIP:
  case DW_LANG_Assembly:
  case DW_LANG_C_sharp:
  case DW_LANG_Mojo:
  case DW_LANG_GLSL:
  case DW_LANG_GLSL_ES:
  case DW_LANG_HLSL:
  case DW_LANG_OpenCL_CPP:
  case DW_LANG_CPP_for_OpenCL:
  case DW_LANG_SYCL:
  case DW_LANG_Ruby:
  case DW_LANG_Move:
  case DW_LANG_Hylo:
  case DW_LANG_Metal:
  case DW_LANG_C_plus_plus_23:
  case DW_LANG_C23:
  case DW_LANG_Odin:
  case DW_LANG_P4:
  case DW_LANG_V:
  case DW_LANG_Algol68:
  case DW_LANG_Nim:
  case DW_LANG_Erlang:
  case DW_LANG_Elixir:
  case DW_LANG_Gleam:
    result = false;
    break;
  }

  return result;
}

/// Return true if \p S is a C or Objective-C source language encoding.
///
/// \param S Source language encoding to test.
/// \return True if \p S is a C or Objective-C language encoding.
inline bool isC(SourceLanguage S) {
  // Deliberately enumerate all the language options so we get a warning when
  // new language options are added (-Wswitch) that'll hopefully help keep this
  // switch up-to-date when new C++ versions are added.
  switch (S) {
  case DW_LANG_C11:
  case DW_LANG_C17:
  case DW_LANG_C23:
  case DW_LANG_C89:
  case DW_LANG_C99:
  case DW_LANG_C:
  case DW_LANG_ObjC:
    return true;
  case DW_LANG_C_plus_plus:
  case DW_LANG_C_plus_plus_03:
  case DW_LANG_C_plus_plus_11:
  case DW_LANG_C_plus_plus_14:
  case DW_LANG_C_plus_plus_17:
  case DW_LANG_C_plus_plus_20:
  case DW_LANG_Ada83:
  case DW_LANG_Cobol74:
  case DW_LANG_Cobol85:
  case DW_LANG_Fortran77:
  case DW_LANG_Fortran90:
  case DW_LANG_Pascal83:
  case DW_LANG_Modula2:
  case DW_LANG_Java:
  case DW_LANG_Ada95:
  case DW_LANG_Fortran95:
  case DW_LANG_PLI:
  case DW_LANG_ObjC_plus_plus:
  case DW_LANG_UPC:
  case DW_LANG_D:
  case DW_LANG_Python:
  case DW_LANG_OpenCL:
  case DW_LANG_Go:
  case DW_LANG_Modula3:
  case DW_LANG_Haskell:
  case DW_LANG_OCaml:
  case DW_LANG_Rust:
  case DW_LANG_Swift:
  case DW_LANG_Julia:
  case DW_LANG_Dylan:
  case DW_LANG_Fortran03:
  case DW_LANG_Fortran08:
  case DW_LANG_RenderScript:
  case DW_LANG_BLISS:
  case DW_LANG_Mips_Assembler:
  case DW_LANG_GOOGLE_RenderScript:
  case DW_LANG_BORLAND_Delphi:
  case DW_LANG_lo_user:
  case DW_LANG_hi_user:
  case DW_LANG_Kotlin:
  case DW_LANG_Zig:
  case DW_LANG_Crystal:
  case DW_LANG_Fortran18:
  case DW_LANG_Ada2005:
  case DW_LANG_Ada2012:
  case DW_LANG_HIP:
  case DW_LANG_Assembly:
  case DW_LANG_C_sharp:
  case DW_LANG_Mojo:
  case DW_LANG_GLSL:
  case DW_LANG_GLSL_ES:
  case DW_LANG_HLSL:
  case DW_LANG_OpenCL_CPP:
  case DW_LANG_CPP_for_OpenCL:
  case DW_LANG_SYCL:
  case DW_LANG_Ruby:
  case DW_LANG_Move:
  case DW_LANG_Hylo:
  case DW_LANG_Metal:
  case DW_LANG_C_plus_plus_23:
  case DW_LANG_Fortran23:
  case DW_LANG_Odin:
  case DW_LANG_P4:
  case DW_LANG_V:
  case DW_LANG_Algol68:
  case DW_LANG_Nim:
  case DW_LANG_Erlang:
  case DW_LANG_Elixir:
  case DW_LANG_Gleam:
    return false;
  }
  llvm_unreachable("Unknown language kind.");
}

/// Return the preferred array-index type encoding for source language \p S.
///
/// \param S Source language whose preferred array-index type is requested.
/// \return Preferred DW_ATE_* encoding for array indices in language \p S.
inline TypeKind getArrayIndexTypeEncoding(SourceLanguage S) {
  return isFortran(S) ? DW_ATE_signed : DW_ATE_unsigned;
}

/// Identifier case-sensitivity encodings (DW_AT_identifier_case).
enum CaseSensitivity {
  // Identifier case codes
  DW_ID_case_sensitive = 0x00,    ///< Identifiers are case-sensitive.
  DW_ID_up_case = 0x01,            ///< Identifiers are all upper case.
  DW_ID_down_case = 0x02,          ///< Identifiers are all lower case.
  DW_ID_case_insensitive = 0x03    ///< Identifiers are case-insensitive.
};

/// Calling convention encodings (DW_AT_calling_convention / DW_CC_*).
enum CallingConvention {
// Calling convention codes
#define HANDLE_DW_CC(ID, NAME) DW_CC_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_CC_lo_user = 0x40, ///< Lower bound of the user calling convention range.
  DW_CC_hi_user = 0xff  ///< Upper bound of the user calling convention range.
};

/// LLVM address-space encodings used in DWARF location expressions.
enum AddressSpace {
#define HANDLE_DW_ASPACE(ID, NAME) DW_ASPACE_LLVM_##NAME = ID,
#define HANDLE_DW_ASPACE_PRED(ID, NAME, PRED) DW_ASPACE_LLVM_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// DWARF DW_AT_inline encodings describing whether a routine was inlined.
enum InlineAttribute {
  // Inline codes
  DW_INL_not_inlined = 0x00,           ///< Not inlined.
  DW_INL_inlined = 0x01,               ///< Inlined.
  DW_INL_declared_not_inlined = 0x02,  ///< Declared inline but not inlined.
  DW_INL_declared_inlined = 0x03       ///< Declared inline and inlined.
};

/// Array dimension ordering encodings (DW_AT_ordering).
enum ArrayDimensionOrdering {
  // Array ordering
  DW_ORD_row_major = 0x00, ///< Row-major array ordering.
  DW_ORD_col_major = 0x01  ///< Column-major array ordering.
};

/// Variant discriminant descriptor encodings (DW_AT_discr_list).
enum DiscriminantList {
  // Discriminant descriptor values
  DW_DSC_label = 0x00, ///< Discriminant is a label.
  DW_DSC_range = 0x01  ///< Discriminant is a range.
};

/// Line Number Standard Opcode Encodings.
enum LineNumberOps : uint8_t {
#define HANDLE_DW_LNS(ID, NAME) DW_LNS_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// Line Number Extended Opcode Encodings.
enum LineNumberExtendedOps {
#define HANDLE_DW_LNE(ID, NAME) DW_LNE_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_LNE_lo_user = 0x80, ///< Lower bound of the user extended opcode range.
  DW_LNE_hi_user = 0xff  ///< Upper bound of the user extended opcode range.
};

/// Line number content type encodings.
enum LineNumberEntryFormat {
#define HANDLE_DW_LNCT(ID, NAME) DW_LNCT_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_LNCT_lo_user = 0x2000, ///< Lower bound of the user content type range.
  DW_LNCT_hi_user = 0x3fff, ///< Upper bound of the user content type range.
};

/// DWARF macinfo record type encodings (.debug_macinfo).
enum MacinfoRecordType {
  // Macinfo Type Encodings
  DW_MACINFO_define = 0x01,     ///< Macro definition.
  DW_MACINFO_undef = 0x02,      ///< Macro undefinition.
  DW_MACINFO_start_file = 0x03, ///< Start of a source file inclusion.
  DW_MACINFO_end_file = 0x04,   ///< End of a source file inclusion.
  DW_MACINFO_vendor_ext = 0xff  ///< Vendor extension record.
};

/// DWARF v5 macro information entry type encodings.
enum MacroEntryType {
#define HANDLE_DW_MACRO(ID, NAME) DW_MACRO_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_MACRO_lo_user = 0xe0, ///< Lower bound of the user macro entry range.
  DW_MACRO_hi_user = 0xff  ///< Upper bound of the user macro entry range.
};

/// GNU .debug_macro macro information entry type encodings.
enum GnuMacroEntryType {
#define HANDLE_DW_MACRO_GNU(ID, NAME) DW_MACRO_GNU_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_MACRO_GNU_lo_user = 0xe0, ///< Lower bound of the user GNU macro range.
  DW_MACRO_GNU_hi_user = 0xff  ///< Upper bound of the user GNU macro range.
};

/// DWARF v5 range list entry encoding values.
enum RnglistEntries {
#define HANDLE_DW_RLE(ID, NAME) DW_RLE_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// DWARF v5 loc list entry encoding values.
enum LoclistEntries {
#define HANDLE_DW_LLE(ID, NAME) DW_LLE_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// Call frame instruction encodings.
enum CallFrameInfo {
#define HANDLE_DW_CFA(ID, NAME) DW_CFA_##NAME = ID,
#define HANDLE_DW_CFA_PRED(ID, NAME, ARCH) DW_CFA_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_CFA_extended = 0x00, ///< Extended opcode prefix (DWARF CFA opcode 0).

  DW_CFA_lo_user = 0x1c, ///< Lower bound of the user CFA instruction range.
  DW_CFA_hi_user = 0x3f  ///< Upper bound of the user CFA instruction range.
};

/// Miscellaneous DWARF and LSDA pointer-encoding constants.
enum Constants {
  // Children flag
  DW_CHILDREN_no = 0x00,  ///< DIE has no children.
  DW_CHILDREN_yes = 0x01, ///< DIE has children.

  DW_EH_PE_absptr = 0x00, ///< Absolute pointer encoding (no relative adjustment).
  DW_EH_PE_omit = 0xff, ///< Pointer encoding is omitted / unused.
  DW_EH_PE_uleb128 = 0x01, ///< Unsigned LEB128-encoded value.
  DW_EH_PE_udata2 = 0x02, ///< Unsigned 2-byte value.
  DW_EH_PE_udata4 = 0x03, ///< Unsigned 4-byte value.
  DW_EH_PE_udata8 = 0x04, ///< Unsigned 8-byte value.
  DW_EH_PE_sleb128 = 0x09, ///< Signed LEB128-encoded value.
  DW_EH_PE_sdata2 = 0x0A,  ///< Signed 2-byte value.
  DW_EH_PE_sdata4 = 0x0B,  ///< Signed 4-byte value.
  DW_EH_PE_sdata8 = 0x0C,  ///< Signed 8-byte value.
  DW_EH_PE_signed = 0x08, ///< Format bit indicating a signed value.
  DW_EH_PE_pcrel = 0x10, ///< Value is relative to the current PC.
  DW_EH_PE_textrel = 0x20, ///< Value is relative to the text section.
  DW_EH_PE_datarel = 0x30, ///< Value is relative to the data section.
  DW_EH_PE_funcrel = 0x40, ///< Value is relative to the function start.
  DW_EH_PE_aligned = 0x50, ///< Value is aligned.
  DW_EH_PE_indirect = 0x80 ///< Value is an indirect pointer reference.
};

/// Constants for the DW_APPLE_PROPERTY_attributes attribute.
/// Keep this list in sync with clang's DeclObjCCommon.h
/// ObjCPropertyAttribute::Kind!
enum ApplePropertyAttributes {
#define HANDLE_DW_APPLE_PROPERTY(ID, NAME) DW_APPLE_PROPERTY_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// Constants for unit types in DWARF v5.
enum UnitType : unsigned char {
#define HANDLE_DW_UT(ID, NAME) DW_UT_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_UT_lo_user = 0x80, ///< Lower bound of the user unit-type range.
  DW_UT_hi_user = 0xff  ///< Upper bound of the user unit-type range.
};

/// DWARF v5 index attribute encodings used in .debug_names.
enum Index {
#define HANDLE_DW_IDX(ID, NAME) DW_IDX_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_IDX_lo_user = 0x2000, ///< Lower bound of the user index attribute range.
  DW_IDX_hi_user = 0x3fff  ///< Upper bound of the user index attribute range.
};

/// Return true if \p UnitType is a recognized DWARF unit type encoding.
///
/// \param UnitType Candidate DW_UT_* unit type byte.
/// \return True if \p UnitType is a recognized DW_UT_* value.
inline bool isUnitType(uint8_t UnitType) {
  switch (UnitType) {
  case DW_UT_compile:
  case DW_UT_type:
  case DW_UT_partial:
  case DW_UT_skeleton:
  case DW_UT_split_compile:
  case DW_UT_split_type:
    return true;
  default:
    return false;
  }
}

/// Return true if \p T is a DWARF unit DIE tag (compile/type/partial/skeleton).
///
/// \param T DWARF tag to test.
/// \return True if \p T is a compile, type, partial, or skeleton unit tag.
inline bool isUnitType(dwarf::Tag T) {
  switch (T) {
  case DW_TAG_compile_unit:
  case DW_TAG_type_unit:
  case DW_TAG_partial_unit:
  case DW_TAG_skeleton_unit:
    return true;
  default:
    return false;
  }
}

/// Constants for the DWARF v5 accelerator table (atoms and type flags).
enum AcceleratorTable {
  // Data layout descriptors.
  DW_ATOM_null = 0u,       ///< Marker as the end of a list of atoms.
  DW_ATOM_die_offset = 1u, ///< DIE offset in the debug_info section.
  DW_ATOM_cu_offset = 2u, ///< Offset of the compile unit header that contains the
                          ///< item in question.
  DW_ATOM_die_tag = 3u,   ///< A tag entry.
  DW_ATOM_type_flags = 4u, ///< Set of flags for a type.

  DW_ATOM_type_type_flags = 5u, ///< Dsymutil type extension.
  DW_ATOM_qual_name_hash = 6u,  ///< Dsymutil qualified hash extension.

  // DW_ATOM_type_flags values.

  /// Always set for C++, only set for ObjC if this is the @implementation for a
  /// class.
  DW_FLAG_type_implementation = 2u,

  // Hash functions.

  DW_hash_function_djb = 0u ///< Daniel J. Bernstein hash.
};

/// Return a suggested bucket count for the DWARF v5 .debug_names table.
///
/// \param UniqueHashCount Number of unique hashes that will be inserted.
/// \return Suggested bucket count for \p UniqueHashCount hashes.
inline uint32_t getDebugNamesBucketCount(uint32_t UniqueHashCount) {
  if (UniqueHashCount > 1024)
    return UniqueHashCount / 4;
  if (UniqueHashCount > 16)
    return UniqueHashCount / 2;
  return std::max<uint32_t>(UniqueHashCount, 1);
}

/// Constants for the GNU pubnames/pubtypes extensions supporting gdb index.
enum GDBIndexEntryKind {
  GIEK_NONE,     ///< No symbol kind.
  GIEK_TYPE,     ///< The entry names a type.
  GIEK_VARIABLE, ///< The entry names a variable.
  GIEK_FUNCTION, ///< The entry names a function.
  GIEK_OTHER,    ///< The entry names some other kind of symbol.
  GIEK_UNUSED5,  ///< Reserved, unused.
  GIEK_UNUSED6,  ///< Reserved, unused.
  GIEK_UNUSED7   ///< Reserved, unused.
};

/// Linkage of a GDB index entry.
enum GDBIndexEntryLinkage {
  GIEL_EXTERNAL, ///< External (global) linkage.
  GIEL_STATIC    ///< Static linkage.
};

/// \defgroup DwarfConstantsDumping Dwarf constants dumping functions
///
/// All these functions map their argument's value back to the
/// corresponding enumerator name or return an empty StringRef if the value
/// isn't known.
///
/// @{
/// Returns the name of the DW_TAG constant with the given value.
///
/// \param Tag DW_TAG_* encoding value.
/// \return Enumerator name for \p Tag, or empty if unknown.
LLVM_ABI StringRef TagString(unsigned Tag);
/// Returns the name of the DW_CHILDREN constant with the given value.
///
/// \param Children DW_CHILDREN_* encoding value.
/// \return Enumerator name for \p Children, or empty if unknown.
LLVM_ABI StringRef ChildrenString(unsigned Children);
/// Returns the name of the DW_AT attribute with the given value.
///
/// \param Attribute DW_AT_* encoding value.
/// \return Enumerator name for \p Attribute, or empty if unknown.
LLVM_ABI StringRef AttributeString(unsigned Attribute);
/// Returns the name of the DW_FORM form encoding with the given value.
///
/// \param Encoding DW_FORM_* encoding value.
/// \return Enumerator name for \p Encoding, or empty if unknown.
LLVM_ABI StringRef FormEncodingString(unsigned Encoding);
/// Returns the name of the DW_OP operation encoding with the given value.
///
/// \param Encoding DW_OP_* encoding value.
/// \return Enumerator name for \p Encoding, or empty if unknown.
LLVM_ABI StringRef OperationEncodingString(unsigned Encoding);
/// Returns the name of the DW_OP sub-operation encoding with the given values.
///
/// \param OpEncoding Parent DW_OP encoding that owns the sub-operation.
/// \param SubOpEncoding Sub-operation encoding value.
/// \return Enumerator name for the sub-operation, or empty if unknown.
LLVM_ABI StringRef SubOperationEncodingString(unsigned OpEncoding,
                                              unsigned SubOpEncoding);
/// Returns the name of the DW_ATE type-encoding constant with the given value.
///
/// \param Encoding DW_ATE_* encoding value.
/// \return Enumerator name for \p Encoding, or empty if unknown.
LLVM_ABI StringRef AttributeEncodingString(unsigned Encoding);
/// Returns the name of the DW_DS decimal sign constant with the given value.
///
/// \param Sign DW_DS_* encoding value.
/// \return Enumerator name for \p Sign, or empty if unknown.
LLVM_ABI StringRef DecimalSignString(unsigned Sign);
/// Returns the name of the DW_END endianity constant with the given value.
///
/// \param Endian DW_END_* encoding value.
/// \return Enumerator name for \p Endian, or empty if unknown.
LLVM_ABI StringRef EndianityString(unsigned Endian);
/// Returns the name of the DW_ACCESS accessibility constant with the given value.
///
/// \param Access DW_ACCESS_* encoding value.
/// \return Enumerator name for \p Access, or empty if unknown.
LLVM_ABI StringRef AccessibilityString(unsigned Access);
/// Returns the name of the DW_DEFAULTED constant with the given value.
///
/// \param DefaultedEncodings DW_DEFAULTED_* encoding value.
/// \return Enumerator name for \p DefaultedEncodings, or empty if unknown.
LLVM_ABI StringRef DefaultedMemberString(unsigned DefaultedEncodings);
/// Returns the name of the DW_VIS visibility constant with the given value.
///
/// \param Visibility DW_VIS_* encoding value.
/// \return Enumerator name for \p Visibility, or empty if unknown.
LLVM_ABI StringRef VisibilityString(unsigned Visibility);
/// Returns the name of the DW_VIRTUALITY constant with the given value.
///
/// \param Virtuality DW_VIRTUALITY_* encoding value.
/// \return Enumerator name for \p Virtuality, or empty if unknown.
LLVM_ABI StringRef VirtualityString(unsigned Virtuality);
/// Returns the name of the Apple enum-kind constant with the given value.
///
/// \param EnumKind DW_APPLE_ENUM_KIND_* encoding value.
/// \return Enumerator name for \p EnumKind, or empty if unknown.
LLVM_ABI StringRef EnumKindString(unsigned EnumKind);
/// Returns the name of the DW_LANG source language with the given value.
///
/// \param Language DW_LANG_* encoding value.
/// \return Enumerator name for \p Language, or empty if unknown.
LLVM_ABI StringRef LanguageString(unsigned Language);
/// Returns the name of the DW_LNAME source language name.
///
/// \param Lang DW_LNAME_* source language name encoding.
/// \return Enumerator name for \p Lang, or empty if unknown.
LLVM_ABI StringRef SourceLanguageNameString(SourceLanguageName Lang);
/// Returns the name of the language dialect constant with the given value.
///
/// \param LanguageDialect DW_LLVM_LANG_DIALECT_* encoding value.
/// \return Enumerator name for \p LanguageDialect, or empty if unknown.
LLVM_ABI StringRef LanguageDialectString(unsigned LanguageDialect);
/// Returns the name of the DW_ID case-sensitivity constant with the given value.
///
/// \param Case DW_ID_* encoding value.
/// \return Enumerator name for \p Case, or empty if unknown.
LLVM_ABI StringRef CaseString(unsigned Case);
/// Returns the name of the DW_CC calling-convention constant with the given value.
///
/// \param Convention DW_CC_* encoding value.
/// \return Enumerator name for \p Convention, or empty if unknown.
LLVM_ABI StringRef ConventionString(unsigned Convention);
/// Returns the name of the DW_INL inline code constant with the given value.
///
/// \param Code DW_INL_* encoding value.
/// \return Enumerator name for \p Code, or empty if unknown.
LLVM_ABI StringRef InlineCodeString(unsigned Code);
/// Returns the name of the array order constant with the given value.
///
/// \param Order DW_ORD_* encoding value.
/// \return Enumerator name for \p Order, or empty if unknown.
LLVM_ABI StringRef ArrayOrderString(unsigned Order);
/// Returns the name of the standard line-number opcode with the given value.
///
/// \param Standard DW_LNS_* encoding value.
/// \return Enumerator name for \p Standard, or empty if unknown.
LLVM_ABI StringRef LNStandardString(unsigned Standard);
/// Returns the name of the extended line-number opcode with the given value.
///
/// \param Encoding DW_LNE_* encoding value.
/// \return Enumerator name for \p Encoding, or empty if unknown.
LLVM_ABI StringRef LNExtendedString(unsigned Encoding);
/// Returns the name of the DW_MACINFO record type with the given value.
///
/// \param Encoding DW_MACINFO_* encoding value.
/// \return Enumerator name for \p Encoding, or empty if unknown.
LLVM_ABI StringRef MacinfoString(unsigned Encoding);
/// Returns the name of the DW_MACRO entry type with the given value.
///
/// \param Encoding DW_MACRO_* encoding value.
/// \return Enumerator name for \p Encoding, or empty if unknown.
LLVM_ABI StringRef MacroString(unsigned Encoding);
/// Returns the name of the GNU .debug_macro entry type with the given value.
///
/// \param Encoding DW_MACRO_GNU_* encoding value.
/// \return Enumerator name for \p Encoding, or empty if unknown.
LLVM_ABI StringRef GnuMacroString(unsigned Encoding);
/// Returns the name of the DW_RLE range list entry encoding with the given value.
///
/// \param Encoding DW_RLE_* encoding value.
/// \return Enumerator name for \p Encoding, or empty if unknown.
LLVM_ABI StringRef RangeListEncodingString(unsigned Encoding);
/// Returns the name of the DW_LLE loclist entry encoding with the given value.
///
/// \param Encoding DW_LLE_* encoding value.
/// \return Enumerator name for \p Encoding, or empty if unknown.
LLVM_ABI StringRef LocListEncodingString(unsigned Encoding);
/// Returns the name of the call-frame instruction for \p Encoding on \p Arch.
///
/// \param Encoding DW_CFA_* encoding value.
/// \param Arch Target architecture used to resolve architecture-specific CFAs.
/// \return CFA instruction name for \p Encoding on \p Arch, or empty if unknown.
LLVM_ABI StringRef CallFrameString(unsigned Encoding, Triple::ArchType Arch);
/// Returns the name of the Apple property attribute with the given value.
///
/// \param Prop DW_APPLE_PROPERTY_* encoding value.
/// \return Enumerator name for \p Prop, or empty if unknown.
LLVM_ABI StringRef ApplePropertyString(unsigned Prop);
/// Returns the name of the DW_UT unit type with the given value.
///
/// \param Unit DW_UT_* encoding value.
/// \return Enumerator name for \p Unit, or empty if unknown.
LLVM_ABI StringRef UnitTypeString(unsigned Unit);
/// Returns the name of the accelerator-table atom type with the given value.
///
/// \param Atom DW_ATOM_* encoding value.
/// \return Enumerator name for \p Atom, or empty if unknown.
LLVM_ABI StringRef AtomTypeString(unsigned Atom);
/// Returns the name of the GDB index entry kind.
///
/// \param Kind GDB index entry kind to name.
/// \return Enumerator name for \p Kind.
LLVM_ABI StringRef GDBIndexEntryKindString(GDBIndexEntryKind Kind);
/// Returns the name of the GDB index entry linkage.
///
/// \param Linkage GDB index entry linkage to name.
/// \return Enumerator name for \p Linkage.
LLVM_ABI StringRef GDBIndexEntryLinkageString(GDBIndexEntryLinkage Linkage);
/// Returns the name of the DW_IDX index attribute with the given value.
///
/// \param Idx DW_IDX_* encoding value.
/// \return Enumerator name for \p Idx, or empty if unknown.
LLVM_ABI StringRef IndexString(unsigned Idx);
/// Returns "DWARF32" or "DWARF64" for the given DWARF format enum.
///
/// \param Format DWARF32 or DWARF64 format selector.
/// \return "DWARF32" or "DWARF64" for \p Format.
LLVM_ABI StringRef FormatString(DwarfFormat Format);
/// Returns "DWARF32" or "DWARF64" depending on \p IsDWARF64.
///
/// \param IsDWARF64 True to name the 64-bit format, false for 32-bit.
/// \return "DWARF32" or "DWARF64" depending on \p IsDWARF64.
LLVM_ABI StringRef FormatString(bool IsDWARF64);
/// Returns the name of the DWARF range-list entry encoding \p RLE.
///
/// \param RLE DW_RLE_* encoding value.
/// \return Enumerator name for \p RLE, or empty if unknown.
LLVM_ABI StringRef RLEString(unsigned RLE);
/// Returns the name of address space \p AS for target triple \p TT.
///
/// \param AS Address-space encoding value.
/// \param TT Target triple used to interpret architecture-specific spaces.
/// \return Name of address space \p AS for \p TT, or empty if unknown.
LLVM_ABI StringRef AddressSpaceString(unsigned AS, const llvm::Triple &TT);
/// @}

/// \defgroup DwarfConstantsParsing Dwarf constants parsing functions
///
/// These functions map their strings back to the corresponding enumeration
/// value or return 0 if there is none, except for these exceptions:
///
/// \li \a getTag() returns \a DW_TAG_invalid on invalid input.
/// \li \a getVirtuality() returns \a DW_VIRTUALITY_invalid on invalid input.
/// \li \a getMacinfo() returns \a DW_MACINFO_invalid on invalid input.
///
/// @{
/// Maps a DW_TAG name string to its encoding, or DW_TAG_invalid if unknown.
///
/// \param TagString Enumerator name such as "DW_TAG_compile_unit".
/// \return DW_TAG_* encoding, or DW_TAG_invalid if unknown.
LLVM_ABI unsigned getTag(StringRef TagString);
/// Maps a DW_OP name string to its location-atom encoding, or 0 if unknown.
///
/// \param OperationEncodingString Enumerator name such as "DW_OP_addr".
/// \return DW_OP_* encoding, or 0 if unknown.
LLVM_ABI unsigned getOperationEncoding(StringRef OperationEncodingString);
/// Maps a sub-operation name for \p OpEncoding to its encoding, or 0 if unknown.
///
/// \param OpEncoding Parent DW_OP encoding that owns the sub-operation.
/// \param SubOperationEncodingString Sub-operation enumerator name string.
/// \return Sub-operation encoding, or 0 if unknown.
LLVM_ABI unsigned getSubOperationEncoding(unsigned OpEncoding,
                                          StringRef SubOperationEncodingString);
/// Maps a virtuality name string to its DW_VIRTUALITY encoding, or
/// DW_VIRTUALITY_invalid if unknown.
///
/// \param VirtualityString Enumerator name such as "DW_VIRTUALITY_virtual".
/// \return DW_VIRTUALITY_* encoding, or DW_VIRTUALITY_invalid if unknown.
LLVM_ABI unsigned getVirtuality(StringRef VirtualityString);
/// Maps an Apple enum kind name string to its encoding, or 0 if unknown.
///
/// \param EnumKindString Enumerator name such as "DW_APPLE_ENUM_KIND_Closed".
/// \return Enum kind encoding, or 0 if unknown.
LLVM_ABI unsigned getEnumKind(StringRef EnumKindString);
/// Maps a DW_LANG name string to its encoding, or 0 if unknown.
///
/// \param LanguageString Enumerator name such as "DW_LANG_C_plus_plus".
/// \return DW_LANG_* encoding, or 0 if unknown.
LLVM_ABI unsigned getLanguage(StringRef LanguageString);
/// Maps a DW_LNAME name string to its encoding, or 0 if unknown.
///
/// \param SourceLanguageNameString Enumerator name such as "DW_LNAME_C".
/// \return DW_LNAME_* encoding, or 0 if unknown.
LLVM_ABI unsigned getSourceLanguageName(StringRef SourceLanguageNameString);
/// Maps a language dialect name string to its encoding, or 0 if unknown.
///
/// \param LanguageDialectString Enumerator name for a DW_LLVM_LANG_DIALECT_*.
/// \return Dialect encoding, or 0 if unknown.
LLVM_ABI unsigned getLanguageDialect(StringRef LanguageDialectString);
/// Maps a calling-convention name string to its encoding, or 0 if unknown.
///
/// \param LanguageString Enumerator name such as "DW_CC_normal".
/// \return DW_CC_* encoding, or 0 if unknown.
LLVM_ABI unsigned getCallingConvention(StringRef LanguageString);
/// Maps a DW_ATE type-encoding name string to its value, or 0 if unknown.
///
/// \param EncodingString Enumerator name such as "DW_ATE_signed".
/// \return DW_ATE_* encoding, or 0 if unknown.
LLVM_ABI unsigned getAttributeEncoding(StringRef EncodingString);
/// Maps a DW_MACINFO name string to its encoding, or DW_MACINFO_invalid if unknown.
///
/// \param MacinfoString Enumerator name such as "DW_MACINFO_define".
/// \return DW_MACINFO_* encoding, or DW_MACINFO_invalid if unknown.
LLVM_ABI unsigned getMacinfo(StringRef MacinfoString);
/// Maps a DW_MACRO name string to its encoding, or 0 if unknown.
///
/// \param MacroString Enumerator name such as "DW_MACRO_define".
/// \return DW_MACRO_* encoding, or 0 if unknown.
LLVM_ABI unsigned getMacro(StringRef MacroString);
/// @}

/// \defgroup DwarfConstantsVersioning Dwarf version for constants
///
/// Returns the DWARF version when a constant was first defined.
///
/// For constants defined by DWARF, returns the DWARF version when the constant
/// was first defined. For vendor extensions, if there is a version-related
/// policy for when to emit it, returns a version number for that policy.
/// Otherwise returns 0.
///
/// @{
/// Returns the DWARF version when the tag was first defined.
///
/// \param T DWARF tag to query.
/// \return DWARF version when \p T was first defined, or 0.
LLVM_ABI unsigned TagVersion(Tag T);
/// Returns the DWARF version when the attribute was first defined.
///
/// \param A DWARF attribute to query.
/// \return DWARF version when \p A was first defined, or 0.
LLVM_ABI unsigned AttributeVersion(Attribute A);
/// Returns the DWARF version when the form was first defined.
///
/// \param F DWARF form to query.
/// \return DWARF version when \p F was first defined, or 0.
LLVM_ABI unsigned FormVersion(Form F);
/// Returns the DWARF version when the operation was first defined.
///
/// \param O Location atom to query.
/// \return DWARF version when \p O was first defined, or 0.
LLVM_ABI unsigned OperationVersion(LocationAtom O);
/// Returns the DWARF version when the base type encoding was first defined.
///
/// \param E Base type encoding (DW_ATE_*) to query.
/// \return DWARF version when \p E was first defined, or 0.
LLVM_ABI unsigned AttributeEncodingVersion(TypeKind E);
/// Returns the DWARF version when the source language was first defined.
///
/// \param L Source language encoding to query.
/// \return DWARF version when \p L was first defined, or 0.
LLVM_ABI unsigned LanguageVersion(SourceLanguage L);
/// @}

/// \defgroup DwarfConstantsVendor Dwarf "vendor" for constants
///
/// These functions return an identifier describing "who" defined the constant,
/// either the DWARF standard itself or the vendor who defined the extension.
///
/// @{
/// Returns the vendor identifier that defined the given tag.
///
/// \param T DWARF tag to query.
/// \return Vendor identifier that defined \p T.
LLVM_ABI unsigned TagVendor(Tag T);
/// Returns the vendor identifier that defined the given attribute.
///
/// \param A DWARF attribute to query.
/// \return Vendor identifier that defined \p A.
LLVM_ABI unsigned AttributeVendor(Attribute A);
/// Returns the vendor identifier that defined the given form.
///
/// \param F DWARF form to query.
/// \return Vendor identifier that defined \p F.
LLVM_ABI unsigned FormVendor(Form F);
/// Returns the vendor identifier that defined the given operation.
///
/// \param O Location atom to query.
/// \return Vendor identifier that defined \p O.
LLVM_ABI unsigned OperationVendor(LocationAtom O);
/// Returns the vendor identifier that defined the given base type encoding.
///
/// \param E Base type encoding (DW_ATE_*) to query.
/// \return Vendor identifier that defined \p E.
LLVM_ABI unsigned AttributeEncodingVendor(TypeKind E);
/// Returns the vendor identifier that defined the given source language.
///
/// \param L Source language encoding to query.
/// \return Vendor identifier that defined \p L.
LLVM_ABI unsigned LanguageVendor(SourceLanguage L);
/// @}

/// The number of operands for the given LocationAtom.
///
/// \param O Location atom (DW_OP_*) to query.
/// \return Number of operands for \p O, or std::nullopt if unknown.
LLVM_ABI std::optional<unsigned> OperationOperands(LocationAtom O);

/// Return the stack arity of the given location atom, if known.
///
/// This is the number of elements on the stack this operation operates on.
/// Returns -1 if the arity is variable (e.g. depending on the argument) or
/// unknown.
///
/// \param O Location atom (DW_OP_*) to query.
/// \return Stack arity for \p O, or std::nullopt if variable or unknown.
LLVM_ABI std::optional<unsigned> OperationArity(LocationAtom O);

/// Return true if \p O is a TLS address push/form location opcode.
///
/// \param O Location opcode byte to test.
/// \return True if \p O is a TLS address opcode.
inline bool isTlsAddressOp(uint8_t O) {
  return O == DW_OP_form_tls_address || O == DW_OP_GNU_push_tls_address;
}

/// Returns the default lower array bound for the given source language, if known.
///
/// \param L Source language encoding to query.
/// \return Default lower array bound for \p L, or std::nullopt if unknown.
LLVM_ABI std::optional<unsigned> LanguageLowerBound(SourceLanguage L);

/// The size of a reference determined by the DWARF 32/64-bit format.
///
/// \param Format DWARF32 or DWARF64 format selector.
/// \return Byte size of a DWARF offset for \p Format (4 or 8).
inline uint8_t getDwarfOffsetByteSize(DwarfFormat Format) {
  switch (Format) {
  case DwarfFormat::DWARF32:
    return 4;
  case DwarfFormat::DWARF64:
    return 8;
  }
  llvm_unreachable("Invalid Format value");
}

/// Parameters that control the byte size of variable-width DW_FORM values.
///
/// Form sizes can depend on the DWARF version, address byte size, or
/// DWARF32/DWARF64 format.
struct FormParams {
  /// DWARF version number used to interpret form sizes and encodings.
  uint16_t Version;
  /// Target address size in bytes for this compilation unit.
  uint8_t AddrSize;
  /// DWARF32 or DWARF64 format controlling offset and length field widths.
  DwarfFormat Format;
  /// True if DWARF v2 output generally uses relocations for references
  /// to other .debug_* sections.
  bool DwarfUsesRelocationsAcrossSections = false;

  /// The definition of the size of form DW_FORM_ref_addr depends on the
  /// version. In DWARF v2 it's the size of an address; after that, it's the
  /// size of a reference.
  ///
  /// \return Byte size of a DW_FORM_ref_addr for these form parameters.
  uint8_t getRefAddrByteSize() const {
    if (Version == 2)
      return AddrSize;
    return getDwarfOffsetByteSize();
  }

  /// The size of a reference is determined by the DWARF 32/64-bit format.
  ///
  /// \return Byte size of a DWARF offset for Format (4 or 8).
  uint8_t getDwarfOffsetByteSize() const {
    return dwarf::getDwarfOffsetByteSize(Format);
  }
  /// Maximum offset value representable in this DWARF format (32- or 64-bit).
  ///
  /// \return UINT32_MAX for DWARF32, or UINT64_MAX for DWARF64.
  inline uint64_t getDwarfMaxOffset() const {
    return (getDwarfOffsetByteSize() == 4) ? UINT32_MAX : UINT64_MAX;
  }

  /// True when both Version and AddrSize are non-zero (usable form parameters).
  ///
  /// \return True if Version and AddrSize are both non-zero.
  explicit operator bool() const { return Version && AddrSize; }
};

/// Get the byte size of the unit length field depending on the DWARF format.
///
/// \param Format DWARF32 or DWARF64 format selector.
/// \return Byte size of the unit length field for \p Format (4 or 12).
inline uint8_t getUnitLengthFieldByteSize(DwarfFormat Format) {
  switch (Format) {
  case DwarfFormat::DWARF32:
    return 4;
  case DwarfFormat::DWARF64:
    return 12;
  }
  llvm_unreachable("Invalid Format value");
}

/// Get the fixed byte size for a given form.
///
/// If the form has a fixed byte size, then an Optional with a value will be
/// returned. If the form is always encoded using a variable length storage
/// format (ULEB or SLEB numbers or blocks) then std::nullopt will be returned.
///
/// \param Form DWARF form to get the fixed byte size for.
/// \param Params DWARF parameters to help interpret forms.
/// \returns std::optional<uint8_t> value with the fixed byte size or
/// std::nullopt if \p Form doesn't have a fixed byte size.
LLVM_ABI std::optional<uint8_t> getFixedFormByteSize(dwarf::Form Form,
                                                     FormParams Params);

/// Tells whether the specified form is defined in the specified version,
/// or is an extension if extensions are allowed.
///
/// \param F DWARF form encoding to check.
/// \param Version DWARF version number to check against.
/// \param ExtensionsOk If true, vendor extension forms are treated as valid.
/// \return True if \p F is valid for \p Version (or is an allowed extension).
LLVM_ABI bool isValidFormForVersion(Form F, unsigned Version,
                                    bool ExtensionsOk = true);

/// Returns the symbolic string representing Val when used as a value
/// for attribute Attr.
///
/// \param Attr DW_AT_* attribute whose value is being named.
/// \param Val Attribute value to convert to a symbolic name.
/// \return Symbolic name for \p Val as a value of \p Attr, or empty if unknown.
LLVM_ABI StringRef AttributeValueString(uint16_t Attr, unsigned Val);

/// Returns the symbolic string representing Val when used as a value
/// for atom Atom.
///
/// \param Atom Accelerator-table atom type whose value is being named.
/// \param Val Atom value to convert to a symbolic name.
/// \return Symbolic name for \p Val as a value of \p Atom, or empty if unknown.
LLVM_ABI StringRef AtomValueString(uint16_t Atom, unsigned Val);

/// Describes an entry of the various gnu_pub* debug sections.
///
/// The gnu_pub* kind looks like:
///
/// 0-3  reserved
/// 4-6  symbol kind
/// 7    0 == global, 1 == static
///
/// A gdb_index descriptor includes the above kind, shifted 24 bits up with the
/// offset of the cu within the debug_info section stored in those 24 bits.
struct PubIndexEntryDescriptor {
  /// Symbol kind bits from the gnu_pub* / gdb_index descriptor.
  GDBIndexEntryKind Kind;
  /// Linkage bit from the gnu_pub* / gdb_index descriptor.
  GDBIndexEntryLinkage Linkage;
  /// Construct a descriptor from the given kind and linkage.
  ///
  /// \param Kind Symbol kind bits for the entry.
  /// \param Linkage Linkage bit for the entry.
  PubIndexEntryDescriptor(GDBIndexEntryKind Kind, GDBIndexEntryLinkage Linkage)
      : Kind(Kind), Linkage(Linkage) {}
  /// Construct with \p Kind and external (global) linkage.
  ///
  /// \param Kind Symbol kind bits for the entry.
  /* implicit */ PubIndexEntryDescriptor(GDBIndexEntryKind Kind)
      : Kind(Kind), Linkage(GIEL_EXTERNAL) {}
  /// Decode kind and linkage from a packed gnu_pub* / gdb_index descriptor byte.
  ///
  /// \param Value Packed descriptor byte from a gnu_pub* or gdb_index section.
  explicit PubIndexEntryDescriptor(uint8_t Value)
      : Kind(
            static_cast<GDBIndexEntryKind>((Value & KIND_MASK) >> KIND_OFFSET)),
        Linkage(static_cast<GDBIndexEntryLinkage>((Value & LINKAGE_MASK) >>
                                                  LINKAGE_OFFSET)) {}
  /// Pack kind and linkage into the gnu_pub* / gdb_index descriptor byte.
  ///
  /// \return Packed descriptor byte encoding Kind and Linkage.
  uint8_t toBits() const {
    return Kind << KIND_OFFSET | Linkage << LINKAGE_OFFSET;
  }

private:
  enum {
    KIND_OFFSET = 4,
    KIND_MASK = 7 << KIND_OFFSET,
    LINKAGE_OFFSET = 7,
    LINKAGE_MASK = 1 << LINKAGE_OFFSET
  };
};

/// Trait that identifies DWARF enums that have stringifiers for format_provider.
template <typename Enum> struct EnumTraits : public std::false_type {};

/// EnumTraits specialization for dwarf::Attribute.
template <> struct EnumTraits<Attribute> : public std::true_type {
  /// Short type prefix used when formatting unknown values (e.g. "AT").
  static constexpr char Type[3] = "AT";
  /// Maps an Attribute encoding to its enumerator name string.
  LLVM_ABI static StringRef (*const StringFn)(unsigned);
};

/// EnumTraits specialization for dwarf::Form.
template <> struct EnumTraits<Form> : public std::true_type {
  /// Short type prefix used when formatting unknown values (e.g. "FORM").
  static constexpr char Type[5] = "FORM";
  /// Maps a Form encoding to its enumerator name string.
  LLVM_ABI static StringRef (*const StringFn)(unsigned);
};

/// EnumTraits specialization for dwarf::Index.
template <> struct EnumTraits<Index> : public std::true_type {
  /// Short type prefix used when formatting unknown values (e.g. "IDX").
  static constexpr char Type[4] = "IDX";
  /// Maps an Index encoding to its enumerator name string.
  LLVM_ABI static StringRef (*const StringFn)(unsigned);
};

/// EnumTraits specialization for dwarf::Tag.
template <> struct EnumTraits<Tag> : public std::true_type {
  /// Short type prefix used when formatting unknown values (e.g. "TAG").
  static constexpr char Type[4] = "TAG";
  /// Maps a Tag encoding to its enumerator name string.
  LLVM_ABI static StringRef (*const StringFn)(unsigned);
};

/// EnumTraits specialization for dwarf::LineNumberOps.
template <> struct EnumTraits<LineNumberOps> : public std::true_type {
  /// Short type prefix used when formatting unknown values (e.g. "LNS").
  static constexpr char Type[4] = "LNS";
  /// Maps a LineNumberOps encoding to its enumerator name string.
  LLVM_ABI static StringRef (*const StringFn)(unsigned);
};

/// EnumTraits specialization for dwarf::LocationAtom.
template <> struct EnumTraits<LocationAtom> : public std::true_type {
  /// Short type prefix used when formatting unknown values (e.g. "OP").
  static constexpr char Type[3] = "OP";
  /// Maps a LocationAtom encoding to its enumerator name string.
  LLVM_ABI static StringRef (*const StringFn)(unsigned);
};

/// Compute the all-ones tombstone address for the given address byte size.
///
/// \param AddressByteSize Target address size in bytes (typically 4 or 8).
/// \return All-ones tombstone address truncated to \p AddressByteSize bytes.
inline uint64_t computeTombstoneAddress(uint8_t AddressByteSize) {
  return std::numeric_limits<uint64_t>::max() >> (8 - AddressByteSize) * 8;
}

} // End of namespace dwarf

/// Specialization of format_provider for DWARF enums with EnumTraits.
///
/// Unlike the dumping functions above, unknown enumerator values are formatted
/// as DW_TYPE_unknown_1234 (e.g. DW_TAG_unknown_ffff).
template <typename Enum>
struct format_provider<Enum, std::enable_if_t<dwarf::EnumTraits<Enum>::value>> {
  /// Format a DWARF enum value to \p OS for llvm::formatv.
  ///
  /// \param E Enum value to format.
  /// \param OS Output stream to write the formatted name to.
  /// \param Style Unused format style string.
  static void format(const Enum &E, raw_ostream &OS, StringRef Style) {
    StringRef Str = dwarf::EnumTraits<Enum>::StringFn(E);
    if (Str.empty()) {
      OS << "DW_" << dwarf::EnumTraits<Enum>::Type << "_unknown_"
         << llvm::format("%x", E);
    } else
      OS << Str;
  }
};
} // End of namespace llvm

#endif
