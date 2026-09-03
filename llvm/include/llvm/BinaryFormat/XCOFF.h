//===-- llvm/BinaryFormat/XCOFF.h - The XCOFF file format -------*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines manifest constants for the XCOFF object file format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_XCOFF_H
#define LLVM_BINARYFORMAT_XCOFF_H

#include "llvm/Support/Compiler.h"
#include <stddef.h>
#include <stdint.h>

namespace llvm {
class StringRef;
template <unsigned> class SmallString;
template <typename T> class Expected;

/// Constants, structures, and helpers for the XCOFF object-file format.
namespace XCOFF {

/// Padding size appended to short file names in the file header.
constexpr size_t FileNamePadSize = 6;
/// Maximum length of a symbol or section name stored inline.
constexpr size_t NameSize = 8;
/// Maximum length of a name in a file auxiliary entry.
constexpr size_t AuxFileEntNameSize = 14;
/// Size in bytes of a 32-bit XCOFF file header.
constexpr size_t FileHeaderSize32 = 20;
/// Size in bytes of a 64-bit XCOFF file header.
constexpr size_t FileHeaderSize64 = 24;
/// Size in bytes of a full 32-bit XCOFF auxiliary file header.
constexpr size_t AuxFileHeaderSize32 = 72;
/// Size in bytes of a full 64-bit XCOFF auxiliary file header.
constexpr size_t AuxFileHeaderSize64 = 110;
/// Size in bytes of the short form of an auxiliary file header.
constexpr size_t AuxFileHeaderSizeShort = 28;
/// Size in bytes of a 32-bit XCOFF section header.
constexpr size_t SectionHeaderSize32 = 40;
/// Size in bytes of a 64-bit XCOFF section header.
constexpr size_t SectionHeaderSize64 = 72;
/// Size in bytes of a symbol table entry.
constexpr size_t SymbolTableEntrySize = 18;
/// Serialized size in bytes of a 32-bit XCOFF relocation entry.
constexpr size_t RelocationSerializationSize32 = 10;
/// Serialized size in bytes of a 64-bit XCOFF relocation entry.
constexpr size_t RelocationSerializationSize64 = 14;
/// Size in bytes of a 32-bit exception-section entry.
constexpr size_t ExceptionSectionEntrySize32 = 6;
/// Size in bytes of a 64-bit exception-section entry.
constexpr size_t ExceptionSectionEntrySize64 = 10;
/// Sentinel value indicating relocation count overflow in a section header.
constexpr uint16_t RelocOverflow = 65535;
/// Register number used for the alloca register in traceback tables.
constexpr uint8_t AllocRegNo = 31;

/// Reserved section numbers used in symbol table entries.
enum ReservedSectionNum : int16_t {
  N_DEBUG = -2, ///< Debug section number.
  N_ABS = -1,   ///< Absolute (non-relocatable) symbol.
  N_UNDEF = 0   ///< Undefined symbol.
};

/// Magic numbers identifying 32-bit and 64-bit XCOFF files.
enum MagicNumber : uint16_t {
  XCOFF32 = 0x01DF, ///< 32-bit XCOFF magic number.
  XCOFF64 = 0x01F7  ///< 64-bit XCOFF magic number.
};

// Masks for packing/unpacking the r_rsize field of relocations.

// The msb is used to indicate if the bits being relocated are signed or
// unsigned.
static constexpr uint8_t XR_SIGN_INDICATOR_MASK = 0x80;
// The 2nd msb is used to indicate that the binder has replaced/modified the
// original instruction.
static constexpr uint8_t XR_FIXUP_INDICATOR_MASK = 0x40;
// The remaining bits specify the bit length of the relocatable reference
// minus one.
static constexpr uint8_t XR_BIASED_LENGTH_MASK = 0x3f;

/// Flags present only in the 64-bit XCOFF auxiliary header.
enum AuxHeaderFlags64 : uint16_t {
  SHR_SYMTAB = 0x8000,  ///< At exec time, create shared symbol table for program
                        ///< (main program only).
  FORK_POLICY = 0x4000, ///< Forktree policy specified (main program only).
  FORK_COR = 0x2000     ///< If _AOUT_FORK_POLICY is set, specify copy-on-reference
                        ///< if this bit is set. Specify copy-on- write otherwise.
                        ///< If _AOUT_FORK_POLICY is 0, this bit is reserved for
                        ///< future use and should be set to 0.
};

/// Auxiliary-header interpretation version identifiers.
enum XCOFFInterpret : uint16_t {
  OLD_XCOFF_INTERPRET = 1, ///< Legacy auxiliary-header interpretation.
  NEW_XCOFF_INTERPRET = 2  ///< Current auxiliary-header interpretation.
};

/// File header flags for the f_flags field.
enum FileFlag : uint16_t {
  F_RELFLG = 0x0001,    ///< relocation info stripped from file
  F_EXEC = 0x0002,      ///< file is executable (i.e., it
                        ///< has a loader section)
  F_LNNO = 0x0004,      ///< line numbers stripped from file
  F_LSYMS = 0x0008,     ///< local symbols stripped from file
  F_FDPR_PROF = 0x0010, ///< file was profiled with FDPR
  F_FDPR_OPTI = 0x0020, ///< file was reordered with FDPR
  F_DSA = 0x0040,       ///< file uses Dynamic Segment Allocation (32-bit
                        ///< only)
  F_DEP_1 = 0x0080,     ///< Data Execution Protection bit 1
  F_VARPG = 0x0100,     ///< executable requests using variable size pages
  F_LPTEXT = 0x0400,    ///< executable requires large pages for text
  F_LPDATA = 0x0800,    ///< executable requires large pages for data
  F_DYNLOAD = 0x1000,   ///< file is dynamically loadable and
                        ///< executable (equivalent to F_EXEC on AIX)
  F_SHROBJ = 0x2000,    ///< file is a shared object
  /// File can be loaded by the system loader, but it is ignored by the linker
  /// if it is a member of an archive.
  F_LOADONLY = 0x4000,
  F_DEP_2 = 0x8000 ///< Data Execution Protection bit 2
};

// x_smclas field of x_csect from system header: /usr/include/syms.h
/// Storage Mapping Class definitions.
enum StorageMappingClass : uint8_t {
  //     READ ONLY CLASSES
  XMC_PR = 0,      ///< Program Code
  XMC_RO = 1,      ///< Read Only Constant
  XMC_DB = 2,      ///< Debug Dictionary Table
  XMC_GL = 6,      ///< Global Linkage (Interfile Interface Code)
  XMC_XO = 7,      ///< Extended Operation (Pseudo Machine Instruction)
  XMC_SV = 8,      ///< Supervisor Call (32-bit process only)
  XMC_SV64 = 17,   ///< Supervisor Call for 64-bit process
  XMC_SV3264 = 18, ///< Supervisor Call for both 32- and 64-bit processes
  XMC_TI = 12,     ///< Traceback Index csect
  XMC_TB = 13,     ///< Traceback Table csect

