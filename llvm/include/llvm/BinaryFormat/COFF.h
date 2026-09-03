//===-- llvm/BinaryFormat/COFF.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains an definitions used in Windows COFF Files.
//
// Structures and enums defined within this file where created using
// information from Microsoft's publicly available PE/COFF format document:
//
// Microsoft Portable Executable and Common Object File Format Specification
// Revision 8.1 - February 15, 2008
//
// As of 5/2/2010, hosted by Microsoft at:
// http://www.microsoft.com/whdc/system/platform/firmware/pecoff.mspx
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_COFF_H
#define LLVM_BINARYFORMAT_COFF_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include <cassert>

namespace llvm {
/// Constants, structures, and helpers for the Windows COFF and PE formats.
namespace COFF {

/// Maximum number of sections a COFF object can have (inclusive).
const int32_t MaxNumberOfSections16 = 65279;

// The PE signature bytes that follows the DOS stub header.
static const char PEMagic[] = {'P', 'E', '\0', '\0'};

static const char BigObjMagic[] = {
    '\xc7', '\xa1', '\xba', '\xd1', '\xee', '\xba', '\xa9', '\x4b',
    '\xaf', '\x20', '\xfa', '\xf6', '\x6a', '\xa4', '\xdc', '\xb8',
};

static const char ClGlObjMagic[] = {
    '\x38', '\xfe', '\xb3', '\x0c', '\xa5', '\xd9', '\xab', '\x4d',
    '\xac', '\x9b', '\xd6', '\xb6', '\x22', '\x26', '\x53', '\xc2',
};

// The signature bytes that start a .res file.
static const char WinResMagic[] = {
    '\x00', '\x00', '\x00', '\x00', '\x20', '\x00', '\x00', '\x00',
    '\xff', '\xff', '\x00', '\x00', '\xff', '\xff', '\x00', '\x00',
};

/// Fixed sizes in bytes of common COFF structures.
enum {
  Header16Size = 20,   ///< Size of a standard COFF file header.
  Header32Size = 56,   ///< Size of a bigobj COFF file header prefix layout.
  NameSize = 8,        ///< Size of a short section or symbol name field.
  Symbol16Size = 18,   ///< Size of a standard COFF symbol table entry.
  Symbol32Size = 20,   ///< Size of a bigobj COFF symbol table entry.
  SectionSize = 40,    ///< Size of a COFF section header.
  RelocationSize = 10  ///< Size of a COFF relocation entry.
};

/// Standard COFF file header.
struct header {
  uint16_t Machine;              ///< Target machine type (\c MachineTypes).
  int32_t NumberOfSections;      ///< Number of sections in the file.
  uint32_t TimeDateStamp;        ///< Time and date the file was created.
  uint32_t PointerToSymbolTable; ///< File offset of the COFF symbol table.
  uint32_t NumberOfSymbols;      ///< Number of entries in the symbol table.
  uint16_t SizeOfOptionalHeader; ///< Size of the optional header, or 0.
  uint16_t Characteristics;      ///< File characteristics flags.
};

/// Microsoft bigobj COFF file header for objects with many sections.
struct BigObjHeader {
  /// Constants for the Microsoft bigobj COFF header version field.
  enum : uint16_t {
    MinBigObjectVersion = 2 ///< Minimum supported bigobj header version number.
  };

  uint16_t Sig1; ///< Must be IMAGE_FILE_MACHINE_UNKNOWN (0).
  uint16_t Sig2; ///< Must be 0xFFFF.
  uint16_t Version;              ///< Bigobj header version.
  uint16_t Machine;              ///< Target machine type (\c MachineTypes).
  uint32_t TimeDateStamp;        ///< Time and date the file was created.
  uint8_t UUID[16];              ///< UUID identifying the bigobj format.
  uint32_t unused1;              ///< Reserved; must be zero.
  uint32_t unused2;              ///< Reserved; must be zero.
  uint32_t unused3;              ///< Reserved; must be zero.
  uint32_t unused4;              ///< Reserved; must be zero.
  uint32_t NumberOfSections;     ///< Number of sections in the file.
  uint32_t PointerToSymbolTable; ///< File offset of the COFF symbol table.
  uint32_t NumberOfSymbols;      ///< Number of entries in the symbol table.
};

/// COFF target machine type identifiers.
enum MachineTypes : unsigned {
  MT_Invalid = 0xffff, ///< Invalid or unspecified machine type.

  IMAGE_FILE_MACHINE_UNKNOWN = 0x0,     ///< Contents assumed applicable to any machine.
  IMAGE_FILE_MACHINE_AM33 = 0x1D3,      ///< Matsushita AM33.
  IMAGE_FILE_MACHINE_AMD64 = 0x8664,    ///< x64 (AMD64).
  IMAGE_FILE_MACHINE_ARM = 0x1C0,       ///< ARM little endian.
  IMAGE_FILE_MACHINE_ARMNT = 0x1C4,     ///< ARM Thumb-2 little endian.
  IMAGE_FILE_MACHINE_ARM64 = 0xAA64,    ///< ARM64 little endian.
  IMAGE_FILE_MACHINE_ARM64EC = 0xA641,  ///< ARM64EC (Emulation Compatible).
  IMAGE_FILE_MACHINE_ARM64X = 0xA64E,   ///< ARM64X mixed native/EC image.
  IMAGE_FILE_MACHINE_EBC = 0xEBC,       ///< EFI byte code.
  IMAGE_FILE_MACHINE_I386 = 0x14C,      ///< Intel 386 or later.
  IMAGE_FILE_MACHINE_IA64 = 0x200,      ///< Intel Itanium.
  IMAGE_FILE_MACHINE_M32R = 0x9041,     ///< Mitsubishi M32R little endian.
  IMAGE_FILE_MACHINE_MIPS16 = 0x266,    ///< MIPS16.
  IMAGE_FILE_MACHINE_MIPSFPU = 0x366,   ///< MIPS with FPU.
  IMAGE_FILE_MACHINE_MIPSFPU16 = 0x466, ///< MIPS16 with FPU.
  IMAGE_FILE_MACHINE_POWERPC = 0x1F0,   ///< PowerPC little endian.
  IMAGE_FILE_MACHINE_POWERPCFP = 0x1F1, ///< PowerPC with floating point.
  IMAGE_FILE_MACHINE_R4000 = 0x166,     ///< MIPS R4000 little endian.
  IMAGE_FILE_MACHINE_RISCV32 = 0x5032,  ///< RISC-V 32-bit address space.
  IMAGE_FILE_MACHINE_RISCV64 = 0x5064,  ///< RISC-V 64-bit address space.
  IMAGE_FILE_MACHINE_RISCV128 = 0x5128, ///< RISC-V 128-bit address space.
  IMAGE_FILE_MACHINE_SH3 = 0x1A2,       ///< Hitachi SH3.
  IMAGE_FILE_MACHINE_SH3DSP = 0x1A3,    ///< Hitachi SH3 DSP.
  IMAGE_FILE_MACHINE_SH4 = 0x1A6,       ///< Hitachi SH4.
  IMAGE_FILE_MACHINE_SH5 = 0x1A8,       ///< Hitachi SH5.
  IMAGE_FILE_MACHINE_THUMB = 0x1C2,     ///< ARM Thumb / Thumb-2 little endian.
  IMAGE_FILE_MACHINE_WCEMIPSV2 = 0x169  ///< MIPS little-endian WCE v2.
};

/// Return true if \p Machine is ARM64EC or ARM64X.
/// \param Machine COFF machine type to test.
/// \return True if \p Machine is ARM64EC or ARM64X.
template <typename T> bool isArm64EC(T Machine) {
  return Machine == IMAGE_FILE_MACHINE_ARM64EC ||
         Machine == IMAGE_FILE_MACHINE_ARM64X;
}

/// Return true if \p Machine is any ARM64 variant (including EC/X).
/// \param Machine COFF machine type to test.
/// \return True if \p Machine is ARM64, ARM64EC, or ARM64X.
template <typename T> bool isAnyArm64(T Machine) {
  return Machine == IMAGE_FILE_MACHINE_ARM64 || isArm64EC(Machine);
}

/// Return true if \p Machine is a 64-bit COFF target (AMD64 or ARM64*).
/// \param Machine COFF machine type to test.
/// \return True if \p Machine is AMD64 or any ARM64 variant.
template <typename T> bool is64Bit(T Machine) {
  return Machine == IMAGE_FILE_MACHINE_AMD64 || isAnyArm64(Machine);
}

/// COFF file header characteristic flags.
enum Characteristics : unsigned {
  C_Invalid = 0, ///< Invalid or empty characteristics value.

