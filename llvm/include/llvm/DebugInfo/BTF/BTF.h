//===-- BTF.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the layout of .BTF and .BTF.ext ELF sections.
///
/// The binary layout for .BTF section:
///   struct Header
///   Type and Str subsections
/// The Type subsection is a collection of types with type id starting with 1.
/// The Str subsection is simply a collection of strings.
///
/// The binary layout for .BTF.ext section:
///   struct ExtHeader
///   FuncInfo, LineInfo, FieldReloc and ExternReloc subsections
/// The FuncInfo subsection is defined as below:
///   BTFFuncInfo Size
///   struct SecFuncInfo for ELF section #1
///   A number of struct BPFFuncInfo for ELF section #1
///   struct SecFuncInfo for ELF section #2
///   A number of struct BPFFuncInfo for ELF section #2
///   ...
/// The LineInfo subsection is defined as below:
///   BPFLineInfo Size
///   struct SecLineInfo for ELF section #1
///   A number of struct BPFLineInfo for ELF section #1
///   struct SecLineInfo for ELF section #2
///   A number of struct BPFLineInfo for ELF section #2
///   ...
/// The FieldReloc subsection is defined as below:
///   BPFFieldReloc Size
///   struct SecFieldReloc for ELF section #1
///   A number of struct BPFFieldReloc for ELF section #1
///   struct SecFieldReloc for ELF section #2
///   A number of struct BPFFieldReloc for ELF section #2
///   ...
///
/// The section formats are also defined at
///    https://github.com/torvalds/linux/blob/master/include/uapi/linux/btf.h
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_BTF_H
#define LLVM_LIB_TARGET_BPF_BTF_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/TrailingObjects.h"

namespace llvm {
/// BPF Type Format (.BTF / .BTF.ext) binary layout definitions.
namespace BTF {

/// Magic number and version for the .BTF section header.
enum : uint32_t {
  MAGIC = 0xeB9F, ///< .BTF section magic number.
  VERSION = 1     ///< .BTF section format version.
};

/// Sizes in bytes of various things in the BTF format.
enum {
  HeaderSize = 24,         ///< Size of struct Header.
  ExtHeaderSize = 32,      ///< Size of struct ExtHeader.
  CommonTypeSize = 12,     ///< Size of struct CommonType.
  BTFArraySize = 12,       ///< Size of struct BTFArray.
  BTFEnumSize = 8,         ///< Size of struct BTFEnum.
  BTFEnum64Size = 12,      ///< Size of struct BTFEnum64.
  BTFMemberSize = 12,      ///< Size of struct BTFMember.
  BTFParamSize = 8,        ///< Size of struct BTFParam.
  BTFDataSecVarSize = 12,  ///< Size of one DATASEC variable entry.
  SecFuncInfoSize = 8,     ///< Size of struct SecFuncInfo.
  SecLineInfoSize = 8,     ///< Size of struct SecLineInfo.
  SecFieldRelocSize = 8,   ///< Size of struct SecFieldReloc.
  BPFFuncInfoSize = 8,     ///< Size of struct BPFFuncInfo.
  BPFLineInfoSize = 16,    ///< Size of struct BPFLineInfo.
  BPFFieldRelocSize = 16,  ///< Size of struct BPFFieldReloc.
};

/// The .BTF section header definition.
struct Header {
  uint16_t Magic;  ///< Magic value
  uint8_t Version; ///< Version number
  uint8_t Flags;   ///< Extra flags
  uint32_t HdrLen; ///< Length of this header

  /// All offsets are in bytes relative to the end of this header.
  uint32_t TypeOff; ///< Offset of type section
  uint32_t TypeLen; ///< Length of type section
  uint32_t StrOff;  ///< Offset of string section
  uint32_t StrLen;  ///< Length of string section
};

/// Limits on variable-length type payloads.
enum : uint32_t {
  MAX_VLEN = 0xffff ///< Max # of struct/union/enum members or func args
};

/// BTF type kind codes stored in CommonType::Info.
enum TypeKinds : uint8_t {
#define HANDLE_BTF_KIND(ID, NAME) BTF_KIND_##NAME = ID,
#include "BTF.def"
};

// Constants for CommonType::Info field.
/// Kind flag bit set when a FWD type refers to a union.
constexpr uint32_t FWD_UNION_FLAG = 1u << 31;
/// Kind flag bit set when an ENUM or ENUM64 type is signed.
constexpr uint32_t ENUM_SIGNED_FLAG = 1u << 31;

/// The BTF common type definition. Different kinds may have
/// additional information after this structure data.
struct CommonType {
  /// Type name offset in the string table.
  uint32_t NameOff;