  //       READ WRITE CLASSES
  XMC_RW = 5,   ///< Read Write Data
  XMC_TC0 = 15, ///< TOC Anchor for TOC Addressability
  XMC_TC = 3,   ///< General TOC item
  XMC_TD = 16,  ///< Scalar data item in the TOC
  XMC_DS = 10,  ///< Descriptor csect
  XMC_UA = 4,   ///< Unclassified - Treated as Read Write
  XMC_BS = 9,   ///< BSS class (uninitialized static internal)
  XMC_UC = 11,  ///< Un-named Fortran Common

  XMC_TL = 20, ///< Initialized thread-local variable
  XMC_UL = 21, ///< Uninitialized thread-local variable
  XMC_TE = 22  ///< Symbol mapped at the end of TOC
};

/// Section type flags for the lower 16 bits of s_flags.
///
/// Flags for defining the section type. Masks for use with the (signed, 32-bit)
/// s_flags field of the section header structure, selecting for values in the
/// lower 16 bits. Defined in the system header `scnhdr.h`.
enum SectionTypeFlags : int32_t {
  STYP_PAD = 0x0008,    ///< Padding section.
  STYP_DWARF = 0x0010,  ///< DWARF debug section.
  STYP_TEXT = 0x0020,   ///< Executable text section.
  STYP_DATA = 0x0040,   ///< Initialized data section.
  STYP_BSS = 0x0080,    ///< Uninitialized data section.
  STYP_EXCEPT = 0x0100, ///< Exception section.
  STYP_INFO = 0x0200,   ///< Comment / info section.
  STYP_TDATA = 0x0400,  ///< Initialized thread-local data section.
  STYP_TBSS = 0x0800,   ///< Uninitialized thread-local data section.
  STYP_LOADER = 0x1000, ///< Loader section.
  STYP_DEBUG = 0x2000,  ///< Debug section.
  STYP_TYPCHK = 0x4000, ///< Type-check section.
  STYP_OVRFLO = 0x8000  ///< Overflow section.
};

/// DWARF section subtype flags for the upper 16 bits of s_flags.
///
/// Values for defining the section subtype of sections of type STYP_DWARF as
/// they would appear in the (signed, 32-bit) s_flags field of the section
/// header structure, contributing to the 16 most significant bits. Defined in
/// the system header `scnhdr.h`.
enum DwarfSectionSubtypeFlags : int32_t {
  SSUBTYP_DWINFO = 0x1'0000,  ///< DWARF info section
  SSUBTYP_DWLINE = 0x2'0000,  ///< DWARF line section
  SSUBTYP_DWPBNMS = 0x3'0000, ///< DWARF pubnames section
  SSUBTYP_DWPBTYP = 0x4'0000, ///< DWARF pubtypes section
  SSUBTYP_DWARNGE = 0x5'0000, ///< DWARF aranges section
  SSUBTYP_DWABREV = 0x6'0000, ///< DWARF abbrev section
  SSUBTYP_DWSTR = 0x7'0000,   ///< DWARF str section
  SSUBTYP_DWRNGES = 0x8'0000, ///< DWARF ranges section
  SSUBTYP_DWLOC = 0x9'0000,   ///< DWARF loc section
  SSUBTYP_DWFRAME = 0xA'0000, ///< DWARF frame section
  SSUBTYP_DWMAC = 0xB'0000    ///< DWARF macinfo section
};

/// Symbol storage classes for the n_sclass field of a symbol table entry.
///
/// The values come from `storclass.h` and `dbxstclass.h`.
enum StorageClass : uint8_t {
  // Storage classes used for symbolic debugging symbols.
  C_FILE = 103,  ///< File name.
  C_BINCL = 108, ///< Beginning of include file.
  C_EINCL = 109, ///< Ending of include file.
  C_GSYM = 128,  ///< Global variable.
  C_STSYM = 133, ///< Statically allocated symbol.
  C_BCOMM = 135, ///< Beginning of common block.
  C_ECOMM = 137, ///< End of common block.
  C_ENTRY = 141, ///< Alternate entry.
  C_BSTAT = 143, ///< Beginning of static block.
  C_ESTAT = 144, ///< End of static block.
  C_GTLS = 145,  ///< Global thread-local variable.
  C_STTLS = 146, ///< Static thread-local variable.