  /// The file does not contain base relocations and must be loaded at its
  /// preferred base. If this cannot be done, the loader will error.
  IMAGE_FILE_RELOCS_STRIPPED = 0x0001,
  /// The file is valid and can be run.
  IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002,
  /// COFF line numbers have been stripped. This is deprecated and should be
  /// 0.
  IMAGE_FILE_LINE_NUMS_STRIPPED = 0x0004,
  /// COFF symbol table entries for local symbols have been removed. This is
  /// deprecated and should be 0.
  IMAGE_FILE_LOCAL_SYMS_STRIPPED = 0x0008,
  /// Aggressively trim working set. This is deprecated and must be 0.
  IMAGE_FILE_AGGRESSIVE_WS_TRIM = 0x0010,
  /// Image can handle > 2GiB addresses.
  IMAGE_FILE_LARGE_ADDRESS_AWARE = 0x0020,
  /// Little endian: the LSB precedes the MSB in memory. This is deprecated
  /// and should be 0.
  IMAGE_FILE_BYTES_REVERSED_LO = 0x0080,
  /// Machine is based on a 32bit word architecture.
  IMAGE_FILE_32BIT_MACHINE = 0x0100,
  /// Debugging info has been removed.
  IMAGE_FILE_DEBUG_STRIPPED = 0x0200,
  /// If the image is on removable media, fully load it and copy it to swap.
  IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP = 0x0400,
  /// If the image is on network media, fully load it and copy it to swap.
  IMAGE_FILE_NET_RUN_FROM_SWAP = 0x0800,
  /// The image file is a system file, not a user program.
  IMAGE_FILE_SYSTEM = 0x1000,
  /// The image file is a DLL.
  IMAGE_FILE_DLL = 0x2000,
  /// This file should only be run on a uniprocessor machine.
  IMAGE_FILE_UP_SYSTEM_ONLY = 0x4000,
  /// Big endian: the MSB precedes the LSB in memory. This is deprecated
  /// and should be 0.
  IMAGE_FILE_BYTES_REVERSED_HI = 0x8000
};

/// Predefined Windows resource type identifiers.
enum ResourceTypeID : unsigned {
  RID_Cursor = 1,       ///< Hardware-dependent cursor resource.
  RID_Bitmap = 2,       ///< Bitmap resource.
  RID_Icon = 3,         ///< Hardware-dependent icon resource.
  RID_Menu = 4,         ///< Menu resource.
  RID_Dialog = 5,       ///< Dialog box.
  RID_String = 6,       ///< String-table entry.
  RID_FontDir = 7,      ///< Font directory resource.
  RID_Font = 8,         ///< Font resource.
  RID_Accelerator = 9,  ///< Accelerator table.
  RID_RCData = 10,      ///< Application-defined raw data.
  RID_MessageTable = 11, ///< Message-table entry.
  RID_Group_Cursor = 12, ///< Hardware-independent cursor group.
  RID_Group_Icon = 14,   ///< Hardware-independent icon group.
  RID_Version = 16,      ///< Version resource.
  RID_DLGInclude = 17,   ///< Dialog include resource name.
  RID_PlugPlay = 19,     ///< Plug and Play resource.
  RID_VXD = 20,          ///< VXD resource.
  RID_AniCursor = 21,    ///< Animated cursor.
  RID_AniIcon = 22,      ///< Animated icon.
  RID_HTML = 23,         ///< HTML resource.
  RID_Manifest = 24,     ///< Side-by-side assembly manifest.
};

/// COFF symbol table entry.
struct symbol {
  char Name[NameSize];        ///< Symbol name or string-table offset encoding.
  uint32_t Value;             ///< Value; meaning depends on section and class.
  int32_t SectionNumber;      ///< One-based section index, or a special number.
  uint16_t Type;              ///< Base and complex type encoding.
  uint8_t StorageClass;       ///< Storage class (\c SymbolStorageClass).
  uint8_t NumberOfAuxSymbols; ///< Number of following auxiliary symbol records.
};

/// Special COFF symbol section number values.
enum SymbolSectionNumber : int32_t {
  IMAGE_SYM_DEBUG = -2,    ///< Symbol provides debugging information.
  IMAGE_SYM_ABSOLUTE = -1, ///< Symbol is an absolute value (not an address).
  IMAGE_SYM_UNDEFINED = 0  ///< Symbol is undefined or common.
};

/// Storage class tells where and what the symbol represents
enum SymbolStorageClass {
  SSC_Invalid = 0xff, ///< Invalid or unspecified storage class.