  /// Packed vlen, kind, and kind_flag for this type.
  ///
  /// "Info" bits arrangement:
  /// Bits  0-15: vlen (e.g. # of struct's members)
  /// Bits 16-23: unused
  /// Bits 24-28: kind (e.g. int, ptr, array...etc)
  /// Bits 29-30: unused
  /// Bit     31: kind_flag, currently used by
  ///             struct, union and fwd
  uint32_t Info;

  union {
    /// Size in bytes for INT, ENUM, STRUCT, and UNION kinds.
    uint32_t Size;
    /// Type id referring to another type for PTR, TYPEDEF, and similar kinds.
    uint32_t Type;
  };

  /// Returns the BTF type kind from Info bits 24-28.
  /// \returns The BTF type kind code.
  uint32_t getKind() const { return Info >> 24 & 0x1f; }
  /// Returns the variable-length count from Info bits 0-15.
  /// \returns The vlen field from the low 16 bits of Info.
  uint32_t getVlen() const { return Info & 0xffff; }
};

// For some specific BTF_KIND, "struct CommonType" is immediately
// followed by extra data.

// BTF_KIND_INT is followed by a u32 and the following
// is the 32 bits arrangement:
// BTF_INT_ENCODING(VAL) : (((VAL) & 0x0f000000) >> 24)
// BTF_INT_OFFSET(VAL) : (((VAL & 0x00ff0000)) >> 16)
// BTF_INT_BITS(VAL) : ((VAL) & 0x000000ff)

/// Attributes stored in the INT_ENCODING.
enum : uint8_t {
  INT_SIGNED = (1 << 0), ///< Integer is signed.
  INT_CHAR = (1 << 1),   ///< Integer is a character type.
  INT_BOOL = (1 << 2)    ///< Integer is a boolean type.
};

/// BTF_KIND_ENUM is followed by multiple "struct BTFEnum".
/// The exact number of btf_enum is stored in the vlen (of the
/// info in "struct CommonType").
struct BTFEnum {
  uint32_t NameOff; ///< Enum name offset in the string table
  int32_t Val;      ///< Enum member value
};

/// BTF_KIND_ENUM64 is followed by multiple "struct BTFEnum64".
/// The exact number of BTFEnum64 is stored in the vlen (of the
/// info in "struct CommonType").
struct BTFEnum64 {
  uint32_t NameOff;  ///< Enum name offset in the string table
  uint32_t Val_Lo32; ///< Enum member lo32 value
  uint32_t Val_Hi32; ///< Enum member hi32 value
};

/// BTF_KIND_ARRAY is followed by one "struct BTFArray".
struct BTFArray {
  uint32_t ElemType;  ///< Element type
  uint32_t IndexType; ///< Index type
  uint32_t Nelems;    ///< Number of elements for this array
};

/// A single member of a BTF struct or union type.
///
/// BTF_KIND_STRUCT and BTF_KIND_UNION are followed
/// by multiple "struct BTFMember".  The exact number
/// of BTFMember is stored in the vlen (of the info in
/// "struct CommonType").
///
/// If the struct/union contains any bitfield member,
/// the Offset below represents BitOffset (bits 0 - 23)
/// and BitFieldSize(bits 24 - 31) with BitFieldSize = 0
/// for non bitfield members. Otherwise, the Offset
/// represents the BitOffset.
struct BTFMember {
  uint32_t NameOff; ///< Member name offset in the string table
  uint32_t Type;    ///< Member type
  uint32_t Offset;  ///< BitOffset or BitFieldSize+BitOffset
};

/// BTF_KIND_FUNC_PROTO are followed by multiple "struct BTFParam".
/// The exist number of BTFParam is stored in the vlen (of the info
/// in "struct CommonType").
struct BTFParam {
  uint32_t NameOff; ///< Parameter name offset in the string table
  uint32_t Type;    ///< Parameter type id
};

/// BTF_KIND_FUNC can be global, static or extern.
enum : uint8_t {
  FUNC_STATIC = 0, ///< Function has static linkage.
  FUNC_GLOBAL = 1, ///< Function has global linkage.
  FUNC_EXTERN = 2, ///< Function is an extern declaration.
};

/// Variable scoping information.
enum : uint8_t {
  VAR_STATIC = 0,           ///< Linkage: InternalLinkage
  VAR_GLOBAL_ALLOCATED = 1, ///< Linkage: ExternalLinkage
  VAR_GLOBAL_EXTERNAL = 2,  ///< Linkage: ExternalLinkage
};

/// BTF_KIND_DATASEC are followed by multiple "struct BTFDataSecVar".
/// The exist number of BTFDataSec is stored in the vlen (of the info
/// in "struct CommonType").
struct BTFDataSec {
  uint32_t Type;   ///< A BTF_KIND_VAR type
  uint32_t Offset; ///< In-section offset
  uint32_t Size;   ///< Occupied memory size
};

/// The .BTF.ext section header definition.
struct ExtHeader {
  uint16_t Magic;  ///< Magic value
  uint8_t Version; ///< Version number
  uint8_t Flags;   ///< Extra flags
  uint32_t HdrLen; ///< Length of this header