  // Storage classes used for DWARF symbols.
  C_DWARF = 112, ///< DWARF section symbol.

  // Storage classes used for absolute symbols.
  C_LSYM = 129,  ///< Automatic variable allocated on stack.
  C_PSYM = 130,  ///< Argument to subroutine allocated on stack.
  C_RSYM = 131,  ///< Register variable.
  C_RPSYM = 132, ///< Argument to function or procedure stored in register.
  C_ECOML = 136, ///< Local member of common block.
  C_FUN = 142,   ///< Function or procedure.

  // Storage classes used for undefined external symbols or
  // symbols of general sections.
  C_EXT = 2,       ///< External symbol.
  C_WEAKEXT = 111, ///< Weak external symbol.

  // Storage classes used for symbols of general sections.
  C_NULL = 0,     ///< No storage class.
  C_STAT = 3,     ///< Static.
  C_BLOCK = 100,  ///< ".bb" or ".eb".
  C_FCN = 101,    ///< ".bf" or ".ef".
  C_HIDEXT = 107, ///< Un-named external symbol.
  C_INFO = 110,   ///< Comment string in .info section.
  C_DECL = 140,   ///< Declaration of object (type).

  // Storage classes - Obsolete/Undocumented.
  C_AUTO = 1,     ///< Automatic variable.
  C_REG = 4,      ///< Register variable.
  C_EXTDEF = 5,   ///< External definition.
  C_LABEL = 6,    ///< Label.
  C_ULABEL = 7,   ///< Undefined label.
  C_MOS = 8,      ///< Member of structure.
  C_ARG = 9,      ///< Function argument.
  C_STRTAG = 10,  ///< Structure tag.
  C_MOU = 11,     ///< Member of union.
  C_UNTAG = 12,   ///< Union tag.
  C_TPDEF = 13,   ///< Type definition.
  C_USTATIC = 14, ///< Undefined static.
  C_ENTAG = 15,   ///< Enumeration tag.
  C_MOE = 16,     ///< Member of enumeration.
  C_REGPARM = 17, ///< Register parameter.
  C_FIELD = 18,   ///< Bit field.
  C_EOS = 102,    ///< End of structure.
  C_LINE = 104,   ///< Source line number (obsolete).
  C_ALIAS = 105,  ///< Duplicate tag.
  C_HIDDEN = 106, ///< Special storage class for external.
  C_EFCN = 255,   ///< Physical end of function.