  IMAGE_SYM_CLASS_END_OF_FUNCTION = -1,  ///< Physical end of function
  IMAGE_SYM_CLASS_NULL = 0,              ///< No symbol
  IMAGE_SYM_CLASS_AUTOMATIC = 1,         ///< Stack variable
  IMAGE_SYM_CLASS_EXTERNAL = 2,          ///< External symbol
  IMAGE_SYM_CLASS_STATIC = 3,            ///< Static
  IMAGE_SYM_CLASS_REGISTER = 4,          ///< Register variable
  IMAGE_SYM_CLASS_EXTERNAL_DEF = 5,      ///< External definition
  IMAGE_SYM_CLASS_LABEL = 6,             ///< Label
  IMAGE_SYM_CLASS_UNDEFINED_LABEL = 7,   ///< Undefined label
  IMAGE_SYM_CLASS_MEMBER_OF_STRUCT = 8,  ///< Member of structure
  IMAGE_SYM_CLASS_ARGUMENT = 9,          ///< Function argument
  IMAGE_SYM_CLASS_STRUCT_TAG = 10,       ///< Structure tag
  IMAGE_SYM_CLASS_MEMBER_OF_UNION = 11,  ///< Member of union
  IMAGE_SYM_CLASS_UNION_TAG = 12,        ///< Union tag
  IMAGE_SYM_CLASS_TYPE_DEFINITION = 13,  ///< Type definition
  IMAGE_SYM_CLASS_UNDEFINED_STATIC = 14, ///< Undefined static
  IMAGE_SYM_CLASS_ENUM_TAG = 15,         ///< Enumeration tag
  IMAGE_SYM_CLASS_MEMBER_OF_ENUM = 16,   ///< Member of enumeration
  IMAGE_SYM_CLASS_REGISTER_PARAM = 17,   ///< Register parameter
  IMAGE_SYM_CLASS_BIT_FIELD = 18,        ///< Bit field
  /// ".bb" or ".eb" - beginning or end of block
  IMAGE_SYM_CLASS_BLOCK = 100,
  /// ".bf" or ".ef" - beginning or end of function
  IMAGE_SYM_CLASS_FUNCTION = 101,
  IMAGE_SYM_CLASS_END_OF_STRUCT = 102, ///< End of structure
  IMAGE_SYM_CLASS_FILE = 103,          ///< File name
  /// Line number, reformatted as symbol
  IMAGE_SYM_CLASS_SECTION = 104,
  IMAGE_SYM_CLASS_WEAK_EXTERNAL = 105, ///< Duplicate tag
  /// External symbol in dmert public lib
  IMAGE_SYM_CLASS_CLR_TOKEN = 107
};

/// Fundamental COFF symbol base type codes.
enum SymbolBaseType : unsigned {
  IMAGE_SYM_TYPE_NULL = 0,   ///< No type information or unknown base type.
  IMAGE_SYM_TYPE_VOID = 1,   ///< Used with void pointers and functions.
  IMAGE_SYM_TYPE_CHAR = 2,   ///< A character (signed byte).
  IMAGE_SYM_TYPE_SHORT = 3,  ///< A 2-byte signed integer.
  IMAGE_SYM_TYPE_INT = 4,    ///< A natural integer type on the target.
  IMAGE_SYM_TYPE_LONG = 5,   ///< A 4-byte signed integer.
  IMAGE_SYM_TYPE_FLOAT = 6,  ///< A 4-byte floating-point number.
  IMAGE_SYM_TYPE_DOUBLE = 7, ///< An 8-byte floating-point number.
  IMAGE_SYM_TYPE_STRUCT = 8, ///< A structure.
  IMAGE_SYM_TYPE_UNION = 9,  ///< An union.
  IMAGE_SYM_TYPE_ENUM = 10,  ///< An enumerated type.
  IMAGE_SYM_TYPE_MOE = 11,   ///< A member of enumeration (a specific value).
  IMAGE_SYM_TYPE_BYTE = 12,  ///< A byte; unsigned 1-byte integer.
  IMAGE_SYM_TYPE_WORD = 13,  ///< A word; unsigned 2-byte integer.
  IMAGE_SYM_TYPE_UINT = 14,  ///< An unsigned integer of natural size.
  IMAGE_SYM_TYPE_DWORD = 15  ///< An unsigned 4-byte integer.
};

/// Derived (complex) COFF symbol type codes and the packing shift.
enum SymbolComplexType : unsigned {
  IMAGE_SYM_DTYPE_NULL = 0,     ///< No complex type; simple scalar variable.
  IMAGE_SYM_DTYPE_POINTER = 1,  ///< A pointer to base type.
  IMAGE_SYM_DTYPE_FUNCTION = 2, ///< A function that returns a base type.
  IMAGE_SYM_DTYPE_ARRAY = 3,    ///< An array of base type.

  /// Type is formed as (base + (derived << SCT_COMPLEX_TYPE_SHIFT))
  SCT_COMPLEX_TYPE_SHIFT = 4
};

/// Auxiliary symbol record type identifiers.
enum AuxSymbolType {
  IMAGE_AUX_SYMBOL_TYPE_TOKEN_DEF = 1 ///< CLR token definition auxiliary.
};

/// COFF section header.
struct section {
  char Name[NameSize];            ///< Section name or string-table offset encoding.
  uint32_t VirtualSize;           ///< Total size of the section when loaded.
  uint32_t VirtualAddress;        ///< Address of the first byte when loaded.
  uint32_t SizeOfRawData;         ///< Size of the section's data on disk.
  uint32_t PointerToRawData;      ///< File pointer to the section's first page.
  uint32_t PointerToRelocations;  ///< File pointer to the relocation entries.
  uint32_t PointerToLineNumbers;  ///< File pointer to the line-number entries.
  uint16_t NumberOfRelocations;   ///< Number of relocation entries.
  uint16_t NumberOfLineNumbers;   ///< Number of line-number entries.
  uint32_t Characteristics;       ///< Section flags (\c SectionCharacteristics).
};

/// COFF section characteristic flags.
enum SectionCharacteristics : uint32_t {
  SC_Invalid = 0xffffffff, ///< Invalid section characteristics value.