  uint32_t FuncInfoOff;   ///< Offset of func info section
  uint32_t FuncInfoLen;   ///< Length of func info section
  uint32_t LineInfoOff;   ///< Offset of line info section
  uint32_t LineInfoLen;   ///< Length of line info section
  uint32_t FieldRelocOff; ///< Offset of offset reloc section
  uint32_t FieldRelocLen; ///< Length of offset reloc section
};

/// Specifying one function info.
struct BPFFuncInfo {
  uint32_t InsnOffset; ///< Byte offset in the section
  uint32_t TypeId;     ///< Type id referring to .BTF type section
};

/// Specifying function info's in one section.
struct SecFuncInfo {
  uint32_t SecNameOff;  ///< Section name index in the .BTF string table
  uint32_t NumFuncInfo; ///< Number of func info's in this section
};

/// Specifying one line info.
struct BPFLineInfo {
  uint32_t InsnOffset;  ///< Byte offset in this section
  uint32_t FileNameOff; ///< File name index in the .BTF string table
  uint32_t LineOff;     ///< Line index in the .BTF string table
  uint32_t LineCol;     ///< Line num: line_col >> 10,
                        ///  col num: line_col & 0x3ff
  /// Returns the source line number encoded in LineCol.
  /// \returns The line number from the high bits of LineCol.
  uint32_t getLine() const { return LineCol >> 10; }
  /// Returns the source column number encoded in LineCol.
  /// \returns The column number from the low bits of LineCol.
  uint32_t getCol() const { return LineCol & 0x3ff; }
};

/// Specifying line info's in one section.
struct SecLineInfo {
  uint32_t SecNameOff;  ///< Section name index in the .BTF string table
  uint32_t NumLineInfo; ///< Number of line info's in this section
};

/// Specifying one offset relocation.
struct BPFFieldReloc {
  uint32_t InsnOffset;    ///< Byte offset in this section
  uint32_t TypeID;        ///< TypeID for the relocation
  uint32_t OffsetNameOff; ///< The string to traverse types
  uint32_t RelocKind;     ///< What to patch the instruction
};

/// Specifying offset relocation's in one section.
struct SecFieldReloc {
  uint32_t SecNameOff;    ///< Section name index in the .BTF string table
  uint32_t NumFieldReloc; ///< Number of offset reloc's in this section
};

/// CO-RE relocation kind codes used in .BTF.ext section.
enum PatchableRelocKind : uint32_t {
  FIELD_BYTE_OFFSET = 0, ///< Patch with the field's byte offset.
  FIELD_BYTE_SIZE,       ///< Patch with the field's byte size.
  FIELD_EXISTENCE,       ///< Patch with whether the field exists.
  FIELD_SIGNEDNESS,      ///< Patch with whether the field is signed.
  FIELD_LSHIFT_U64,      ///< Patch with the left-shift amount for a u64 field.
  FIELD_RSHIFT_U64,      ///< Patch with the right-shift amount for a u64 field.
  BTF_TYPE_ID_LOCAL,     ///< Patch with the local BTF type id.
  BTF_TYPE_ID_REMOTE,    ///< Patch with the remote (target) BTF type id.
  TYPE_EXISTENCE,        ///< Patch with whether the type exists.
  TYPE_SIZE,             ///< Patch with the type's size in bytes.
  ENUM_VALUE_EXISTENCE,  ///< Patch with whether the enum value exists.
  ENUM_VALUE,            ///< Patch with the enum value.
  TYPE_MATCH,            ///< Patch with whether the type matches.
  MAX_FIELD_RELOC_KIND,  ///< Number of CO-RE relocation kinds.
};

// Define a number of sub-types for CommonType, each with:
// - An accessor for a relevant "tail" information (data fields that
//   follow the CommonType record in binary format).
// - A classof() definition based on CommonType::getKind() value to
//   allow use with dyn_cast<>() function.

// For CommonType sub-types that are followed by a single entry of
// some type in the binary format.
#define BTF_DEFINE_TAIL(Type, Accessor)                                        \
  const Type &Accessor() const { return *getTrailingObjects(); }

// For CommonType sub-types that are followed by CommonType::getVlen()
// number of entries of some type in the binary format.
#define BTF_DEFINE_TAIL_ARR(Type, Accessor)                                    \
  ArrayRef<Type> Accessor() const { return getTrailingObjects(getVlen()); }

/// BTF_KIND_ARRAY type with a trailing BTFArray descriptor.
struct ArrayType final : CommonType,
                         private TrailingObjects<ArrayType, BTFArray> {
  friend TrailingObjects;
  /// Returns the array descriptor that follows this type.
  /// \returns The trailing BTFArray descriptor for this type.
  BTF_DEFINE_TAIL(BTFArray, getArray)

  /// True if \p V is a BTF_KIND_ARRAY type.
  /// \param V The common type to test.
  /// \returns True if \p V has kind BTF_KIND_ARRAY.
  static bool classof(const CommonType *V) {
    return V->getKind() == BTF_KIND_ARRAY;
  }
};

/// BTF_KIND_STRUCT or BTF_KIND_UNION type with trailing BTFMember entries.
struct StructType final : CommonType,
                          private TrailingObjects<StructType, BTFMember> {
  friend TrailingObjects;
  /// Returns the member descriptors that follow this type.
  /// \returns The trailing BTFMember entries for this type.
  BTF_DEFINE_TAIL_ARR(BTFMember, members)

  /// True if \p V is a BTF_KIND_STRUCT or BTF_KIND_UNION type.
  /// \param V The common type to test.
  /// \returns True if \p V has kind BTF_KIND_STRUCT or BTF_KIND_UNION.
  static bool classof(const CommonType *V) {
    return V->getKind() == BTF_KIND_STRUCT || V->getKind() == BTF_KIND_UNION;
  }
};

/// BTF_KIND_ENUM type with trailing BTFEnum value entries.
struct EnumType final : CommonType, private TrailingObjects<EnumType, BTFEnum> {
  friend TrailingObjects;
  /// Returns the enumerator values that follow this type.
  /// \returns The trailing BTFEnum entries for this type.
  BTF_DEFINE_TAIL_ARR(BTFEnum, values)

  /// True if \p V is a BTF_KIND_ENUM type.
  /// \param V The common type to test.
  /// \returns True if \p V has kind BTF_KIND_ENUM.
  static bool classof(const CommonType *V) {
    return V->getKind() == BTF_KIND_ENUM;
  }
};

/// BTF_KIND_ENUM64 type with trailing BTFEnum64 value entries.
struct Enum64Type final : CommonType,
                          private TrailingObjects<Enum64Type, BTFEnum64> {
  friend TrailingObjects;
  /// Returns the 64-bit enumerator values that follow this type.
  /// \returns The trailing BTFEnum64 entries for this type.
  BTF_DEFINE_TAIL_ARR(BTFEnum64, values)

  /// True if \p V is a BTF_KIND_ENUM64 type.
  /// \param V The common type to test.
  /// \returns True if \p V has kind BTF_KIND_ENUM64.
  static bool classof(const CommonType *V) {
    return V->getKind() == BTF_KIND_ENUM64;
  }
};

#undef BTF_DEFINE_TAIL
#undef BTF_DEFINE_TAIL_ARR

} // End namespace BTF.
} // End namespace llvm.

#endif