  // Storage classes - reserved
  C_TCSYM = 134 ///< Reserved.
};

/// Symbol type codes for the lower 3 bits of csect auxiliary x_smtyp.
///
/// Flags for defining the symbol type. Values to be encoded into the lower 3
/// bits of the (unsigned, 8-bit) x_smtyp field of csect auxiliary symbol table
/// entries. Defined in the system header `syms.h`.
enum SymbolType : uint8_t {
  XTY_ER = 0, ///< External reference.
  XTY_SD = 1, ///< Csect definition for initialized storage.
  XTY_LD = 2, ///< Label definition.
              ///< Defines an entry point to an initialized csect.
  XTY_CM = 3  ///< Common csect definition. For uninitialized storage.
};

/// Symbol visibility encodings for the high 4 bits of n_type.
///
/// Values for visibility as they would appear when encoded in the high 4 bits
/// of the 16-bit unsigned n_type field of symbol table entries. Valid for
/// 32-bit XCOFF only when the vstamp in the auxiliary header is greater than 1.
enum VisibilityType : uint16_t {
  SYM_V_UNSPECIFIED = 0x0000, ///< Unspecified visibility.
  SYM_V_INTERNAL = 0x1000,    ///< Internal visibility.
  SYM_V_HIDDEN = 0x2000,      ///< Hidden visibility.
  SYM_V_PROTECTED = 0x3000,   ///< Protected visibility.
  SYM_V_EXPORTED = 0x4000     ///< Exported visibility.
};

/// Mask selecting the visibility bits in a symbol's n_type field.
constexpr uint16_t VISIBILITY_MASK = 0x7000;

/// Relocation type codes for XCOFF relocation entries.
///
/// Defined in `/usr/include/reloc.h`.
enum RelocationType : uint8_t {
  R_POS = 0x00, ///< Positive relocation. Provides the address of the referenced
                ///< symbol.
  R_RL = 0x0c,  ///< Positive indirect load relocation. Modifiable instruction.
  R_RLA = 0x0d, ///< Positive load address relocation. Modifiable instruction.

  R_NEG = 0x01, ///< Negative relocation. Provides the negative of the address
                ///< of the referenced symbol.
  R_REL = 0x02, ///< Relative to self relocation. Provides a displacement value
                ///< between the address of the referenced symbol and the
                ///< address being relocated.

  R_TOC = 0x03, ///< Relative to the TOC relocation. Provides a displacement
                ///< that is the difference between the address of the
                ///< referenced symbol and the TOC anchor csect.
  R_TRL = 0x12, ///< TOC relative indirect load relocation. Similar to R_TOC,
                ///< but not modifiable instruction.

  /// Relative to the TOC or to the thread-local storage base relocation.
  /// Compilers are not permitted to generate this relocation type. It is the
  /// result of a reversible transformation by the linker of an R_TOC relation
  /// that turned a load instruction into an add-immediate instruction.
  R_TRLA = 0x13,

  R_GL = 0x05, ///< Global linkage-external TOC address relocation. Provides the
               ///< address of the external TOC associated with a defined
               ///< external symbol.
  R_TCL = 0x06, ///< Local object TOC address relocation. Provides the address
                ///< of the local TOC entry of a defined external symbol.