  IMAGE_SCN_TYPE_NOLOAD = 0x00000002,          ///< Reserved.
  IMAGE_SCN_TYPE_NO_PAD = 0x00000008,          ///< Section should not be padded to next boundary.
  IMAGE_SCN_CNT_CODE = 0x00000020,             ///< Section contains executable code.
  IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040, ///< Section contains initialized data.
  IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080, ///< Section contains uninitialized data.
  IMAGE_SCN_LNK_OTHER = 0x00000100,            ///< Reserved.
  IMAGE_SCN_LNK_INFO = 0x00000200,             ///< Section contains comments or other info.
  IMAGE_SCN_LNK_REMOVE = 0x00000800,           ///< Section will not become part of the image.
  IMAGE_SCN_LNK_COMDAT = 0x00001000,           ///< Section contains COMDAT data.
  IMAGE_SCN_GPREL = 0x00008000,                ///< Section contains data referenced through the GP.
  IMAGE_SCN_MEM_PURGEABLE = 0x00020000,        ///< Reserved.
  IMAGE_SCN_MEM_16BIT = 0x00020000,            ///< Reserved (same value as PURGEABLE).
  IMAGE_SCN_MEM_LOCKED = 0x00040000,           ///< Reserved.
  IMAGE_SCN_MEM_PRELOAD = 0x00080000,          ///< Reserved.
  IMAGE_SCN_ALIGN_1BYTES = 0x00100000,         ///< Align data on a 1-byte boundary.
  IMAGE_SCN_ALIGN_2BYTES = 0x00200000,         ///< Align data on a 2-byte boundary.
  IMAGE_SCN_ALIGN_4BYTES = 0x00300000,         ///< Align data on a 4-byte boundary.
  IMAGE_SCN_ALIGN_8BYTES = 0x00400000,         ///< Align data on an 8-byte boundary.
  IMAGE_SCN_ALIGN_16BYTES = 0x00500000,        ///< Align data on a 16-byte boundary.
  IMAGE_SCN_ALIGN_32BYTES = 0x00600000,        ///< Align data on a 32-byte boundary.
  IMAGE_SCN_ALIGN_64BYTES = 0x00700000,        ///< Align data on a 64-byte boundary.
  IMAGE_SCN_ALIGN_128BYTES = 0x00800000,       ///< Align data on a 128-byte boundary.
  IMAGE_SCN_ALIGN_256BYTES = 0x00900000,       ///< Align data on a 256-byte boundary.
  IMAGE_SCN_ALIGN_512BYTES = 0x00A00000,       ///< Align data on a 512-byte boundary.
  IMAGE_SCN_ALIGN_1024BYTES = 0x00B00000,      ///< Align data on a 1024-byte boundary.
  IMAGE_SCN_ALIGN_2048BYTES = 0x00C00000,      ///< Align data on a 2048-byte boundary.
  IMAGE_SCN_ALIGN_4096BYTES = 0x00D00000,      ///< Align data on a 4096-byte boundary.
  IMAGE_SCN_ALIGN_8192BYTES = 0x00E00000,      ///< Align data on an 8192-byte boundary.
  IMAGE_SCN_ALIGN_MASK = 0x00F00000,           ///< Mask for section alignment bits.
  IMAGE_SCN_LNK_NRELOC_OVFL = 0x01000000,      ///< Section contains extended relocations.
  IMAGE_SCN_MEM_DISCARDABLE = 0x02000000,      ///< Section can be discarded as needed.
  IMAGE_SCN_MEM_NOT_CACHED = 0x04000000,       ///< Section cannot be cached.
  IMAGE_SCN_MEM_NOT_PAGED = 0x08000000,        ///< Section cannot be paged.
  IMAGE_SCN_MEM_SHARED = 0x10000000,           ///< Section can be shared in memory.
  IMAGE_SCN_MEM_EXECUTE = 0x20000000,          ///< Section can be executed as code.
  IMAGE_SCN_MEM_READ = 0x40000000,             ///< Section can be read.
  IMAGE_SCN_MEM_WRITE = 0x80000000             ///< Section can be written to.
};

/// COFF relocation entry.
struct relocation {
  uint32_t VirtualAddress;    ///< Address of the item to which relocation is applied.
  uint32_t SymbolTableIndex;  ///< Zero-based index into the symbol table.
  uint16_t Type;              ///< Relocation type (architecture-specific).
};

/// i386 COFF relocation type codes.
enum RelocationTypeI386 : unsigned {
  IMAGE_REL_I386_ABSOLUTE = 0x0000, ///< Relocation is ignored.
  IMAGE_REL_I386_DIR16 = 0x0001,    ///< Not supported.
  IMAGE_REL_I386_REL16 = 0x0002,    ///< Not supported.
  IMAGE_REL_I386_DIR32 = 0x0006,    ///< Direct 32-bit reference to the symbol's VA.
  IMAGE_REL_I386_DIR32NB = 0x0007,  ///< Direct 32-bit reference to the symbol's RVA.
  IMAGE_REL_I386_SEG12 = 0x0009,    ///< Not supported.
  IMAGE_REL_I386_SECTION = 0x000A,  ///< Section index of the symbol.
  IMAGE_REL_I386_SECREL = 0x000B,   ///< 32-bit offset from the section base.
  IMAGE_REL_I386_TOKEN = 0x000C,    ///< CLR token.
  IMAGE_REL_I386_SECREL7 = 0x000D,  ///< 7-bit offset from the section base.
  IMAGE_REL_I386_REL32 = 0x0014     ///< 32-bit relative displacement to the symbol.
};

/// AMD64 COFF relocation type codes.
enum RelocationTypeAMD64 : unsigned {
  IMAGE_REL_AMD64_ABSOLUTE = 0x0000,  ///< Relocation is ignored.
  IMAGE_REL_AMD64_ADDR64 = 0x0001,    ///< 64-bit VA of the relocation target.
  IMAGE_REL_AMD64_ADDR32 = 0x0002,    ///< 32-bit VA of the relocation target.
  IMAGE_REL_AMD64_ADDR32NB = 0x0003,  ///< 32-bit address without an image base.
  IMAGE_REL_AMD64_REL32 = 0x0004,     ///< 32-bit relative address from the byte following.
  IMAGE_REL_AMD64_REL32_1 = 0x0005,   ///< 32-bit relative address from byte distance 1.
  IMAGE_REL_AMD64_REL32_2 = 0x0006,   ///< 32-bit relative address from byte distance 2.
  IMAGE_REL_AMD64_REL32_3 = 0x0007,   ///< 32-bit relative address from byte distance 3.
  IMAGE_REL_AMD64_REL32_4 = 0x0008,   ///< 32-bit relative address from byte distance 4.
  IMAGE_REL_AMD64_REL32_5 = 0x0009,   ///< 32-bit relative address from byte distance 5.
  IMAGE_REL_AMD64_SECTION = 0x000A,   ///< Section index of the symbol.
  IMAGE_REL_AMD64_SECREL = 0x000B,    ///< 32-bit offset from the section base.
  IMAGE_REL_AMD64_SECREL7 = 0x000C,   ///< 7-bit unsigned offset from the section base.
  IMAGE_REL_AMD64_TOKEN = 0x000D,     ///< CLR token.
  IMAGE_REL_AMD64_SREL32 = 0x000E,    ///< 32-bit signed span-dependent value.
  IMAGE_REL_AMD64_PAIR = 0x000F,      ///< Pair that must immediately follow SPAN32.
  IMAGE_REL_AMD64_SSPAN32 = 0x0010    ///< 32-bit signed span-dependent value applied at link.
};

/// ARM COFF relocation type codes.
enum RelocationTypesARM : unsigned {
  IMAGE_REL_ARM_ABSOLUTE = 0x0000,   ///< Relocation is ignored.
  IMAGE_REL_ARM_ADDR32 = 0x0001,     ///< 32-bit VA of the target.
  IMAGE_REL_ARM_ADDR32NB = 0x0002,   ///< 32-bit RVA of the target.
  IMAGE_REL_ARM_BRANCH24 = 0x0003,   ///< 24-bit relative displacement to the target.
  IMAGE_REL_ARM_BRANCH11 = 0x0004,   ///< Reference to a subroutine call (Thumb).
  IMAGE_REL_ARM_TOKEN = 0x0005,      ///< CLR token.
  IMAGE_REL_ARM_BLX24 = 0x0008,      ///< 24-bit BLX displacement.
  IMAGE_REL_ARM_BLX11 = 0x0009,      ///< 11-bit BLX displacement (Thumb).
  IMAGE_REL_ARM_REL32 = 0x000A,      ///< 32-bit relative address from the byte following.
  IMAGE_REL_ARM_SECTION = 0x000E,    ///< Section index of the symbol.
  IMAGE_REL_ARM_SECREL = 0x000F,     ///< 32-bit offset from the section base.
  IMAGE_REL_ARM_MOV32A = 0x0010,     ///< ARM: MOVW/MOVT pair for a 32-bit address.
  IMAGE_REL_ARM_MOV32T = 0x0011,     ///< Thumb: MOVW/MOVT pair for a 32-bit address.
  IMAGE_REL_ARM_BRANCH20T = 0x0012,  ///< Thumb: 20-bit conditional branch.
  IMAGE_REL_ARM_BRANCH24T = 0x0014,  ///< Thumb: 24-bit unconditional branch.
  IMAGE_REL_ARM_BLX23T = 0x0015,     ///< Thumb: 23-bit BLX displacement.
  IMAGE_REL_ARM_PAIR = 0x0016,       ///< Relocation pair for a following relocation.
};

/// ARM64 COFF relocation type codes.
enum RelocationTypesARM64 : unsigned {
  IMAGE_REL_ARM64_ABSOLUTE = 0x0000,       ///< Relocation is ignored.
  IMAGE_REL_ARM64_ADDR32 = 0x0001,         ///< 32-bit VA of the target.
  IMAGE_REL_ARM64_ADDR32NB = 0x0002,       ///< 32-bit RVA of the target.
  IMAGE_REL_ARM64_BRANCH26 = 0x0003,       ///< 26-bit relative displacement to the target.
  IMAGE_REL_ARM64_PAGEBASE_REL21 = 0x0004, ///< Page-base relative 21-bit ADRP.
  IMAGE_REL_ARM64_REL21 = 0x0005,          ///< 21-bit relative displacement.
  IMAGE_REL_ARM64_PAGEOFFSET_12A = 0x0006, ///< 12-bit page offset for ADD/ADDS.
  IMAGE_REL_ARM64_PAGEOFFSET_12L = 0x0007, ///< 12-bit page offset for LDR/STR.
  IMAGE_REL_ARM64_SECREL = 0x0008,         ///< 32-bit offset from the section base.
  IMAGE_REL_ARM64_SECREL_LOW12A = 0x0009,  ///< Low 12 bits of section-relative ADD.
  IMAGE_REL_ARM64_SECREL_HIGH12A = 0x000A, ///< High 12 bits of section-relative ADD.
  IMAGE_REL_ARM64_SECREL_LOW12L = 0x000B,  ///< Low 12 bits of section-relative LDR/STR.
  IMAGE_REL_ARM64_TOKEN = 0x000C,          ///< CLR token.
  IMAGE_REL_ARM64_SECTION = 0x000D,        ///< Section index of the symbol.
  IMAGE_REL_ARM64_ADDR64 = 0x000E,         ///< 64-bit VA of the target.
  IMAGE_REL_ARM64_BRANCH19 = 0x000F,       ///< 19-bit conditional branch displacement.
  IMAGE_REL_ARM64_BRANCH14 = 0x0010,       ///< 14-bit TBZ/TBNZ displacement.
  IMAGE_REL_ARM64_REL32 = 0x0011,          ///< 32-bit relative address from the byte following.
};

/// MIPS COFF relocation type codes.
enum RelocationTypesMips : unsigned {
  IMAGE_REL_MIPS_ABSOLUTE = 0x0000,  ///< Relocation is ignored.
  IMAGE_REL_MIPS_REFHALF = 0x0001,   ///< High 16 bits of a 32-bit reference.
  IMAGE_REL_MIPS_REFWORD = 0x0002,   ///< 32-bit reference to the symbol's VA.
  IMAGE_REL_MIPS_JMPADDR = 0x0003,   ///< 26-bit relative displacement for jump.
  IMAGE_REL_MIPS_REFHI = 0x0004,     ///< High 16 bits of a 32-bit address (pair).
  IMAGE_REL_MIPS_REFLO = 0x0005,     ///< Low 16 bits of a 32-bit address.
  IMAGE_REL_MIPS_GPREL = 0x0006,     ///< 16-bit GP-relative reference.
  IMAGE_REL_MIPS_LITERAL = 0x0007,   ///< Same as GPREL.
  IMAGE_REL_MIPS_SECTION = 0x000A,   ///< Section index of the symbol.
  IMAGE_REL_MIPS_SECREL = 0x000B,    ///< 32-bit offset from the section base.
  IMAGE_REL_MIPS_SECRELLO = 0x000C,  ///< Low 16 bits of a section-relative address.
  IMAGE_REL_MIPS_SECRELHI = 0x000D,  ///< High 16 bits of a section-relative address.
  IMAGE_REL_MIPS_JMPADDR16 = 0x0010, ///< MIPS16 ISA jump displacement.
  IMAGE_REL_MIPS_REFWORDNB = 0x0022, ///< 32-bit RVA of the target.
  IMAGE_REL_MIPS_PAIR = 0x0025,      ///< Relocation pair that follows REFHI/SECRELHI.
};

/// Dynamic relocation types used in PE load configuration.
enum DynamicRelocationType : unsigned {
  IMAGE_DYNAMIC_RELOCATION_GUARD_RF_PROLOGUE = 1, ///< Control Flow Guard RF prologue.
  IMAGE_DYNAMIC_RELOCATION_GUARD_RF_EPILOGUE = 2, ///< Control Flow Guard RF epilogue.
  IMAGE_DYNAMIC_RELOCATION_GUARD_IMPORT_CONTROL_TRANSFER = 3, ///< CFG import control transfer.
  IMAGE_DYNAMIC_RELOCATION_GUARD_INDIR_CONTROL_TRANSFER = 4, ///< CFG indirect control transfer.
  IMAGE_DYNAMIC_RELOCATION_GUARD_SWITCHTABLE_BRANCH = 5, ///< CFG switch-table branch.
  IMAGE_DYNAMIC_RELOCATION_ARM64X = 6, ///< ARM64X dynamic relocation.
};

/// ARM64X dynamic value relocation fixup kinds.
enum Arm64XFixupType : uint8_t {
  IMAGE_DVRT_ARM64X_FIXUP_TYPE_ZEROFILL = 0, ///< Zero-fill the specified range.
  IMAGE_DVRT_ARM64X_FIXUP_TYPE_VALUE = 1,    ///< Write an absolute value.
  IMAGE_DVRT_ARM64X_FIXUP_TYPE_DELTA = 2,    ///< Apply a signed delta.
};

/// COMDAT section selection types.
enum COMDATType : uint8_t {
  IMAGE_COMDAT_SELECT_NODUPLICATES = 1, ///< Fail if a duplicate COMDAT exists.
  IMAGE_COMDAT_SELECT_ANY,              ///< Any section of the same COMDAT may be linked.
  IMAGE_COMDAT_SELECT_SAME_SIZE,        ///< Sections must have the same size.
  IMAGE_COMDAT_SELECT_EXACT_MATCH,      ///< Sections must match exactly.
  IMAGE_COMDAT_SELECT_ASSOCIATIVE,      ///< Section is linked if an associated section is.
  IMAGE_COMDAT_SELECT_LARGEST,          ///< The largest section is selected.
  IMAGE_COMDAT_SELECT_NEWEST            ///< The newest section is selected.
};

// Auxiliary Symbol Formats
/// Auxiliary record for a function-definition symbol.
struct AuxiliaryFunctionDefinition {
  uint32_t TagIndex;              ///< Symbol-table index of the related .bf entry.
  uint32_t TotalSize;             ///< Size of the executable code for the function.
  uint32_t PointerToLinenumber;   ///< File offset of the function's line-number entries.
  uint32_t PointerToNextFunction; ///< Symbol-table index of the next function.
  char unused[2];                 ///< Unused padding; must be zero.
};

/// Auxiliary record for beginning-of-function / end-of-function symbols.
struct AuxiliarybfAndefSymbol {
  uint8_t unused1[4];             ///< Unused; must be zero.
  uint16_t Linenumber;            ///< Actual ordinal line number within the source.
  uint8_t unused2[6];             ///< Unused; must be zero.
  uint32_t PointerToNextFunction; ///< Symbol-table index of the next function (.bf only).
  uint8_t unused3[2];             ///< Unused; must be zero.
};

/// Auxiliary record for a weak-external symbol.
struct AuxiliaryWeakExternal {
  uint32_t TagIndex;       ///< Symbol-table index of the symbol to link against.
  uint32_t Characteristics; ///< Search characteristics (\c WeakExternalCharacteristics).
  uint8_t unused[10];      ///< Unused; must be zero.
};

/// Characteristics describing how a weak external should be resolved.
enum WeakExternalCharacteristics : unsigned {
  IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY = 1, ///< Do not search libraries for a definition.
  IMAGE_WEAK_EXTERN_SEARCH_LIBRARY = 2,   ///< Search libraries for a definition.
  IMAGE_WEAK_EXTERN_SEARCH_ALIAS = 3,     ///< Symbol is an alias of TagIndex.
  IMAGE_WEAK_EXTERN_ANTI_DEPENDENCY = 4   ///< Symbol is an anti-dependency of TagIndex.
};

/// Auxiliary record for a section-definition symbol.
struct AuxiliarySectionDefinition {
  uint32_t Length;               ///< Size of section data; same as SizeOfRawData.
  uint16_t NumberOfRelocations;  ///< Number of relocation entries for the section.
  uint16_t NumberOfLinenumbers;  ///< Number of line-number entries for the section.
  uint32_t CheckSum;             ///< Checksum for communal data.
  uint32_t Number;               ///< One-based section number (associative COMDAT).
  uint8_t Selection;             ///< COMDAT selection type (\c COMDATType).
  char unused;                   ///< Unused; must be zero.
};

/// Auxiliary record for a CLR token definition.
struct AuxiliaryCLRToken {
  uint8_t AuxType;           ///< Must be IMAGE_AUX_SYMBOL_TYPE_TOKEN_DEF.
  uint8_t unused1;           ///< Unused; must be zero.
  uint32_t SymbolTableIndex; ///< Symbol index of the COFF symbol referenced by this token.
  char unused2[12];          ///< Unused; must be zero.
};

/// Union of COFF auxiliary symbol record layouts.
union Auxiliary {
  AuxiliaryFunctionDefinition FunctionDefinition; ///< Function-definition auxiliary.
  AuxiliarybfAndefSymbol bfAndefSymbol;           ///< Beginning/end-of-function auxiliary.
  AuxiliaryWeakExternal WeakExternal;             ///< Weak-external auxiliary.
  AuxiliarySectionDefinition SectionDefinition;   ///< Section-definition auxiliary.
};

/// The Import Directory Table.
///
/// There is a single array of these and one entry per imported DLL.
struct ImportDirectoryTableEntry {
  uint32_t ImportLookupTableRVA;  ///< RVA of the import lookup table.
  uint32_t TimeDateStamp;         ///< Stamp set to non-zero after binding.
  uint32_t ForwarderChain;        ///< Index of first forwarder reference, or -1.
  uint32_t NameRVA;               ///< RVA of the ASCII DLL name.
  uint32_t ImportAddressTableRVA; ///< RVA of the import address table.
};

/// The PE32 Import Lookup Table.
///
/// There is an array of these for each imported DLL. It represents either
/// the ordinal to import from the target DLL, or a name to lookup and import
/// from the target DLL.
///
/// This also happens to be the same format used by the Import Address Table
/// when it is initially written out to the image.
struct ImportLookupTableEntry32 {
  /// Packed ordinal flag and either ordinal or Hint/Name RVA.
  uint32_t data;