  R_REF = 0x0f, ///< A non-relocating relocation. Used to prevent the binder
                ///< from garbage collecting a csect (such as code used for
                ///< dynamic initialization of non-local statics) for which
                ///< another csect has an implicit dependency.

  R_BA = 0x08, ///< Branch absolute relocation. Provides the address of the
               ///< referenced symbol. References a non-modifiable instruction.
  R_BR = 0x0a, ///< Branch relative to self relocation. Provides the
               ///< displacement that is the difference between the address of
               ///< the referenced symbol and the address of the referenced
               ///< branch instruction. References a non-modifiable instruction.
  R_RBA = 0x18, ///< Branch absolute relocation. Similar to R_BA but
                ///< references a modifiable instruction.
  R_RBR = 0x1a, ///< Branch relative to self relocation. Similar to the R_BR
                ///< relocation type, but references a modifiable instruction.

  R_TLS = 0x20,    ///< General-dynamic reference to TLS symbol.
  R_TLS_IE = 0x21, ///< Initial-exec reference to TLS symbol.
  R_TLS_LD = 0x22, ///< Local-dynamic reference to TLS symbol.
  R_TLS_LE = 0x23, ///< Local-exec reference to TLS symbol.
  R_TLSM = 0x24,  ///< Module reference to TLS. Provides a handle for the module
                  ///< containing the referenced symbol.
  R_TLSML = 0x25, ///< Module reference to the local TLS storage.

  R_TOCU = 0x30, ///< Relative to TOC upper. Specifies the high-order 16 bits of
                 ///< a large code model TOC-relative relocation.
  R_TOCL = 0x31 ///< Relative to TOC lower. Specifies the low-order 16 bits of a
                ///< large code model TOC-relative relocation.
};

/// String-type codes for file auxiliary entries.
enum CFileStringType : uint8_t {
  XFT_FN = 0,  ///< Specifies the source-file name.
  XFT_CT = 1,  ///< Specifies the compiler time stamp.
  XFT_CV = 2,  ///< Specifies the compiler version number.
  XFT_CD = 128 ///< Specifies compiler-defined information.
};

/// Source-language identifiers for file auxiliary entries.
enum CFileLangId : uint8_t {
  TB_C = 0,        ///< C language.
  TB_Fortran = 1,  ///< Fortran language.
  TB_CPLUSPLUS = 9 ///< C++ language.
};

/// XCOFF-specific CPU identifiers for file auxiliary entries.
///
/// Defined in AIX OS header: `/usr/include/aouthdr.h`.
enum CFileCpuId : uint8_t {
  TCPU_INVALID = 0, ///< Invalid id - assumes POWER for old objects.
  TCPU_PPC = 1,     ///< PowerPC common architecture 32 bit mode.
  TCPU_PPC64 = 2,   ///< PowerPC common architecture 64-bit mode.
  TCPU_COM = 3,     ///< POWER and PowerPC architecture common.
  TCPU_PWR = 4,     ///< POWER common architecture objects.
  TCPU_ANY = 5,     ///< Mixture of any incompatable POWER
                    ///< and PowerPC architecture implementations.
  TCPU_601 = 6,     ///< 601 implementation of PowerPC architecture.
  TCPU_603 = 7,     ///< 603 implementation of PowerPC architecture.
  TCPU_604 = 8,     ///< 604 implementation of PowerPC architecture.

  // The following are PowerPC 64-bit architectures.
  TCPU_620 = 16,   ///< 620 implementation of PowerPC architecture.
  TCPU_A35 = 17,   ///< A35 implementation of PowerPC architecture.
  TCPU_PWR5 = 18,  ///< POWER5 architecture.
  TCPU_970 = 19,   ///< PowerPC 970 architecture.
  TCPU_PWR6 = 20,  ///< POWER6 architecture.
  TCPU_PWR5X = 22, ///< POWER5+ architecture.
  TCPU_PWR6E = 23, ///< POWER6E architecture.
  TCPU_PWR7 = 24,  ///< POWER7 architecture.
  TCPU_PWR8 = 25,  ///< POWER8 architecture.
  TCPU_PWR9 = 26,  ///< POWER9 architecture.
  TCPU_PWR10 = 27, ///< POWER10 architecture.

  TCPU_PWRX = 224 ///< RS2 implementation of POWER architecture.
};

/// Auxiliary-entry type codes for 64-bit XCOFF symbol table entries.
enum SymbolAuxType : uint8_t {
  AUX_EXCEPT = 255, ///< Identifies an exception auxiliary entry.
  AUX_FCN = 254,    ///< Identifies a function auxiliary entry.
  AUX_SYM = 253,    ///< Identifies a symbol auxiliary entry.
  AUX_FILE = 252,   ///< Identifies a file auxiliary entry.
  AUX_CSECT = 251,  ///< Identifies a csect auxiliary entry.
  AUX_SECT = 250    ///< Identifies a SECT auxiliary entry.
};                  // 64-bit XCOFF file only.

/// Return the string name of a storage mapping class.
///
/// \param SMC Storage mapping class to convert.
/// \return String name of the storage mapping class.
LLVM_ABI StringRef getMappingClassString(XCOFF::StorageMappingClass SMC);
/// Return the string name of a relocation type.
///
/// \param Type Relocation type to convert.
/// \return String name of the relocation type.
LLVM_ABI StringRef getRelocationTypeString(XCOFF::RelocationType Type);
/// Return the string name of a CFileCpuId value.
///
/// \param TCPU CPU identifier to convert.
/// \return String name of the CPU identifier.
LLVM_ABI StringRef getTCPUString(XCOFF::CFileCpuId TCPU);
/// Decode fixed and floating parameter types from a traceback-table bitfield.
///
/// \param Value Encoded parameter-type bitfield.
/// \param FixedParmsNum Expected number of fixed-point parameters.
/// \param FloatingParmsNum Expected number of floating-point parameters.
/// \return Decoded parameter-type string, or an error on failure.
LLVM_ABI Expected<SmallString<32>> parseParmsType(uint32_t Value,
                                                  unsigned FixedParmsNum,
                                                  unsigned FloatingParmsNum);
/// Decode fixed, floating, and vector parameter types from a bitfield.
///
/// \param Value Encoded parameter-type bitfield with vector info.
/// \param FixedParmsNum Expected number of fixed-point parameters.
/// \param FloatingParmsNum Expected number of floating-point parameters.
/// \param VectorParmsNum Expected number of vector parameters.
/// \return Decoded parameter-type string, or an error on failure.
LLVM_ABI Expected<SmallString<32>>
parseParmsTypeWithVecInfo(uint32_t Value, unsigned FixedParmsNum,
                          unsigned FloatingParmsNum, unsigned VectorParmsNum);
/// Decode vector parameter element types from a traceback-table bitfield.
///
/// \param Value Encoded vector parameter-type bitfield.
/// \param ParmsNum Expected number of vector parameters.
/// \return Decoded vector parameter-type string, or an error on failure.
LLVM_ABI Expected<SmallString<32>> parseVectorParmsType(uint32_t Value,
                                                        unsigned ParmsNum);

/// Layout constants and language IDs for an AIX traceback table.
struct TracebackTable {
  /// Source-language identifiers encoded in a traceback table.
  enum LanguageID : uint8_t {
    C,          ///< C language.
    Fortran,    ///< Fortran language.
    Pascal,     ///< Pascal language.
    Ada,        ///< Ada language.
    PL1,        ///< PL/I language.
    Basic,      ///< BASIC language.
    Lisp,       ///< Lisp language.
    Cobol,      ///< COBOL language.
    Modula2,    ///< Modula-2 language.
    CPlusPlus,  ///< C++ language.
    Rpg,        ///< RPG language.
    PL8,        ///< PL.8 language.
    PLIX = PL8, ///< Alias for PL.8 / PLIX.
    Assembly,   ///< Assembly language.
    Java,       ///< Java language.
    ObjectiveC  ///< Objective-C language.
  };
  // Byte 1
  /// Mask selecting the traceback-table version field.
  static constexpr uint32_t VersionMask = 0xFF00'0000;
  /// Bit shift for the traceback-table version field.
  static constexpr uint8_t VersionShift = 24;

  // Byte 2
  /// Mask selecting the language-identifier field.
  static constexpr uint32_t LanguageIdMask = 0x00FF'0000;
  /// Bit shift for the language-identifier field.
  static constexpr uint8_t LanguageIdShift = 16;