  /// Is this entry specified by ordinal, or name?
  /// \return True if the high bit is set (import by ordinal).
  bool isOrdinal() const { return data & 0x80000000; }

  /// Get the ordinal value of this entry. isOrdinal must be true.
  /// \return Import ordinal stored in the low 16 bits of data.
  uint16_t getOrdinal() const {
    assert(isOrdinal() && "ILT entry is not an ordinal!");
    return data & 0xFFFF;
  }

  /// Set the ordinal value and set isOrdinal to true.
  ///
  /// \param o Import ordinal to store in this entry.
  void setOrdinal(uint16_t o) {
    data = o;
    data |= 0x80000000;
  }

  /// Get the Hint/Name entry RVA. isOrdinal must be false.
  /// \return RVA of the Hint/Name table entry.
  uint32_t getHintNameRVA() const {
    assert(!isOrdinal() && "ILT entry is not a Hint/Name RVA!");
    return data;
  }

  /// Set the Hint/Name entry RVA and set isOrdinal to false.
  ///
  /// \param rva RVA of the Hint/Name table entry.
  void setHintNameRVA(uint32_t rva) { data = rva; }
};

/// The DOS compatible header at the front of all PEs.
struct DOSHeader {
  uint16_t Magic;                   ///< Magic number ("MZ").
  uint16_t UsedBytesInTheLastPage;  ///< Bytes used on the last page of the file.
  uint16_t FileSizeInPages;         ///< File size in 512-byte pages.
  uint16_t NumberOfRelocationItems; ///< Number of relocation entries.
  uint16_t HeaderSizeInParagraphs;  ///< Size of the header in 16-byte paragraphs.
  uint16_t MinimumExtraParagraphs;  ///< Minimum extra paragraphs needed.
  uint16_t MaximumExtraParagraphs;  ///< Maximum extra paragraphs needed.
  uint16_t InitialRelativeSS;       ///< Initial SS relative to the start of the image.
  uint16_t InitialSP;               ///< Initial SP value.
  uint16_t Checksum;                ///< Checksum; usually zero.
  uint16_t InitialIP;               ///< Initial IP value.
  uint16_t InitialRelativeCS;       ///< Initial CS relative to the start of the image.
  uint16_t AddressOfRelocationTable; ///< File address of the relocation table.
  uint16_t OverlayNumber;           ///< Overlay number.
  uint16_t Reserved[4];             ///< Reserved words.
  uint16_t OEMid;                   ///< OEM identifier.
  uint16_t OEMinfo;                 ///< OEM information.
  uint16_t Reserved2[10];           ///< Reserved words.
  uint32_t AddressOfNewExeHeader;   ///< File address of the new exe (PE) header.
};

/// PE32/PE32+ optional header fields shared by both formats.
struct PE32Header {
  /// Magic values identifying PE32 vs PE32+ optional headers.
  enum {
    PE32 = 0x10b,     ///< PE32 optional header magic.
    PE32_PLUS = 0x20b ///< PE32+ optional header magic.
  };