  // Byte 3
  /// Mask indicating the procedure has global linkage.
  static constexpr uint32_t IsGlobalLinkageMask = 0x0000'8000;
  /// Mask indicating an out-of-line epilog or prologue.
  static constexpr uint32_t IsOutOfLineEpilogOrPrologueMask = 0x0000'4000;
  /// Mask indicating a traceback-table offset is present.
  static constexpr uint32_t HasTraceBackTableOffsetMask = 0x0000'2000;
  /// Mask indicating an internal (non-global) procedure.
  static constexpr uint32_t IsInternalProcedureMask = 0x0000'1000;
  /// Mask indicating controlled storage is present.
  static constexpr uint32_t HasControlledStorageMask = 0x0000'0800;
  /// Mask indicating the procedure is TOCless.
  static constexpr uint32_t IsTOClessMask = 0x0000'0400;
  /// Mask indicating floating-point operations are present.
  static constexpr uint32_t IsFloatingPointPresentMask = 0x0000'0200;
  /// Mask indicating floating-point log/abort is enabled.
  static constexpr uint32_t IsFloatingPointOperationLogOrAbortEnabledMask =
      0x0000'0100;

  // Byte 4
  /// Mask indicating the procedure is an interrupt handler.
  static constexpr uint32_t IsInterruptHandlerMask = 0x0000'0080;
  /// Mask indicating the function name is present in the table.
  static constexpr uint32_t IsFunctionNamePresentMask = 0x0000'0040;
  /// Mask indicating alloca is used by the procedure.
  static constexpr uint32_t IsAllocaUsedMask = 0x0000'0020;
  /// Mask selecting the on-condition directive field.
  static constexpr uint32_t OnConditionDirectiveMask = 0x0000'001C;
  /// Mask indicating the CR is saved by the procedure.
  static constexpr uint32_t IsCRSavedMask = 0x0000'0002;
  /// Mask indicating the LR is saved by the procedure.
  static constexpr uint32_t IsLRSavedMask = 0x0000'0001;
  /// Bit shift for the on-condition directive field.
  static constexpr uint8_t OnConditionDirectiveShift = 2;

  // Byte 5
  /// Mask indicating the back chain is stored on the stack.
  static constexpr uint32_t IsBackChainStoredMask = 0x8000'0000;
  /// Mask indicating the binder applied a fixup.
  static constexpr uint32_t IsFixupMask = 0x4000'0000;
  /// Mask selecting the number of floating-point registers saved.
  static constexpr uint32_t FPRSavedMask = 0x3F00'0000;
  /// Bit shift for the number of floating-point registers saved.
  static constexpr uint32_t FPRSavedShift = 24;

  // Byte 6
  /// Mask indicating an extension table is present.
  static constexpr uint32_t HasExtensionTableMask = 0x0080'0000;
  /// Mask indicating vector info is present.
  static constexpr uint32_t HasVectorInfoMask = 0x0040'0000;
  /// Mask selecting the number of general-purpose registers saved.
  static constexpr uint32_t GPRSavedMask = 0x003F'0000;
  /// Bit shift for the number of general-purpose registers saved.
  static constexpr uint32_t GPRSavedShift = 16;

  // Byte 7
  /// Mask selecting the number of fixed-point parameters.
  static constexpr uint32_t NumberOfFixedParmsMask = 0x0000'FF00;
  /// Bit shift for the number of fixed-point parameters.
  static constexpr uint8_t NumberOfFixedParmsShift = 8;

  // Byte 8
  /// Mask selecting the number of floating-point parameters.
  static constexpr uint32_t NumberOfFloatingPointParmsMask = 0x0000'00FE;
  /// Mask indicating parameters are present on the stack.
  static constexpr uint32_t HasParmsOnStackMask = 0x0000'0001;
  /// Bit shift for the number of floating-point parameters.
  static constexpr uint8_t NumberOfFloatingPointParmsShift = 1;