  uint16_t Magic;                     ///< Optional-header magic (PE32 or PE32+).
  uint8_t MajorLinkerVersion;         ///< Linker major version number.
  uint8_t MinorLinkerVersion;         ///< Linker minor version number.
  uint32_t SizeOfCode;                ///< Size of all code sections.
  uint32_t SizeOfInitializedData;     ///< Size of all initialized data sections.
  uint32_t SizeOfUninitializedData;   ///< Size of all uninitialized data sections.
  uint32_t AddressOfEntryPoint; ///< RVA of the entry point.
  uint32_t BaseOfCode;          ///< RVA of the start of the code section.
  uint32_t BaseOfData;          ///< RVA of the start of the data section (PE32 only).
  uint64_t ImageBase;                 ///< Preferred base address of the image.
  uint32_t SectionAlignment;          ///< Alignment of sections in memory.
  uint32_t FileAlignment;             ///< Alignment of raw section data in the file.
  uint16_t MajorOperatingSystemVersion; ///< Major OS version required.
  uint16_t MinorOperatingSystemVersion; ///< Minor OS version required.
  uint16_t MajorImageVersion;         ///< Major image version.
  uint16_t MinorImageVersion;         ///< Minor image version.
  uint16_t MajorSubsystemVersion;     ///< Major subsystem version.
  uint16_t MinorSubsystemVersion;     ///< Minor subsystem version.
  uint32_t Win32VersionValue;         ///< Reserved; must be zero.
  uint32_t SizeOfImage;               ///< Size of the image including headers, aligned.
  uint32_t SizeOfHeaders;             ///< Combined size of headers, aligned to FileAlignment.
  uint32_t CheckSum;                  ///< Image checksum.
  uint16_t Subsystem;                 ///< Windows subsystem (\c WindowsSubsystem).
  // FIXME: This should be DllCharacteristics to match the COFF spec.
  uint16_t DLLCharacteristics;        ///< DLL characteristic flags.
  uint64_t SizeOfStackReserve;        ///< Size of the stack to reserve.
  uint64_t SizeOfStackCommit;         ///< Size of the stack to commit.
  uint64_t SizeOfHeapReserve;         ///< Size of the local heap space to reserve.
  uint64_t SizeOfHeapCommit;          ///< Size of the local heap space to commit.
  uint32_t LoaderFlags;               ///< Reserved; must be zero.
  // FIXME: This should be NumberOfRvaAndSizes to match the COFF spec.
  uint32_t NumberOfRvaAndSize;        ///< Number of data-directory entries.
};

/// PE data-directory entry (RVA and size of a table).
struct DataDirectory {
  uint32_t RelativeVirtualAddress; ///< RVA of the table.
  uint32_t Size;                   ///< Size in bytes of the table.
};

/// Indices into the PE optional-header data directory array.
enum DataDirectoryIndex : unsigned {
  EXPORT_TABLE = 0,          ///< Export table.
  IMPORT_TABLE,              ///< Import table.
  RESOURCE_TABLE,            ///< Resource table.
  EXCEPTION_TABLE,           ///< Exception table.
  CERTIFICATE_TABLE,         ///< Certificate (attribute certificate) table.
  BASE_RELOCATION_TABLE,     ///< Base relocation table.
  DEBUG_DIRECTORY,           ///< Debug directory.
  ARCHITECTURE,              ///< Architecture-specific data.
  GLOBAL_PTR,                ///< RVA of the global pointer register value.
  TLS_TABLE,                 ///< Thread local storage table.
  LOAD_CONFIG_TABLE,         ///< Load configuration table.
  BOUND_IMPORT,              ///< Bound import table.
  IAT,                       ///< Import address table.
  DELAY_IMPORT_DESCRIPTOR,   ///< Delay import descriptor.
  CLR_RUNTIME_HEADER,        ///< CLR runtime header.

  NUM_DATA_DIRECTORIES       ///< Number of defined data-directory indices.
};

/// Windows subsystem identifiers for PE images.
enum WindowsSubsystem : unsigned {
  IMAGE_SUBSYSTEM_UNKNOWN = 0, ///< An unknown subsystem.
  IMAGE_SUBSYSTEM_NATIVE = 1,  ///< Device drivers and native Windows processes
  IMAGE_SUBSYSTEM_WINDOWS_GUI = 2,      ///< The Windows GUI subsystem.
  IMAGE_SUBSYSTEM_WINDOWS_CUI = 3,      ///< The Windows character subsystem.
  IMAGE_SUBSYSTEM_OS2_CUI = 5,          ///< The OS/2 character subsystem.
  IMAGE_SUBSYSTEM_POSIX_CUI = 7,        ///< The POSIX character subsystem.
  IMAGE_SUBSYSTEM_NATIVE_WINDOWS = 8,   ///< Native Windows 9x driver.
  IMAGE_SUBSYSTEM_WINDOWS_CE_GUI = 9,   ///< Windows CE.
  IMAGE_SUBSYSTEM_EFI_APPLICATION = 10, ///< An EFI application.
  IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER = 11, ///< An EFI driver with boot
                                                ///  services.
  IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER = 12,      ///< An EFI driver with run-time
                                                ///  services.
  IMAGE_SUBSYSTEM_EFI_ROM = 13,                 ///< An EFI ROM image.
  IMAGE_SUBSYSTEM_XBOX = 14,                    ///< XBOX.
  IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION = 16 ///< A BCD application.
};

/// DLL characteristic flags in the PE optional header.
enum DLLCharacteristics : unsigned {
  /// ASLR with 64 bit address space.
  IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA = 0x0020,
  /// DLL can be relocated at load time.
  IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE = 0x0040,
  /// Code integrity checks are enforced.
  IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY = 0x0080,
  /// Image is NX compatible.
  IMAGE_DLL_CHARACTERISTICS_NX_COMPAT = 0x0100,
  /// Isolation aware, but do not isolate the image.
  IMAGE_DLL_CHARACTERISTICS_NO_ISOLATION = 0x0200,
  /// Does not use structured exception handling (SEH). No SEH handler may be
  /// called in this image.
  IMAGE_DLL_CHARACTERISTICS_NO_SEH = 0x0400,
  /// Do not bind the image.
  IMAGE_DLL_CHARACTERISTICS_NO_BIND = 0x0800,
  /// Image should execute in an AppContainer.
  IMAGE_DLL_CHARACTERISTICS_APPCONTAINER = 0x1000,
  /// A WDM driver.
  IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER = 0x2000,
  /// Image supports Control Flow Guard.
  IMAGE_DLL_CHARACTERISTICS_GUARD_CF = 0x4000,
  /// Terminal Server aware.
  IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE = 0x8000
};

/// Extended DLL characteristic flags (debug directory type 20).
enum ExtendedDLLCharacteristics : unsigned {
  /// Image is CET compatible
  IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT = 0x0001,
  /// Image is CET compatible in strict mode
  IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT_STRICT_MODE = 0x0002,
  /// Image is CET compatible in such a way that context IP validation is
  /// relaxed
  IMAGE_DLL_CHARACTERISTICS_EX_CET_SET_CONTEXT_IP_VALIDATION_RELAXED_MODE =
      0x0004,
  /// Image is CET compatible in such a way that the use of
  /// dynamic APIs is restricted to processes only
  IMAGE_DLL_CHARACTERISTICS_EX_CET_DYNAMIC_APIS_ALLOW_IN_PROC_ONLY = 0x0008,
  /// Reserved for future use. Not used by MSVC link.exe
  IMAGE_DLL_CHARACTERISTICS_EX_CET_RESERVED_1 = 0x0010,
  /// Reserved for future use. Not used by MSVC link.exe
  IMAGE_DLL_CHARACTERISTICS_EX_CET_RESERVED_2 = 0x0020,
  /// Image is CFI compatible.
  IMAGE_DLL_CHARACTERISTICS_EX_FORWARD_CFI_COMPAT = 0x0040,
  /// Image is hotpatch compatible.
  IMAGE_DLL_CHARACTERISTICS_EX_HOTPATCH_COMPATIBLE = 0x0080,
};

/// PE debug directory entry type codes.
enum DebugType : unsigned {
  IMAGE_DEBUG_TYPE_UNKNOWN = 0,      ///< Unknown debug type.
  IMAGE_DEBUG_TYPE_COFF = 1,         ///< COFF debug information.
  IMAGE_DEBUG_TYPE_CODEVIEW = 2,     ///< CodeView debug information.
  IMAGE_DEBUG_TYPE_FPO = 3,          ///< Frame pointer omission information.
  IMAGE_DEBUG_TYPE_MISC = 4,         ///< Miscellaneous debug data.
  IMAGE_DEBUG_TYPE_EXCEPTION = 5,    ///< Exception information.
  IMAGE_DEBUG_TYPE_FIXUP = 6,        ///< Fixup information.
  IMAGE_DEBUG_TYPE_OMAP_TO_SRC = 7,  ///< Mapping from an RVA to source.
  IMAGE_DEBUG_TYPE_OMAP_FROM_SRC = 8, ///< Mapping from source to an RVA.
  IMAGE_DEBUG_TYPE_BORLAND = 9,      ///< Borland debug information.
  IMAGE_DEBUG_TYPE_RESERVED10 = 10,  ///< Reserved.
  IMAGE_DEBUG_TYPE_CLSID = 11,       ///< Reserved.
  IMAGE_DEBUG_TYPE_VC_FEATURE = 12,  ///< Visual C++ feature data.
  IMAGE_DEBUG_TYPE_POGO = 13,        ///< Profile Guided Optimization data.
  IMAGE_DEBUG_TYPE_ILTCG = 14,       ///< Incremental Link Time Code Generation.
  IMAGE_DEBUG_TYPE_MPX = 15,         ///< Intel MPX.
  IMAGE_DEBUG_TYPE_REPRO = 16,       ///< PE determinism / repro hash.
  IMAGE_DEBUG_TYPE_EX_DLLCHARACTERISTICS = 20, ///< Extended DLL characteristics.
};

/// PE base relocation type codes.
enum BaseRelocationType : unsigned {
  IMAGE_REL_BASED_ABSOLUTE = 0,      ///< Relocation is skipped (padding).
  IMAGE_REL_BASED_HIGH = 1,          ///< High 16 bits of a 32-bit address.
  IMAGE_REL_BASED_LOW = 2,           ///< Low 16 bits of a 32-bit address.
  IMAGE_REL_BASED_HIGHLOW = 3,       ///< High and low 16 bits of a 32-bit address.
  IMAGE_REL_BASED_HIGHADJ = 4,       ///< High 16 bits adjusted with the following low.
  IMAGE_REL_BASED_MIPS_JMPADDR = 5,  ///< MIPS jump instruction address.
  IMAGE_REL_BASED_ARM_MOV32A = 5,    ///< ARM MOVW/MOVT absolute address.
  IMAGE_REL_BASED_ARM_MOV32T = 7,    ///< Thumb MOVW/MOVT absolute address.
  IMAGE_REL_BASED_MIPS_JMPADDR16 = 9, ///< MIPS16 jump instruction address.
  IMAGE_REL_BASED_DIR64 = 10         ///< 64-bit address.
};

/// Import object type in a short import library header.
enum ImportType : unsigned {
  IMPORT_CODE = 0,  ///< Executable code.
  IMPORT_DATA = 1,  ///< Data.
  IMPORT_CONST = 2  ///< Specified as CONST in the .def file.
};

/// Import name type encoding in a short import library header.
enum ImportNameType : unsigned {
  /// Import is by ordinal. This indicates that the value in the Ordinal/Hint
  /// field of the import header is the import's ordinal. If this constant is
  /// not specified, then the Ordinal/Hint field should always be interpreted
  /// as the import's hint.
  IMPORT_ORDINAL = 0,
  /// The import name is identical to the public symbol name
  IMPORT_NAME = 1,
  /// The import name is the public symbol name, but skipping the leading ?,
  /// @, or optionally _.
  IMPORT_NAME_NOPREFIX = 2,
  /// The import name is the public symbol name, but skipping the leading ?,
  /// @, or optionally _, and truncating at the first @.
  IMPORT_NAME_UNDECORATE = 3,
  /// The import name is specified as a separate string in the import library
  /// object file.
  IMPORT_NAME_EXPORTAS = 4
};

/// Control Flow Guard and related flags from the load configuration.
enum class GuardFlags : uint32_t {
  /// Module performs control flow integrity checks using system-supplied
  /// support.
  CF_INSTRUMENTED = 0x100,
  /// Module performs control flow and write integrity checks.
  CFW_INSTRUMENTED = 0x200,
  /// Module contains valid control flow target metadata.
  CF_FUNCTION_TABLE_PRESENT = 0x400,
  /// Module does not make use of the /GS security cookie.
  SECURITY_COOKIE_UNUSED = 0x800,
  /// Module supports read only delay load IAT.
  PROTECT_DELAYLOAD_IAT = 0x1000,
  /// Delayload import table in its own .didat section (with nothing else in it)
  /// that can be freely reprotected.
  DELAYLOAD_IAT_IN_ITS_OWN_SECTION = 0x2000,
  /// Module contains suppressed export information. This also infers that the
  /// address taken IAT table is also present in the load config.
  CF_EXPORT_SUPPRESSION_INFO_PRESENT = 0x4000,
  /// Module enables suppression of exports.
  CF_ENABLE_EXPORT_SUPPRESSION = 0x8000,
  /// Module contains longjmp target information.
  CF_LONGJUMP_TABLE_PRESENT = 0x10000,
  /// Module contains EH continuation target information.
  EH_CONTINUATION_TABLE_PRESENT = 0x400000,
  /// Mask for the subfield that contains the stride of Control Flow Guard
  /// function table entries (that is, the additional count of bytes per table
  /// entry).
  CF_FUNCTION_TABLE_SIZE_MASK = 0xF0000000,
  CF_FUNCTION_TABLE_SIZE_5BYTES = 0x10000000,  ///< CFG function table entries are 5 bytes.
  CF_FUNCTION_TABLE_SIZE_6BYTES = 0x20000000,  ///< CFG function table entries are 6 bytes.
  CF_FUNCTION_TABLE_SIZE_7BYTES = 0x30000000,  ///< CFG function table entries are 7 bytes.
  CF_FUNCTION_TABLE_SIZE_8BYTES = 0x40000000,  ///< CFG function table entries are 8 bytes.
  CF_FUNCTION_TABLE_SIZE_9BYTES = 0x50000000,  ///< CFG function table entries are 9 bytes.
  CF_FUNCTION_TABLE_SIZE_10BYTES = 0x60000000, ///< CFG function table entries are 10 bytes.
  CF_FUNCTION_TABLE_SIZE_11BYTES = 0x70000000, ///< CFG function table entries are 11 bytes.
  CF_FUNCTION_TABLE_SIZE_12BYTES = 0x80000000, ///< CFG function table entries are 12 bytes.
  CF_FUNCTION_TABLE_SIZE_13BYTES = 0x90000000, ///< CFG function table entries are 13 bytes.
  CF_FUNCTION_TABLE_SIZE_14BYTES = 0xA0000000, ///< CFG function table entries are 14 bytes.
  CF_FUNCTION_TABLE_SIZE_15BYTES = 0xB0000000, ///< CFG function table entries are 15 bytes.
  CF_FUNCTION_TABLE_SIZE_16BYTES = 0xC0000000, ///< CFG function table entries are 16 bytes.
  CF_FUNCTION_TABLE_SIZE_17BYTES = 0xD0000000, ///< CFG function table entries are 17 bytes.
  CF_FUNCTION_TABLE_SIZE_18BYTES = 0xE0000000, ///< CFG function table entries are 18 bytes.
  CF_FUNCTION_TABLE_SIZE_19BYTES = 0xF0000000, ///< CFG function table entries are 19 bytes.
};

/// Short import library object header.
struct ImportHeader {
  uint16_t Sig1; ///< Must be IMAGE_FILE_MACHINE_UNKNOWN (0).
  uint16_t Sig2; ///< Must be 0xFFFF.
  uint16_t Version;       ///< Import header version.
  uint16_t Machine;       ///< Target machine type (\c MachineTypes).
  uint32_t TimeDateStamp; ///< Time and date the file was created.
  uint32_t SizeOfData;    ///< Size of the following string data.
  uint16_t OrdinalHint;   ///< Ordinal or hint, depending on name type.
  uint16_t TypeInfo;      ///< Packed import type and name type.