  // Masks to select leftmost bits for decoding parameter type information.
  // Bit to use when vector info is not presented.
  /// Bit indicating a floating-point parameter when vector info is absent.
  static constexpr uint32_t ParmTypeIsFloatingBit = 0x8000'0000;
  /// Bit indicating a double (vs float) when vector info is absent.
  static constexpr uint32_t ParmTypeFloatingIsDoubleBit = 0x4000'0000;
  // Bits to use when vector info is presented.
  /// Encoding bits for a fixed-point parameter when vector info is present.
  static constexpr uint32_t ParmTypeIsFixedBits = 0x0000'0000;
  /// Encoding bits for a vector parameter when vector info is present.
  static constexpr uint32_t ParmTypeIsVectorBits = 0x4000'0000;
  /// Encoding bits for a float parameter when vector info is present.
  static constexpr uint32_t ParmTypeIsFloatingBits = 0x8000'0000;
  /// Encoding bits for a double parameter when vector info is present.
  static constexpr uint32_t ParmTypeIsDoubleBits = 0xC000'0000;
  /// Mask selecting a two-bit parameter-type encoding.
  static constexpr uint32_t ParmTypeMask = 0xC000'0000;

  // Vector extension
  /// Mask selecting the number of vector registers saved.
  static constexpr uint16_t NumberOfVRSavedMask = 0xFC00;
  /// Mask indicating vector registers are saved on the stack.
  static constexpr uint16_t IsVRSavedOnStackMask = 0x0200;
  /// Mask indicating the procedure has a variable argument list.
  static constexpr uint16_t HasVarArgsMask = 0x0100;
  /// Bit shift for the number of vector registers saved.
  static constexpr uint8_t NumberOfVRSavedShift = 10;

  /// Mask selecting the number of vector parameters.
  static constexpr uint16_t NumberOfVectorParmsMask = 0x00FE;
  /// Mask indicating VMX instructions are present.
  static constexpr uint16_t HasVMXInstructionMask = 0x0001;
  /// Bit shift for the number of vector parameters.
  static constexpr uint8_t NumberOfVectorParmsShift = 1;

  /// Encoding bits for a vector char parameter type.
  static constexpr uint32_t ParmTypeIsVectorCharBit = 0x0000'0000;
  /// Encoding bits for a vector short parameter type.
  static constexpr uint32_t ParmTypeIsVectorShortBit = 0x4000'0000;
  /// Encoding bits for a vector int parameter type.
  static constexpr uint32_t ParmTypeIsVectorIntBit = 0x8000'0000;
  /// Encoding bits for a vector float parameter type.
  static constexpr uint32_t ParmTypeIsVectorFloatBit = 0xC000'0000;

  /// Width in bits of one parameter-type encoding field.
  static constexpr uint8_t WidthOfParamType = 2;
};

/// Extended traceback-table flag bits.
enum ExtendedTBTableFlag : uint8_t {
  TB_OS1 = 0x80,         ///< Reserved for OS use.
  TB_RESERVED = 0x40,    ///< Reserved for compiler.
  TB_SSP_CANARY = 0x20,  ///< stack smasher canary present on stack.
  TB_OS2 = 0x10,         ///< Reserved for OS use.
  TB_EH_INFO = 0x08,     ///< Exception handling info present.
  TB_LONGTBTABLE2 = 0x01 ///< Additional tbtable extension exists.
};

/// Return the string name of a traceback-table language identifier.
///
/// \param LangId Language identifier to convert.
/// \return String name of the language identifier.
LLVM_ABI StringRef
getNameForTracebackTableLanguageId(TracebackTable::LanguageID LangId);
/// Return a space-separated string of extended traceback-table flag names.
///
/// \param Flag Extended traceback-table flag bitfield.
/// \return Space-separated names of the set flag bits.
LLVM_ABI SmallString<32> getExtendedTBTableFlagString(uint8_t Flag);
/// Map a CPU name string to an XCOFF CFileCpuId value.
///
/// \param CPU CPU name to look up.
/// \return Matching CFileCpuId, or TCPU_INVALID if unrecognized.
LLVM_ABI XCOFF::CFileCpuId getCpuID(StringRef CPU);

/// Storage mapping class and symbol type for a csect.
struct CsectProperties {
  /// Construct csect properties from a mapping class and symbol type.
  ///
  /// \param SMC Storage mapping class.
  /// \param ST Symbol type.
  CsectProperties(StorageMappingClass SMC, SymbolType ST)
      : MappingClass(SMC), Type(ST) {}
  /// Storage mapping class of the csect.
  StorageMappingClass MappingClass;
  /// Symbol type of the csect.
  SymbolType Type;
};

} // end namespace XCOFF
} // end namespace llvm

#endif