  /// Return the import type encoded in TypeInfo.
  /// \return Import type bits from the low 2 bits of TypeInfo.
  ImportType getType() const { return static_cast<ImportType>(TypeInfo & 0x3); }

  /// Return the import name type encoded in TypeInfo.
  /// \return Import name type bits from TypeInfo bits [4:2].
  ImportNameType getNameType() const {
    return static_cast<ImportNameType>((TypeInfo & 0x1C) >> 2);
  }
};

/// Magic values identifying CodeView debug sections.
enum CodeViewIdentifiers {
  DEBUG_SECTION_MAGIC = 0x4,            ///< Magic for a .debug$S / .debug$T section.
  DEBUG_HASHES_SECTION_MAGIC = 0x133C9C5 ///< Magic for a .debug$H section.
};

/// Feature flags stored in the `@feat.00` COFF symbol.
///
/// These flags show up in the @feat.00 symbol. They appear to be some kind of
/// compiler features bitfield read by link.exe.
enum Feat00Flags : uint32_t {
  SafeSEH = 0x1,         ///< Object is compatible with /safeseh.
  GuardStack = 0x100,    ///< Object was compiled with /GS.
  SDL = 0x200,           ///< Object was compiled with /sdl.
  GuardCF = 0x800,       ///< Object was compiled with /guard:cf.
  GuardEHCont = 0x4000,  ///< Object was compiled with /guard:ehcont.
  Kernel = 0x40000000,   ///< Object was compiled with /kernel.
};

/// ARM64EC thunk kinds used in hybrid images.
enum Arm64ECThunkType : uint8_t {
  GuestExit = 0, ///< Guest (x64) exit thunk.
  Entry = 1,     ///< Entry thunk into EC code.
  Exit = 4,      ///< Exit thunk from EC code.
};

/// Return true if \p SectionNumber is a reserved special section number.
///
/// \param SectionNumber COFF section number to test.
/// \return True if \p SectionNumber is reserved (zero or negative).
inline bool isReservedSectionNumber(int32_t SectionNumber) {
  return SectionNumber <= 0;
}

/// Encode section name based on string table offset.
/// The size of Out must be at least COFF::NameSize.
///
/// \param Out Destination buffer of at least \c NameSize bytes.
/// \param Offset Offset of the long name in the string table.
/// \return True if \p Offset was encoded into \p Out; false if too large.
LLVM_ABI bool encodeSectionName(char *Out, uint64_t Offset);

} // End namespace COFF.
} // End namespace llvm.

#endif
