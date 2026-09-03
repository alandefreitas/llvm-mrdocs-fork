//===-- llvm/BinaryFormat/MachO.h - The MachO file format -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines manifest constants for the MachO object file format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MACHO_H
#define LLVM_BINARYFORMAT_MACHO_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SwapByteOrder.h"

namespace llvm {

class Triple;

/// Constants, structures, and helpers for the Mach-O object-file format.
namespace MachO {
// Enums from <mach-o/loader.h>
/// Mach-O and fat-binary magic numbers for mach_header::magic.
enum : uint32_t {
  // Constants for the "magic" field in llvm::MachO::mach_header and
  // llvm::MachO::mach_header_64
  MH_MAGIC = 0xFEEDFACEu, ///< 32-bit Mach-O magic (host endian).
  MH_CIGAM = 0xCEFAEDFEu, ///< 32-bit Mach-O magic, byte-swapped.
  MH_MAGIC_64 = 0xFEEDFACFu, ///< 64-bit Mach-O magic (host endian).
  MH_CIGAM_64 = 0xCFFAEDFEu, ///< 64-bit Mach-O magic, byte-swapped.
  FAT_MAGIC = 0xCAFEBABEu, ///< 32-bit fat-binary magic.
  FAT_CIGAM = 0xBEBAFECAu, ///< 32-bit fat-binary magic, byte-swapped.
  FAT_MAGIC_64 = 0xCAFEBABFu, ///< 64-bit fat-binary magic.
  FAT_CIGAM_64 = 0xBFBAFECAu ///< 64-bit fat-binary magic, byte-swapped.
};

/// Mach-O header filetype values (mach_header::filetype).
enum HeaderFileType {
  // Constants for the "filetype" field in llvm::MachO::mach_header and
  // llvm::MachO::mach_header_64
  MH_OBJECT = 0x1u, ///< Relocatable object file.
  MH_EXECUTE = 0x2u, ///< Demand-paged executable.
  MH_FVMLIB = 0x3u, ///< Fixed VM shared library file.
  MH_CORE = 0x4u, ///< Core file.
  MH_PRELOAD = 0x5u, ///< Preloaded executable.
  MH_DYLIB = 0x6u, ///< Dynamically bound shared library.
  MH_DYLINKER = 0x7u, ///< Dynamic linker shared library.
  MH_BUNDLE = 0x8u, ///< Dynamically bound bundle.
  MH_DYLIB_STUB = 0x9u, ///< Shared library stub for static linking only.
  MH_DSYM = 0xAu, ///< Companion debug-symbols file.
  MH_KEXT_BUNDLE = 0xBu, ///< Kernel extension.
  MH_FILESET = 0xCu, ///< File set containing multiple Mach-O binaries.
};

/// Flag bits for mach_header::flags and mach_header_64::flags.
enum {
  // Constant bits for the "flags" field in llvm::MachO::mach_header and
  // llvm::MachO::mach_header_64
  MH_NOUNDEFS = 0x00000001u, ///< No undefined references remain.
  MH_INCRLINK = 0x00000002u, ///< Output of an incremental link.
  MH_DYLDLINK = 0x00000004u, ///< Input for the dynamic linker.
  MH_BINDATLOAD = 0x00000008u, ///< Undefined references bound at load.
  MH_PREBOUND = 0x00000010u, ///< Dynamic references prebound.
  MH_SPLIT_SEGS = 0x00000020u, ///< Read-only and read-write segments split.
  MH_LAZY_INIT = 0x00000040u, ///< Shared library init is lazy.
  MH_TWOLEVEL = 0x00000080u, ///< Uses two-level namespace bindings.
  MH_FORCE_FLAT = 0x00000100u, ///< Forces flat namespace bindings.
  MH_NOMULTIDEFS = 0x00000200u, ///< No multiple symbol definitions allowed.
  MH_NOFIXPREBINDING = 0x00000400u, ///< Do not run fix-prebinding.
  MH_PREBINDABLE = 0x00000800u, ///< Binary can be prebound.
  MH_ALLMODSBOUND = 0x00001000u, ///< All two-level bindings known.
  MH_SUBSECTIONS_VIA_SYMBOLS = 0x00002000u, ///< Safe to divide sections by symbols.
  MH_CANONICAL = 0x00004000u, ///< Canonicalized (canonical MH form).
  MH_WEAK_DEFINES = 0x00008000u, ///< Contains weak external definitions.
  MH_BINDS_TO_WEAK = 0x00010000u, ///< Binds to weak symbols.
  MH_ALLOW_STACK_EXECUTION = 0x00020000u, ///< Stack is allowed to be executable.
  MH_ROOT_SAFE = 0x00040000u, ///< Safe to use from a setuid root process.
  MH_SETUID_SAFE = 0x00080000u, ///< Safe to use from a setuid process.
  MH_NO_REEXPORTED_DYLIBS = 0x00100000u, ///< No re-exported dylibs.
  MH_PIE = 0x00200000u, ///< Position-independent executable.
  MH_DEAD_STRIPPABLE_DYLIB = 0x00400000u, ///< Dylib may be dead-stripped.
  MH_HAS_TLV_DESCRIPTORS = 0x00800000u, ///< Contains thread-local variable descriptors.
  MH_NO_HEAP_EXECUTION = 0x01000000u, ///< Heap must not be executable.
  MH_APP_EXTENSION_SAFE = 0x02000000u, ///< Safe for app extensions.
  MH_NLIST_OUTOFSYNC_WITH_DYLDINFO = 0x04000000u, ///< nlist is out of sync with dyld info.
  MH_SIM_SUPPORT = 0x08000000u, ///< Simulator support.
  MH_DYLIB_IN_CACHE = 0x80000000u, ///< Dylib lives in the shared cache.
};

/// High bit of load_command::cmd requiring dyld to understand the command.
enum : uint32_t {
  // Flags for the "cmd" field in llvm::MachO::load_command
  LC_REQ_DYLD = 0x80000000u ///< Bit requiring dyld to understand the load command.
};

#define HANDLE_LOAD_COMMAND(LCName, LCValue, LCStruct) LCName = LCValue,

/// Mach-O load command type codes (load_command::cmd).
enum LoadCommandType : uint32_t {
#include "llvm/BinaryFormat/MachO.def"
};

#undef HANDLE_LOAD_COMMAND

/// Segment and section flag masks for segment_command and section headers.
enum : uint32_t {
  // Constant bits for the "flags" field in llvm::MachO::segment_command
  SG_HIGHVM = 0x1u, ///< Space after this segment is high VM.
  SG_FVMLIB = 0x2u, ///< Segment is for a fixed VM library.
  SG_NORELOC = 0x4u, ///< Segment has no relocations.
  SG_PROTECTED_VERSION_1 = 0x8u, ///< Segment uses protected-version-1 encryption.
  SG_READ_ONLY = 0x10u, ///< Segment is read-only after fixups.

  // Constant masks for the "flags" field in llvm::MachO::section and
  // llvm::MachO::section_64
  SECTION_TYPE = 0x000000ffu, ///< SECTION_TYPE
  SECTION_ATTRIBUTES = 0xffffff00u, ///< SECTION_ATTRIBUTES
  SECTION_ATTRIBUTES_USR = 0xff000000u, ///< SECTION_ATTRIBUTES_USR
  SECTION_ATTRIBUTES_SYS = 0x00ffff00u ///< SECTION_ATTRIBUTES_SYS
};

/// These are the section type and attributes fields.  A MachO section can
/// have only one Type, but can have any of the attributes specified.
enum SectionType : uint32_t {
  // Constant masks for the "flags[7:0]" field in llvm::MachO::section and
  // llvm::MachO::section_64 (mask "flags" with SECTION_TYPE)

  /// S_REGULAR - Regular section.
  S_REGULAR = 0x00u,
  /// S_ZEROFILL - Zero fill on demand section.
  S_ZEROFILL = 0x01u,
  /// S_CSTRING_LITERALS - Section with literal C strings.
  S_CSTRING_LITERALS = 0x02u,
  /// S_4BYTE_LITERALS - Section with 4 byte literals.
  S_4BYTE_LITERALS = 0x03u,
  /// S_8BYTE_LITERALS - Section with 8 byte literals.
  S_8BYTE_LITERALS = 0x04u,
  /// S_LITERAL_POINTERS - Section with pointers to literals.
  S_LITERAL_POINTERS = 0x05u,
  /// S_NON_LAZY_SYMBOL_POINTERS - Section with non-lazy symbol pointers.
  S_NON_LAZY_SYMBOL_POINTERS = 0x06u,
  /// S_LAZY_SYMBOL_POINTERS - Section with lazy symbol pointers.
  S_LAZY_SYMBOL_POINTERS = 0x07u,
  /// S_SYMBOL_STUBS - Section with symbol stubs, byte size of stub in
  /// the Reserved2 field.
  S_SYMBOL_STUBS = 0x08u, ///< SYMBOL STUBS.
  /// S_MOD_INIT_FUNC_POINTERS - Section with only function pointers for
  /// initialization.
  S_MOD_INIT_FUNC_POINTERS = 0x09u, ///< MOD INIT FUNC POINTERS.
  /// S_MOD_TERM_FUNC_POINTERS - Section with only function pointers for
  /// termination.
  S_MOD_TERM_FUNC_POINTERS = 0x0au, ///< MOD TERM FUNC POINTERS.
  /// S_COALESCED - Section contains symbols that are to be coalesced.
  S_COALESCED = 0x0bu,
  /// S_GB_ZEROFILL - Zero fill on demand section (that can be larger than 4
  /// gigabytes).
  S_GB_ZEROFILL = 0x0cu, ///< GB ZEROFILL.
  /// S_INTERPOSING - Section with only pairs of function pointers for
  /// interposing.
  S_INTERPOSING = 0x0du, ///< Interposing.
  /// S_16BYTE_LITERALS - Section with only 16 byte literals.
  S_16BYTE_LITERALS = 0x0eu,
  /// S_DTRACE_DOF - Section contains DTrace Object Format.
  S_DTRACE_DOF = 0x0fu,
  /// S_LAZY_DYLIB_SYMBOL_POINTERS - Section with lazy symbol pointers to
  /// lazy loaded dylibs.
  S_LAZY_DYLIB_SYMBOL_POINTERS = 0x10u, ///< LAZY DYLIB SYMBOL POINTERS.
  /// S_THREAD_LOCAL_REGULAR - Thread local data section.
  S_THREAD_LOCAL_REGULAR = 0x11u,
  /// S_THREAD_LOCAL_ZEROFILL - Thread local zerofill section.
  S_THREAD_LOCAL_ZEROFILL = 0x12u,
  /// S_THREAD_LOCAL_VARIABLES - Section with thread local variable
  /// structure data.
  S_THREAD_LOCAL_VARIABLES = 0x13u, ///< THREAD LOCAL variables.
  /// S_THREAD_LOCAL_VARIABLE_POINTERS - Section with pointers to thread
  /// local structures.
  S_THREAD_LOCAL_VARIABLE_POINTERS = 0x14u, ///< THREAD LOCAL VARIABLE POINTERS.
  /// S_THREAD_LOCAL_INIT_FUNCTION_POINTERS - Section with thread local
  /// variable initialization pointers to functions.
  S_THREAD_LOCAL_INIT_FUNCTION_POINTERS = 0x15u, ///< THREAD LOCAL INIT FUNCTION POINTERS.
  /// S_INIT_FUNC_OFFSETS - Section with 32-bit offsets to initializer
  /// functions.
  S_INIT_FUNC_OFFSETS = 0x16u, ///< INIT FUNC OFFSETS.

  LAST_KNOWN_SECTION_TYPE = S_INIT_FUNC_OFFSETS ///< Last section type known to this header.
};

/// User and system section-attribute flags for section::flags.
enum : uint32_t {
  // Constant masks for the "flags[31:24]" field in llvm::MachO::section and
  // llvm::MachO::section_64 (mask "flags" with SECTION_ATTRIBUTES_USR)

  /// S_ATTR_PURE_INSTRUCTIONS - Section contains only true machine
  /// instructions.
  S_ATTR_PURE_INSTRUCTIONS = 0x80000000u, ///< Section contains only machine instructions.
  /// S_ATTR_NO_TOC - Section contains coalesced symbols that are not to be
  /// in a ranlib table of contents.
  S_ATTR_NO_TOC = 0x40000000u, ///< Coalesced symbols omitted from the TOC.
  /// S_ATTR_STRIP_STATIC_SYMS - Ok to strip static symbols in this section
  /// in files with the MY_DYLDLINK flag.
  S_ATTR_STRIP_STATIC_SYMS = 0x20000000u, ///< Ok to strip static symbols in this section.
  /// S_ATTR_NO_DEAD_STRIP - No dead stripping.
  S_ATTR_NO_DEAD_STRIP = 0x10000000u,
  /// S_ATTR_LIVE_SUPPORT - Blocks are live if they reference live blocks.
  S_ATTR_LIVE_SUPPORT = 0x08000000u,
  /// S_ATTR_SELF_MODIFYING_CODE - Used with i386 code stubs written on by
  /// dyld.
  S_ATTR_SELF_MODIFYING_CODE = 0x04000000u, ///< Self-modifying code (i386 stubs).
  /// S_ATTR_DEBUG - A debug section.
  S_ATTR_DEBUG = 0x02000000u,

  // Constant masks for the "flags[23:8]" field in llvm::MachO::section and
  // llvm::MachO::section_64 (mask "flags" with SECTION_ATTRIBUTES_SYS)

  /// S_ATTR_SOME_INSTRUCTIONS - Section contains some machine instructions.
  S_ATTR_SOME_INSTRUCTIONS = 0x00000400u,
  /// S_ATTR_EXT_RELOC - Section has external relocation entries.
  S_ATTR_EXT_RELOC = 0x00000200u,
  /// S_ATTR_LOC_RELOC - Section has local relocation entries.
  S_ATTR_LOC_RELOC = 0x00000100u,

  // Constant masks for the value of an indirect symbol in an indirect
  // symbol table
  INDIRECT_SYMBOL_LOCAL = 0x80000000u, ///< Indirect symbol is local.
  INDIRECT_SYMBOL_ABS = 0x40000000u ///< Indirect symbol is absolute.
};

/// Kinds of data-in-code entries (data_in_code_entry::kind).
enum DataRegionType {
  // Constants for the "kind" field in a data_in_code_entry structure
  DICE_KIND_DATA = 1u, ///< Data embedded in a code section.
  DICE_KIND_JUMP_TABLE8 = 2u, ///< 8-bit jump table.
  DICE_KIND_JUMP_TABLE16 = 3u, ///< 16-bit jump table.
  DICE_KIND_JUMP_TABLE32 = 4u, ///< 32-bit jump table.
  DICE_KIND_ABS_JUMP_TABLE32 = 5u ///< 32-bit absolute jump table.
};

/// Rebase fixup type encodings used in the dyld rebase stream.
enum RebaseType {
  REBASE_TYPE_POINTER = 1u, ///< Rebase a pointer.
  REBASE_TYPE_TEXT_ABSOLUTE32 = 2u, ///< Rebase a 32-bit absolute text value.
  REBASE_TYPE_TEXT_PCREL32 = 3u ///< Rebase a 32-bit PC-relative text value.
};

/// Masks that split a rebase opcode byte into opcode and immediate fields.
enum {
  REBASE_OPCODE_MASK = 0xF0u, ///< Mask for the opcode nibble.
  REBASE_IMMEDIATE_MASK = 0x0Fu ///< Mask for the immediate nibble.
};

/// Opcode values for the dyld rebase bytecode stream.
enum RebaseOpcode {
  REBASE_OPCODE_DONE = 0x00u, ///< End of the rebase opcode stream.
  REBASE_OPCODE_SET_TYPE_IMM = 0x10u, ///< Set rebase type from immediate.
  REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB = 0x20u, ///< Set segment and ULEB offset.
  REBASE_OPCODE_ADD_ADDR_ULEB = 0x30u, ///< Add a ULEB address delta.
  REBASE_OPCODE_ADD_ADDR_IMM_SCALED = 0x40u, ///< Add an immediate scaled address delta.
  REBASE_OPCODE_DO_REBASE_IMM_TIMES = 0x50u, ///< Perform immediate-count rebases.
  REBASE_OPCODE_DO_REBASE_ULEB_TIMES = 0x60u, ///< Perform ULEB-count rebases.
  REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB = 0x70u, ///< Rebase once then add a ULEB address.
  REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB = 0x80u ///< Rebase ULEB times skipping ULEB bytes.
};

/// Bind fixup type encodings used in the dyld bind stream.
enum BindType {
  BIND_TYPE_POINTER = 1u, ///< Bind a pointer.
  BIND_TYPE_TEXT_ABSOLUTE32 = 2u, ///< Bind a 32-bit absolute text value.
  BIND_TYPE_TEXT_PCREL32 = 3u ///< Bind a 32-bit PC-relative text value.
};

/// Special dylib ordinals used in dyld bind opcodes.
enum BindSpecialDylib {
  BIND_SPECIAL_DYLIB_SELF = 0, ///< Bind against the image itself.
  BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE = -1, ///< Bind against the main executable.
  BIND_SPECIAL_DYLIB_FLAT_LOOKUP = -2, ///< Flat-namespace symbol lookup.
  BIND_SPECIAL_DYLIB_WEAK_LOOKUP = -3 ///< Weak-symbol lookup across images.
};

/// Bind symbol flags and opcode/immediate masks for the dyld bind stream.
enum {
  BIND_SYMBOL_FLAGS_WEAK_IMPORT = 0x1u, ///< Imported symbol is weak.
  BIND_SYMBOL_FLAGS_NON_WEAK_DEFINITION = 0x8u, ///< Non-weak definition flag.

  BIND_OPCODE_MASK = 0xF0u, ///< Mask for the opcode nibble of a bind opcode byte.
  BIND_IMMEDIATE_MASK = 0x0Fu ///< Mask for the immediate nibble of a bind opcode byte.
};

/// Opcode values for the dyld bind bytecode stream.
enum BindOpcode {
  BIND_OPCODE_DONE = 0x00u, ///< End of the bind opcode stream.
  BIND_OPCODE_SET_DYLIB_ORDINAL_IMM = 0x10u, ///< Set dylib ordinal from immediate.
  BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB = 0x20u, ///< Set dylib ordinal from ULEB.
  BIND_OPCODE_SET_DYLIB_SPECIAL_IMM = 0x30u, ///< Set special dylib ordinal from immediate.
  BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM = 0x40u, ///< Set symbol name and flags.
  BIND_OPCODE_SET_TYPE_IMM = 0x50u, ///< Set bind type from immediate.
  BIND_OPCODE_SET_ADDEND_SLEB = 0x60u, ///< Set addend from SLEB.
  BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB = 0x70u, ///< Set segment and ULEB offset.
  BIND_OPCODE_ADD_ADDR_ULEB = 0x80u, ///< Add a ULEB address delta.
  BIND_OPCODE_DO_BIND = 0x90u, ///< Perform one bind.
  BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB = 0xA0u, ///< Bind once then add a ULEB address.
  BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED = 0xB0u, ///< Bind once then add scaled immediate.
  BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB = 0xC0u ///< Bind ULEB times skipping ULEB bytes.
};

/// Flag bits encoded in dyld export-trie symbol info.
enum {
  EXPORT_SYMBOL_FLAGS_KIND_MASK = 0x03u, ///< Mask for export symbol kind bits.
  EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION = 0x04u, ///< Exported symbol is a weak definition.
  EXPORT_SYMBOL_FLAGS_REEXPORT = 0x08u, ///< Exported symbol is a re-export.
  EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER = 0x10u ///< Exported symbol has stub and resolver.
};

/// Export-trie symbol kind values (kind field of export flags).
enum ExportSymbolKind {
  EXPORT_SYMBOL_FLAGS_KIND_REGULAR = 0x00u, ///< Regular exported symbol.
  EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL = 0x01u, ///< Thread-local exported symbol.
  EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE = 0x02u ///< Absolute exported symbol.
};

/// Bit masks for the n_type field in nlist / nlist_64.
enum {
  // Constant masks for the "n_type" field in llvm::MachO::nlist and
  // llvm::MachO::nlist_64
  N_STAB = 0xe0, ///< Mask indicating a stab debug entry.
  N_PEXT = 0x10, ///< Private external symbol bit.
  N_TYPE = 0x0e, ///< Mask for the symbol type bits.
  N_EXT = 0x01 ///< External symbol bit.
};

/// Symbol type codes for (n_type & N_TYPE) in nlist / nlist_64.
enum NListType : uint8_t {
  // Constants for the "n_type & N_TYPE" llvm::MachO::nlist and
  // llvm::MachO::nlist_64
  N_UNDF = 0x0u, ///< Undefined symbol.
  N_ABS = 0x2u, ///< Absolute symbol.
  N_SECT = 0xeu, ///< Symbol defined in a section.
  N_PBUD = 0xcu, ///< Prebound undefined symbol.
  N_INDR = 0xau ///< Indirect symbol.
};

/// Special section ordinal values for nlist::n_sect.
enum SectionOrdinal {
  // Constants for the "n_sect" field in llvm::MachO::nlist and
  // llvm::MachO::nlist_64
  NO_SECT = 0u, ///< Symbol is not in any section.
  MAX_SECT = 0xffu ///< Maximum legal section ordinal.
};

/// Reference-type and flag bits for the n_desc field in nlist / nlist_64.
enum {
  // Constant masks for the "n_desc" field in llvm::MachO::nlist and
  // llvm::MachO::nlist_64
  // The low 3 bits are the for the REFERENCE_TYPE.
  REFERENCE_TYPE = 0x7, ///< Mask for the reference-type bits in n_desc.
  REFERENCE_FLAG_UNDEFINED_NON_LAZY = 0, ///< Undefined non-lazy reference.
  REFERENCE_FLAG_UNDEFINED_LAZY = 1, ///< Undefined lazy reference.
  REFERENCE_FLAG_DEFINED = 2, ///< Defined reference.
  REFERENCE_FLAG_PRIVATE_DEFINED = 3, ///< Private defined reference.
  REFERENCE_FLAG_PRIVATE_UNDEFINED_NON_LAZY = 4, ///< Private undefined non-lazy reference.
  REFERENCE_FLAG_PRIVATE_UNDEFINED_LAZY = 5, ///< Private undefined lazy reference.
  // Flag bits (some overlap with the library ordinal bits).
  N_ARM_THUMB_DEF = 0x0008u, ///< Defined symbol is Thumb code on ARM.
  REFERENCED_DYNAMICALLY = 0x0010u, ///< Symbol must not be dead-stripped.
  N_NO_DEAD_STRIP = 0x0020u, ///< Do not dead-strip this symbol.
  N_WEAK_REF = 0x0040u, ///< Weak reference.
  N_WEAK_DEF = 0x0080u, ///< Weak definition.
  N_SYMBOL_RESOLVER = 0x0100u, ///< Symbol resolver function.
  N_ALT_ENTRY = 0x0200u, ///< Alternate entry point.
  N_COLD_FUNC = 0x0400u, ///< Cold function.
  // For undefined symbols coming from libraries, see GET_LIBRARY_ORDINAL()
  // as these are in the top 8 bits.
  SELF_LIBRARY_ORDINAL = 0x0, ///< Library ordinal for the image itself.
  MAX_LIBRARY_ORDINAL = 0xfd, ///< Maximum ordinary library ordinal.
  DYNAMIC_LOOKUP_ORDINAL = 0xfe, ///< Dynamic lookup library ordinal.
  EXECUTABLE_ORDINAL = 0xff ///< Main-executable library ordinal.
};

/// Stab debug symbol type codes when (n_type & N_STAB) != 0.
enum StabType {
  // Constant values for the "n_type" field in llvm::MachO::nlist and
  // llvm::MachO::nlist_64 when "(n_type & N_STAB) != 0"
  N_GSYM = 0x20u, ///< Global symbol stab.
  N_FNAME = 0x22u, ///< Procedure name (f77) stab.
  N_FUN = 0x24u, ///< Procedure stab.
  N_STSYM = 0x26u, ///< Static symbol stab.
  N_LCSYM = 0x28u, ///< Local common stab.
  N_BNSYM = 0x2Eu, ///< Begin nsect symbol stab.
  N_PC = 0x30u, ///< Pascal/C include stab.
  N_AST = 0x32u, ///< AST file path stab.
  N_OPT = 0x3Cu, ///< Compiler options stab.
  N_RSYM = 0x40u, ///< Register symbol stab.
  N_SLINE = 0x44u, ///< Source line stab.
  N_ENSYM = 0x4Eu, ///< End nsect symbol stab.
  N_SSYM = 0x60u, ///< Structure/union element stab.
  N_SO = 0x64u, ///< Source file name stab.
  N_OSO = 0x66u, ///< Object file name stab.
  N_LIB = 0x68u, ///< Shared library path stab.
  N_LSYM = 0x80u, ///< Local symbol stab.
  N_BINCL = 0x82u, ///< Begin include file stab.
  N_SOL = 0x84u, ///< Included source file name stab.
  N_PARAMS = 0x86u, ///< Compiler parameters stab.
  N_VERSION = 0x88u, ///< Compiler version stab.
  N_OLEVEL = 0x8Au, ///< Compiler -O level stab.
  N_PSYM = 0xA0u, ///< Parameter stab.
  N_EINCL = 0xA2u, ///< End include file stab.
  N_ENTRY = 0xA4u, ///< Alternate entry stab.
  N_LBRAC = 0xC0u, ///< Left bracket stab.
  N_EXCL = 0xC2u, ///< Deleted include file stab.
  N_RBRAC = 0xE0u, ///< Right bracket stab.
  N_BCOMM = 0xE2u, ///< Begin common stab.
  N_ECOMM = 0xE4u, ///< End common stab.
  N_ECOML = 0xE8u, ///< End common (local) stab.
  N_LENG = 0xFEu ///< Length stab.
};

/// Special relocation_info::r_symbolnum and r_address flag values.
enum : uint32_t {
  // Constant values for the r_symbolnum field in an
  // llvm::MachO::relocation_info structure when r_extern is 0.
  R_ABS = 0, ///< Absolute relocation when r_extern is 0.

  // Constant bits for the r_address field in an
  // llvm::MachO::relocation_info structure.
  R_SCATTERED = 0x80000000 ///< Bit set in r_address for scattered relocations.
};

/// Relocation type codes for relocation_info::r_type by architecture.
enum RelocationInfoType {
  // Constant values for the r_type field in an
  // llvm::MachO::relocation_info or llvm::MachO::scattered_relocation_info
  // structure.
  GENERIC_RELOC_INVALID = 0xff, ///< Invalid / unknown generic relocation type.
  GENERIC_RELOC_VANILLA = 0, ///< Generic vanilla relocation.
  GENERIC_RELOC_PAIR = 1, ///< Generic paired relocation.
  GENERIC_RELOC_SECTDIFF = 2, ///< Generic section-difference relocation.
  GENERIC_RELOC_PB_LA_PTR = 3, ///< Generic prebound lazy-pointer relocation.
  GENERIC_RELOC_LOCAL_SECTDIFF = 4, ///< Generic local section-difference relocation.
  GENERIC_RELOC_TLV = 5, ///< Generic thread-local variable relocation.

  // Constant values for the r_type field in a PowerPC architecture
  // llvm::MachO::relocation_info or llvm::MachO::scattered_relocation_info
  // structure.
  PPC_RELOC_VANILLA = GENERIC_RELOC_VANILLA, ///< VANILLA.
  PPC_RELOC_PAIR = GENERIC_RELOC_PAIR, ///< PAIR.
  PPC_RELOC_BR14 = 2, ///< BR14.
  PPC_RELOC_BR24 = 3, ///< BR24.
  PPC_RELOC_HI16 = 4, ///< HI16.
  PPC_RELOC_LO16 = 5, ///< LO16.
  PPC_RELOC_HA16 = 6, ///< HA16.
  PPC_RELOC_LO14 = 7, ///< LO14.
  PPC_RELOC_SECTDIFF = 8, ///< SECTDIFF.
  PPC_RELOC_PB_LA_PTR = 9, ///< PB LA PTR.
  PPC_RELOC_HI16_SECTDIFF = 10, ///< HI16 SECTDIFF.
  PPC_RELOC_LO16_SECTDIFF = 11, ///< LO16 SECTDIFF.
  PPC_RELOC_HA16_SECTDIFF = 12, ///< HA16 SECTDIFF.
  PPC_RELOC_JBSR = 13, ///< JBSR.
  PPC_RELOC_LO14_SECTDIFF = 14, ///< LO14 SECTDIFF.
  PPC_RELOC_LOCAL_SECTDIFF = 15, ///< LOCAL SECTDIFF.

  // Constant values for the r_type field in an ARM architecture
  // llvm::MachO::relocation_info or llvm::MachO::scattered_relocation_info
  // structure.
  ARM_RELOC_VANILLA = GENERIC_RELOC_VANILLA, ///< VANILLA.
  ARM_RELOC_PAIR = GENERIC_RELOC_PAIR, ///< PAIR.
  ARM_RELOC_SECTDIFF = GENERIC_RELOC_SECTDIFF, ///< SECTDIFF.
  ARM_RELOC_LOCAL_SECTDIFF = 3, ///< LOCAL SECTDIFF.
  ARM_RELOC_PB_LA_PTR = 4, ///< PB LA PTR.
  ARM_RELOC_BR24 = 5, ///< BR24.
  ARM_THUMB_RELOC_BR22 = 6, ///< BR22.
  ARM_THUMB_32BIT_BRANCH = 7, ///< obsolete
  ARM_RELOC_HALF = 8, ///< HALF.
  ARM_RELOC_HALF_SECTDIFF = 9, ///< HALF SECTDIFF.

  // Constant values for the r_type field in an ARM64 architecture
  // llvm::MachO::relocation_info or llvm::MachO::scattered_relocation_info
  // structure.

  // For pointers.
  ARM64_RELOC_UNSIGNED = 0, ///< ARM64 absolute/unsigned relocation.
  // Must be followed by an ARM64_RELOC_UNSIGNED
  ARM64_RELOC_SUBTRACTOR = 1, ///< ARM64 subtractor relocation.
  // A B/BL instruction with 26-bit displacement.
  ARM64_RELOC_BRANCH26 = 2, ///< ARM64 26-bit branch relocation.
  // PC-rel distance to page of target.
  ARM64_RELOC_PAGE21 = 3, ///< ARM64 page-distance relocation.
  // Offset within page, scaled by r_length.
  ARM64_RELOC_PAGEOFF12 = 4, ///< ARM64 page-offset relocation.
  // PC-rel distance to page of GOT slot.
  ARM64_RELOC_GOT_LOAD_PAGE21 = 5, ///< ARM64 GOT page-distance relocation.
  // Offset within page of GOT slot, scaled by r_length.
  ARM64_RELOC_GOT_LOAD_PAGEOFF12 = 6, ///< ARM64 GOT page-offset relocation.
  // For pointers to GOT slots.
  ARM64_RELOC_POINTER_TO_GOT = 7, ///< ARM64 pointer-to-GOT relocation.
  // PC-rel distance to page of TLVP slot.
  ARM64_RELOC_TLVP_LOAD_PAGE21 = 8, ///< ARM64 TLV page-distance relocation.
  // Offset within page of TLVP slot, scaled by r_length.
  ARM64_RELOC_TLVP_LOAD_PAGEOFF12 = 9, ///< ARM64 TLV page-offset relocation.
  // Must be followed by ARM64_RELOC_PAGE21 or ARM64_RELOC_PAGEOFF12.
  ARM64_RELOC_ADDEND = 10, ///< ARM64 addend relocation.
  // An authenticated pointer.
  ARM64_RELOC_AUTHENTICATED_POINTER = 11, ///< ARM64 authenticated pointer relocation.

  // For pointers. For example, for a .word directive in assembly
  // representing a memory location where data is stored:
  //      .word: _bar
  RISCV_RELOC_UNSIGNED = 0, ///< RISC-V absolute/unsigned relocation.
  // Subtractor operand. Must be followed by a RISCV_RELOC_UNSIGNED,
  // which is the pointer from which to subtract the subtractor. For
  // example:
  //
  //          .global _a
  //          .global _b
  //    _a: ...
  //    _b: ...
  //
  //    .data_region
  //    .word _a - _b
  //    .end_data_region
  RISCV_RELOC_SUBTRACTOR = 1, ///< RISC-V subtractor relocation.
  // A jal/j instruction with 21-bit displacement. For example, a
  // function call:
  //
  //    _foo:
  //          jal _bar
  RISCV_RELOC_BRANCH21 = 2, ///< RISC-V 21-bit branch relocation.
  // High 20 bits of pointer. r_pcrel=1 means this is paired with an
  // AUIPC.  r_pcrel=0 means this is paired with a LUI.
  RISCV_RELOC_HI20 = 3, ///< RISC-V high-20-bits relocation.
  // An ADDI or LW/SW instruction that requires low 12 bits to be
  // adjusted. r_pcrel=1 means this is paired with an AUIPC.
  // r_pcrel=0 means this is paired with a LUI (llvm currently does
  // not support no-PIC). Note: the compiler places the distance to
  // the paired AUIPC in the imm12 (e.g. if previous instruction is
  // the AUIPC, the imm12 is -4 or 0xFFC).  NOTE: this mean that the
  // separation between hi/lo has to fit in (signed) 12 bits. Beyond
  // 12-bits, the pc-relative offset is not inlined in the imm12, but
  // it is instead stored in the 24-bits of a RISCV_RELOC_ADDEND
  // record.
  RISCV_RELOC_LO12 = 4, ///< RISC-V low-12-bits relocation.
  // High 20 bits of GOT slot. r_pcrel=1 means this is paired with an
  // AUIPC.  r_pcrel=0 means this is paired with a LUI (the compiler
  // may emit a @got reloc for a reference to anything outside the
  // translation unit, then the linker elides the @got if the target
  // is in range).
  RISCV_RELOC_GOT_HI20 = 5, ///< RISC-V GOT high-20-bits relocation.
  // Low 12 bits of GOT slot. r_pcrel=1 means this is paired with an
  // AUIPC.  r_pcrel=0 means this is paired with a LUI.
  RISCV_RELOC_GOT_LO12 = 6, ///< RISC-V GOT low-12-bits relocation.
  // For pointers to GOT slots. To be used by C++ exception handling,
  // in the Language Specific Data Area (LSDA, __gcc_except_tab
  // section). Not currently used, but added for completeness.
  RISCV_RELOC_POINTER_TO_GOT = 7, ///< RISC-V pointer-to-GOT relocation.
  // Adds a static offset to a relocation.  Must be followed by
  // RISCV_RELOC_PCREL_HI, RISCV_RELOC_BRANCH21 or
  // RISCV_RELOC_LO12. For example, the 16 bytes offset in:
  //
  //         auipc a0, %pcrel_hi(var+16)
  RISCV_RELOC_ADDEND = 8, ///< RISC-V addend relocation.

  // Constant values for the r_type field in an x86_64 architecture
  // llvm::MachO::relocation_info or llvm::MachO::scattered_relocation_info
  // structure
  X86_64_RELOC_UNSIGNED = 0, ///< x86_64 absolute/unsigned relocation.
  X86_64_RELOC_SIGNED = 1, ///< x86_64 signed relocation.
  X86_64_RELOC_BRANCH = 2, ///< x86_64 branch relocation.
  X86_64_RELOC_GOT_LOAD = 3, ///< x86_64 GOT-load relocation.
  X86_64_RELOC_GOT = 4, ///< x86_64 GOT relocation.
  X86_64_RELOC_SUBTRACTOR = 5, ///< x86_64 subtractor relocation.
  X86_64_RELOC_SIGNED_1 = 6, ///< x86_64 signed relocation with -1 addend.
  X86_64_RELOC_SIGNED_2 = 7, ///< x86_64 signed relocation with -2 addend.
  X86_64_RELOC_SIGNED_4 = 8, ///< x86_64 signed relocation with -4 addend.
  X86_64_RELOC_TLV = 9 ///< x86_64 thread-local variable relocation.
};

// Values for segment_command.initprot.
// From <mach/vm_prot.h>
/// Virtual-memory protection bits for segment_command::initprot / maxprot.
enum {
  VM_PROT_READ = 0x1, ///< Read permission.
  VM_PROT_WRITE = 0x2, ///< Write permission.
  VM_PROT_EXECUTE = 0x4 ///< Execute permission.
};

// Values for platform field in build_version_command.
/// Platform identifiers for build_version_command::platform.
enum PlatformType {
#define PLATFORM(platform, id, name, build_name, target, tapi_target,          \
                 marketing)                                                    \
  PLATFORM_##platform = id,
#include "MachO.def"
};

// Values for tools enum in build_tool_version.
/// Tool identifiers for build_tool_version::tool.
enum {
  TOOL_CLANG = 1, ///< Clang compiler.
  TOOL_SWIFT = 2, ///< Swift compiler.
  TOOL_LD = 3, ///< ld64 linker.
  TOOL_LLD = 4 ///< LLD linker.
};

// Structs from <mach-o/loader.h>

/// 32-bit Mach-O object file header.
struct mach_header {
  uint32_t magic; ///< Magic number identifying the structure.
  uint32_t cputype; ///< CPU type (CPU_TYPE_*).
  uint32_t cpusubtype; ///< CPU subtype (CPU_SUBTYPE_*).
  uint32_t filetype; ///< File type (MH_* HeaderFileType).
  uint32_t ncmds; ///< Number of load commands that follow.
  uint32_t sizeofcmds; ///< Total size in bytes of all load commands.
  uint32_t flags; ///< Flag bits (context-dependent).
};

/// 64-bit Mach-O object file header.
struct mach_header_64 {
  uint32_t magic; ///< Magic number identifying the structure.
  uint32_t cputype; ///< CPU type (CPU_TYPE_*).
  uint32_t cpusubtype; ///< CPU subtype (CPU_SUBTYPE_*).
  uint32_t filetype; ///< File type (MH_* HeaderFileType).
  uint32_t ncmds; ///< Number of load commands that follow.
  uint32_t sizeofcmds; ///< Total size in bytes of all load commands.
  uint32_t flags; ///< Flag bits (context-dependent).
  uint32_t reserved; ///< Reserved; must be zero.
};

/// Base header shared by every Mach-O load command.
struct load_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
};

/// 32-bit segment load command (LC_SEGMENT).
struct segment_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  char segname[16]; ///< Segment name (16 bytes, not necessarily NUL-terminated).
  uint32_t vmaddr; ///< Starting virtual memory address of the segment or entry.
  uint32_t vmsize; ///< Size in bytes of the segment in virtual memory.
  uint32_t fileoff; ///< File offset of the segment or entry data.
  uint32_t filesize; ///< Number of bytes mapped from the file for this segment.
  uint32_t maxprot; ///< Maximum virtual-memory protections (VM_PROT_*).
  uint32_t initprot; ///< Initial virtual-memory protections (VM_PROT_*).
  uint32_t nsects; ///< Number of section headers that follow this command.
  uint32_t flags; ///< Flag bits (context-dependent).
};

/// 64-bit segment load command (LC_SEGMENT_64).
struct segment_command_64 {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  char segname[16]; ///< Segment name (16 bytes, not necessarily NUL-terminated).
  uint64_t vmaddr; ///< Starting virtual memory address of the segment or entry.
  uint64_t vmsize; ///< Size in bytes of the segment in virtual memory.
  uint64_t fileoff; ///< File offset of the segment or entry data.
  uint64_t filesize; ///< Number of bytes mapped from the file for this segment.
  uint32_t maxprot; ///< Maximum virtual-memory protections (VM_PROT_*).
  uint32_t initprot; ///< Initial virtual-memory protections (VM_PROT_*).
  uint32_t nsects; ///< Number of section headers that follow this command.
  uint32_t flags; ///< Flag bits (context-dependent).
};

/// 32-bit section header within a segment_command.
struct section {
  char sectname[16]; ///< Section name (16 bytes, not necessarily NUL-terminated).
  char segname[16]; ///< Segment name (16 bytes, not necessarily NUL-terminated).
  uint32_t addr; ///< Virtual address of the section.
  uint32_t size; ///< Size in bytes of the section or blob.
  uint32_t offset; ///< File offset of the associated data.
  uint32_t align; ///< Section alignment as a power of two.
  uint32_t reloff; ///< File offset of the section's relocation entries.
  uint32_t nreloc; ///< Number of relocation entries.
  uint32_t flags; ///< Flag bits (context-dependent).
  uint32_t reserved1; ///< Reserved; must be zero.
  uint32_t reserved2; ///< Reserved; must be zero.
};

/// 64-bit section header within a segment_command_64.
struct section_64 {
  char sectname[16]; ///< Section name (16 bytes, not necessarily NUL-terminated).
  char segname[16]; ///< Segment name (16 bytes, not necessarily NUL-terminated).
  uint64_t addr; ///< Virtual address of the section.
  uint64_t size; ///< Size in bytes of the section or blob.
  uint32_t offset; ///< File offset of the associated data.
  uint32_t align; ///< Section alignment as a power of two.
  uint32_t reloff; ///< File offset of the section's relocation entries.
  uint32_t nreloc; ///< Number of relocation entries.
  uint32_t flags; ///< Flag bits (context-dependent).
  uint32_t reserved1; ///< Reserved; must be zero.
  uint32_t reserved2; ///< Reserved; must be zero.
  uint32_t reserved3; ///< Reserved; must be zero.
};

/// Return true if \p type is a zerofill section that has no file content.
///
/// \param type Section type byte (SECTION_TYPE bits).
/// \return True if \p type is a zerofill (virtual) section.
inline bool isVirtualSection(uint8_t type) {
  return (type == MachO::S_ZEROFILL || type == MachO::S_GB_ZEROFILL ||
          type == MachO::S_THREAD_LOCAL_ZEROFILL);
}

/// Fixed VM shared library identification (obsolete).
struct fvmlib {
  uint32_t name; ///< Offset of a name string within the load command.
  uint32_t minor_version; ///< Minor version number of the fixed VM library.
  uint32_t header_addr; ///< Virtual address where the library's header is loaded.
};

// The fvmlib_command is obsolete and no longer supported.
/// Load command for a fixed VM shared library (obsolete).
struct fvmlib_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
/// Fixed VM shared library identification (obsolete).
  struct fvmlib fvmlib;
};

/// Shared library identification used by dylib_command.
struct dylib {
  uint32_t name; ///< Offset of a name string within the load command.
  uint32_t timestamp; ///< Library build timestamp.
  uint32_t current_version; ///< Current library version.
  uint32_t compatibility_version; ///< Oldest compatible library version.
};

/// Load command describing a dynamically linked shared library.
struct dylib_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
/// Shared library identification used by dylib_command.
  struct dylib dylib;
};

/// Load command naming the umbrella framework for a subframework.
struct sub_framework_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t umbrella; ///< Offset of the umbrella framework name string.
};

/// Load command naming an allowed client of a subframework.
struct sub_client_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t client; ///< Offset of the allowed client name string.
};

/// Load command naming a sub-umbrella within an umbrella framework.
struct sub_umbrella_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t sub_umbrella; ///< Offset of the sub-umbrella name string.
};

/// Load command naming a sub-library within an umbrella library.
struct sub_library_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t sub_library; ///< Offset of the sub-library name string.
};

// The prebound_dylib_command is obsolete and no longer supported.
/// Load command for a prebound dylib (obsolete).
struct prebound_dylib_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t name; ///< Offset of a name string within the load command.
  uint32_t nmodules; ///< Number of modules in the prebound dylib.
  uint32_t linked_modules; ///< Offset of the bit vector of linked modules.
};

/// Load command naming the dynamic linker to use.
struct dylinker_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t name; ///< Offset of a name string within the load command.
};

/// Load command carrying thread state (LC_THREAD / LC_UNIXTHREAD).
struct thread_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
};

/// 32-bit load command describing module initialization routines.
struct routines_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t init_address; ///< Virtual address of the initialization routine.
  uint32_t init_module; ///< Index of the module containing the initialization routine.
  uint32_t reserved1; ///< Reserved; must be zero.
  uint32_t reserved2; ///< Reserved; must be zero.
  uint32_t reserved3; ///< Reserved; must be zero.
  uint32_t reserved4; ///< Reserved; must be zero.
  uint32_t reserved5; ///< Reserved; must be zero.
  uint32_t reserved6; ///< Reserved; must be zero.
};

/// 64-bit load command describing module initialization routines.
struct routines_command_64 {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint64_t init_address; ///< Virtual address of the initialization routine.
  uint64_t init_module; ///< Index of the module containing the initialization routine.
  uint64_t reserved1; ///< Reserved; must be zero.
  uint64_t reserved2; ///< Reserved; must be zero.
  uint64_t reserved3; ///< Reserved; must be zero.
  uint64_t reserved4; ///< Reserved; must be zero.
  uint64_t reserved5; ///< Reserved; must be zero.
  uint64_t reserved6; ///< Reserved; must be zero.
};

/// Load command locating the symbol table and string table.
struct symtab_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t symoff; ///< File offset of the symbol table.
  uint32_t nsyms; ///< Number of symbol table entries.
  uint32_t stroff; ///< File offset of the string table.
  uint32_t strsize; ///< Size in bytes of the string table.
};

/// Load command describing the dynamic symbol table layout.
struct dysymtab_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t ilocalsym; ///< Index of the first local symbol.
  uint32_t nlocalsym; ///< Number of local symbols.
  uint32_t iextdefsym; ///< Index of the first externally defined symbol.
  uint32_t nextdefsym; ///< Number of externally defined symbols.
  uint32_t iundefsym; ///< Index of the first undefined symbol.
  uint32_t nundefsym; ///< Number of undefined symbols.
  uint32_t tocoff; ///< File offset of the table of contents.
  uint32_t ntoc; ///< Number of table-of-contents entries.
  uint32_t modtaboff; ///< File offset of the module table.
  uint32_t nmodtab; ///< Number of module table entries.
  uint32_t extrefsymoff; ///< File offset of the external reference table.
  uint32_t nextrefsyms; ///< Number of external reference table entries.
  uint32_t indirectsymoff; ///< File offset of the indirect symbol table.
  uint32_t nindirectsyms; ///< Number of indirect symbol table entries.
  uint32_t extreloff; ///< File offset of the external relocation entries.
  uint32_t nextrel; ///< Number of external relocation entries.
  uint32_t locreloff; ///< File offset of the local relocation entries.
  uint32_t nlocrel; ///< Number of local relocation entries.
};

/// Table-of-contents entry mapping a symbol to a module.
struct dylib_table_of_contents {
  uint32_t symbol_index; ///< Index into the symbol table.
  uint32_t module_index; ///< Index into the module table.
};

/// 32-bit module table entry for a dynamic shared library.
struct dylib_module {
  uint32_t module_name; ///< Index into the string table for the module name.
  uint32_t iextdefsym; ///< Index of the first externally defined symbol.
  uint32_t nextdefsym; ///< Number of externally defined symbols.
  uint32_t irefsym; ///< Index into the external reference table.
  uint32_t nrefsym; ///< Number of external references for this module.
  uint32_t ilocalsym; ///< Index of the first local symbol.
  uint32_t nlocalsym; ///< Number of local symbols.
  uint32_t iextrel; ///< Index into the external relocation table.
  uint32_t nextrel; ///< Number of external relocation entries.
  uint32_t iinit_iterm; ///< Packed indices of module init/term sections.
  uint32_t ninit_nterm; ///< Packed counts of module init/term sections.
  uint32_t objc_module_info_addr; ///< Virtual address of Objective-C module info.
  uint32_t objc_module_info_size; ///< Size of Objective-C module info.
};

/// 64-bit module table entry for a dynamic shared library.
struct dylib_module_64 {
  uint32_t module_name; ///< Index into the string table for the module name.
  uint32_t iextdefsym; ///< Index of the first externally defined symbol.
  uint32_t nextdefsym; ///< Number of externally defined symbols.
  uint32_t irefsym; ///< Index into the external reference table.
  uint32_t nrefsym; ///< Number of external references for this module.
  uint32_t ilocalsym; ///< Index of the first local symbol.
  uint32_t nlocalsym; ///< Number of local symbols.
  uint32_t iextrel; ///< Index into the external relocation table.
  uint32_t nextrel; ///< Number of external relocation entries.
  uint32_t iinit_iterm; ///< Packed indices of module init/term sections.
  uint32_t ninit_nterm; ///< Packed counts of module init/term sections.
  uint32_t objc_module_info_size; ///< Size of Objective-C module info.
  uint64_t objc_module_info_addr; ///< Virtual address of Objective-C module info.
};

/// External reference entry describing a symbol used by a module.
struct dylib_reference {
  uint32_t isym : 24; ///< Symbol-table index of the referenced symbol.
  uint32_t flags : 8; ///< Reference-type flags for this symbol.
};

// The twolevel_hints_command is obsolete and no longer supported.
/// Load command locating the two-level namespace hints table.
struct twolevel_hints_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t offset; ///< File offset of the associated data.
  uint32_t nhints; ///< Number of two-level hints.
};

// The twolevel_hints_command is obsolete and no longer supported.
/// Two-level namespace hint for a single undefined symbol.
struct twolevel_hint {
  uint32_t isub_image : 8; ///< Sub-image index within the two-level library.
  uint32_t itoc : 24; ///< Table-of-contents index within that sub-image.
};

// The prebind_cksum_command is obsolete and no longer supported.
/// Load command holding the prebinding checksum (obsolete).
struct prebind_cksum_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t cksum; ///< Prebinding checksum value.
};

/// Load command embedding a 128-bit UUID for the binary.
struct uuid_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint8_t uuid[16]; ///< 128-bit UUID of the binary.
};

/// Load command adding a runtime search path for dylibs.
struct rpath_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t path; ///< Offset of the runtime search-path string.
};

/// Load command pointing at a blob of data in __LINKEDIT.
struct linkedit_data_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t dataoff; ///< File offset of the __LINKEDIT data blob.
  uint32_t datasize; ///< Size in bytes of the __LINKEDIT data blob.
};

/// One data-in-code range describing non-instruction bytes in text.
struct data_in_code_entry {
  uint32_t offset; ///< File offset of the associated data.
  uint16_t length; ///< Length in bytes of the region or blob.
  uint16_t kind; ///< Kind of data-in-code region (DICE_KIND_*).
};

/// Load command recording the source version used to build the binary.
struct source_version_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint64_t version; ///< Encoded version number.
};

/// 32-bit load command describing an encrypted segment range.
struct encryption_info_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t cryptoff; ///< File offset of the encrypted range.
  uint32_t cryptsize; ///< Size in bytes of the encrypted range.
  uint32_t cryptid; ///< Encryption system ID (0 = not encrypted).
};

/// 64-bit load command describing an encrypted segment range.
struct encryption_info_command_64 {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t cryptoff; ///< File offset of the encrypted range.
  uint32_t cryptsize; ///< Size in bytes of the encrypted range.
  uint32_t cryptid; ///< Encryption system ID (0 = not encrypted).
  uint32_t pad; ///< Padding to maintain alignment.
};

/// Load command recording the minimum OS version and SDK.
struct version_min_command {
  uint32_t cmd; ///< LC_VERSION_MIN_MACOSX or
                    // LC_VERSION_MIN_IPHONEOS
  uint32_t cmdsize; ///< sizeof(struct version_min_command)
  uint32_t version; ///< X.Y.Z is encoded in nibbles xxxx.yy.zz
  uint32_t sdk; ///< X.Y.Z is encoded in nibbles xxxx.yy.zz
};

/// Load command describing an arbitrary named note blob.
struct note_command {
  uint32_t cmd; ///< LC_NOTE
  uint32_t cmdsize; ///< sizeof(struct note_command)
  char data_owner[16]; ///< owner name for this LC_NOTE
  uint64_t offset; ///< file offset of this data
  uint64_t size; ///< length of data region
};

/// One tool/version pair following a build_version_command.
struct build_tool_version {
  uint32_t tool; ///< enum for the tool
  uint32_t version; ///< version of the tool
};

/// Load command recording platform, OS, SDK, and tool versions.
struct build_version_command {
  uint32_t cmd; ///< LC_BUILD_VERSION
  uint32_t cmdsize; ///< sizeof(struct build_version_command) +
                     // ntools * sizeof(struct build_tool_version)
  uint32_t platform; ///< platform
  uint32_t minos; ///< X.Y.Z is encoded in nibbles xxxx.yy.zz
  uint32_t sdk; ///< X.Y.Z is encoded in nibbles xxxx.yy.zz
  uint32_t ntools; ///< number of tool entries following this
};

/// Load command embedding a target triple string.
struct target_triple_command {
  uint32_t cmd; ///< LC_TARGET_TRIPLE
  uint32_t cmdsize; ///< including string
  uint32_t triple; ///< target triple string
};

/// Load command embedding an environment variable for dyld.
struct dyld_env_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t name; ///< Offset of a name string within the load command.
};

/// Load command locating dyld rebase/bind/export info streams.
struct dyld_info_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t rebase_off; ///< File offset of the rebase opcodes.
  uint32_t rebase_size; ///< Size in bytes of the rebase opcodes.
  uint32_t bind_off; ///< File offset of the bind opcodes.
  uint32_t bind_size; ///< Size in bytes of the bind opcodes.
  uint32_t weak_bind_off; ///< File offset of the weak-bind opcodes.
  uint32_t weak_bind_size; ///< Size in bytes of the weak-bind opcodes.
  uint32_t lazy_bind_off; ///< File offset of the lazy-bind opcodes.
  uint32_t lazy_bind_size; ///< Size in bytes of the lazy-bind opcodes.
  uint32_t export_off; ///< File offset of the export trie.
  uint32_t export_size; ///< Size in bytes of the export trie.
};

/// Load command carrying null-terminated linker option strings.
struct linker_option_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t count; ///< Number of entries that follow.
};

/// Union embedding a load-command string as a file offset.
union lc_str {
  uint32_t offset; ///< Offset of the string from the start of the load command.
};

/// Load command describing one entry in an MH_FILESET binary.
struct fileset_entry_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint64_t vmaddr; ///< Starting virtual memory address of the segment or entry.
  uint64_t fileoff; ///< File offset of the segment or entry data.
  union lc_str entry_id; ///< Identifier string for this fileset entry.
  uint32_t reserved; ///< Reserved; must be zero.
};

// The symseg_command is obsolete and no longer supported.
/// Load command locating a GNU-style symbol segment (obsolete).
struct symseg_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t offset; ///< File offset of the associated data.
  uint32_t size; ///< Size in bytes of the section or blob.
};

// The ident_command is obsolete and no longer supported.
/// Load command holding free-form identification strings (obsolete).
struct ident_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
};

// The fvmfile_command is obsolete and no longer supported.
/// Load command naming a file loaded at a fixed virtual address (obsolete).
struct fvmfile_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint32_t name; ///< Offset of a name string within the load command.
  uint32_t header_addr; ///< Virtual address where the library's header is loaded.
};

/// 32-bit thread-local variable descriptor.
struct tlv_descriptor_32 {
  uint32_t thunk; ///< Address of the TLV runtime thunk.
  uint32_t key; ///< Key used by the TLV runtime.
  uint32_t offset; ///< File offset of the associated data.
};

/// 64-bit thread-local variable descriptor.
struct tlv_descriptor_64 {
  uint64_t thunk; ///< Address of the TLV runtime thunk.
  uint64_t key; ///< Key used by the TLV runtime.
  uint64_t offset; ///< File offset of the associated data.
};

/// Thread-local variable descriptor using pointer-sized fields.
struct tlv_descriptor {
  uintptr_t thunk; ///< Address of the TLV runtime thunk.
  uintptr_t key; ///< Key used by the TLV runtime.
  uintptr_t offset; ///< File offset of the associated data.
};

/// Load command giving the main entry point and stack size.
struct entry_point_command {
  uint32_t cmd; ///< Load command type (LC_*).
  uint32_t cmdsize; ///< Total size of this command in bytes.
  uint64_t entryoff; ///< File offset of the entry point.
  uint64_t stacksize; ///< Initial stack size (0 = default).
};

// Structs from <mach-o/fat.h>
/// Header of a fat (universal) Mach-O binary.
struct fat_header {
  uint32_t magic; ///< Magic number identifying the structure.
  uint32_t nfat_arch; ///< Number of fat_arch descriptors that follow.
};

/// One architecture slice descriptor in a 32-bit fat binary.
struct fat_arch {
  uint32_t cputype; ///< CPU type (CPU_TYPE_*).
  uint32_t cpusubtype; ///< CPU subtype (CPU_SUBTYPE_*).
  uint32_t offset; ///< File offset of the associated data.
  uint32_t size; ///< Size in bytes of the section or blob.
  uint32_t align; ///< Section alignment as a power of two.
};

/// One architecture slice descriptor in a 64-bit fat binary.
struct fat_arch_64 {
  uint32_t cputype; ///< CPU type (CPU_TYPE_*).
  uint32_t cpusubtype; ///< CPU subtype (CPU_SUBTYPE_*).
  uint64_t offset; ///< File offset of the associated data.
  uint64_t size; ///< Size in bytes of the section or blob.
  uint32_t align; ///< Section alignment as a power of two.
  uint32_t reserved; ///< Reserved; must be zero.
};

// Structs from <mach-o/reloc.h>
/// Standard Mach-O relocation entry.
struct relocation_info {
  int32_t r_address; ///< Section-relative address to be relocated.
  uint32_t r_symbolnum : 24; ///< Symbol table index or section ordinal.
  uint32_t r_pcrel : 1; ///< Nonzero if the relocation is PC-relative.
  uint32_t r_length : 2; ///< Relocation length encoding.
  uint32_t r_extern : 1; ///< Nonzero if r_symbolnum is a symbol table index.
  uint32_t r_type : 4; ///< Relocation type (RelocationInfoType).
};

/// Scattered Mach-O relocation entry (r_scattered bit set).
struct scattered_relocation_info {
#if defined(BYTE_ORDER) && defined(BIG_ENDIAN) && (BYTE_ORDER == BIG_ENDIAN)
  uint32_t r_scattered : 1; ///< Nonzero; marks this as a scattered relocation.
  uint32_t r_pcrel : 1; ///< Nonzero if the relocation is PC-relative.
  uint32_t r_length : 2; ///< Relocation length encoding.
  uint32_t r_type : 4; ///< Relocation type (RelocationInfoType).
  uint32_t r_address : 24; ///< Section-relative address to be relocated.
#else
  uint32_t r_address : 24; ///< Section-relative address to be relocated.
  uint32_t r_type : 4; ///< Relocation type (RelocationInfoType).
  uint32_t r_length : 2; ///< Relocation length encoding.
  uint32_t r_pcrel : 1; ///< Nonzero if the relocation is PC-relative.
  uint32_t r_scattered : 1; ///< Nonzero; marks this as a scattered relocation.
#endif
  int32_t r_value; ///< Absolute address of the relocatable expression value.
};

// Structs NOT from <mach-o/reloc.h>, but that make LLVM's life easier
/// Untyped two-word view of a relocation entry.
struct any_relocation_info {
  uint32_t r_word0; ///< First word of the relocation.
  uint32_t r_word1; ///< Second word of the relocation.
};

// Structs from <mach-o/nlist.h>
/// Shared prefix of 32- and 64-bit nlist entries without the value field.
struct nlist_base {
  uint32_t n_strx; ///< Index into the string table for the symbol name.
  uint8_t n_type; ///< Symbol type and stab bits (N_*).
  uint8_t n_sect; ///< Section ordinal (1-based), or NO_SECT.
  uint16_t n_desc; ///< Description flags, reference type, and library ordinal.
};

/// 32-bit symbol table entry.
struct nlist {
  uint32_t n_strx; ///< Index into the string table for the symbol name.
  uint8_t n_type; ///< Symbol type and stab bits (N_*).
  uint8_t n_sect; ///< Section ordinal (1-based), or NO_SECT.
  int16_t n_desc; ///< Description flags, reference type, and library ordinal.
  uint32_t n_value; ///< Symbol value (address, size, or indirect index).
};

/// 64-bit symbol table entry.
struct nlist_64 {
  uint32_t n_strx; ///< Index into the string table for the symbol name.
  uint8_t n_type; ///< Symbol type and stab bits (N_*).
  uint8_t n_sect; ///< Section ordinal (1-based), or NO_SECT.
  uint16_t n_desc; ///< Description flags, reference type, and library ordinal.
  uint64_t n_value; ///< Symbol value (address, size, or indirect index).
};

// Values for dyld_chained_fixups_header::imports_format.
/// Import-table entry formats for dyld chained fixups.
enum ChainedImportFormat {
  DYLD_CHAINED_IMPORT = 1, ///< Import entry without addend.
  DYLD_CHAINED_IMPORT_ADDEND = 2, ///< Import entry with a 32-bit addend.
  DYLD_CHAINED_IMPORT_ADDEND64 = 3, ///< Import entry with a 64-bit addend.
};

// Values for dyld_chained_fixups_header::symbols_format.
/// Symbol-string table compression formats for dyld chained fixups.
enum {
  DYLD_CHAINED_SYMBOL_UNCOMPRESSED = 0, ///< Uncompressed symbol string pool.
  DYLD_CHAINED_SYMBOL_ZLIB = 1, ///< Zlib-compressed symbol string pool.
};

// Values for dyld_chained_starts_in_segment::page_start.
/// Sentinel values for dyld_chained_starts_in_segment::page_start.
enum {
  DYLD_CHAINED_PTR_START_NONE = 0xFFFF, ///< Page has no chained fixups.
  DYLD_CHAINED_PTR_START_MULTI = 0x8000, ///< page which has multiple starts
  DYLD_CHAINED_PTR_START_LAST = 0x8000, ///< last chain_start for a given page
};

// Values for dyld_chained_starts_in_segment::pointer_format.
/// Chained pointer encoding formats for dyld_chained_starts_in_segment.
enum {
  DYLD_CHAINED_PTR_ARM64E = 1, ///< arm64e authenticated pointer format.
  DYLD_CHAINED_PTR_64 = 2, ///< 64-bit generic chained pointer format.
  DYLD_CHAINED_PTR_32 = 3, ///< 32-bit chained pointer format.
  DYLD_CHAINED_PTR_32_CACHE = 4, ///< 32-bit shared-cache chained pointer format.
  DYLD_CHAINED_PTR_32_FIRMWARE = 5, ///< 32-bit firmware chained pointer format.
  DYLD_CHAINED_PTR_64_OFFSET = 6, ///< 64-bit offset-based chained pointer format.
  DYLD_CHAINED_PTR_ARM64E_KERNEL = 7, ///< arm64e kernel chained pointer format.
  DYLD_CHAINED_PTR_64_KERNEL_CACHE = 8, ///< 64-bit kernel-cache chained pointer format.
  DYLD_CHAINED_PTR_ARM64E_USERLAND = 9, ///< arm64e userland chained pointer format.
  DYLD_CHAINED_PTR_ARM64E_FIRMWARE = 10, ///< arm64e firmware chained pointer format.
  DYLD_CHAINED_PTR_X86_64_KERNEL_CACHE = 11, ///< x86_64 kernel-cache chained pointer format.
  DYLD_CHAINED_PTR_ARM64E_USERLAND24 = 12, ///< arm64e userland 24-bit diversity format.
};

/// Header for dyld chained fixups (LC_DYLD_CHAINED_FIXUPS payload).
///
/// Pointed to by the LC_DYLD_CHAINED_FIXUPS load command; describes
/// starts, imports, and symbol-string offsets within the chain data.
struct dyld_chained_fixups_header {
  uint32_t fixups_version; ///< Chained fixups format version (currently 0).
  uint32_t starts_offset;  ///< Offset of dyld_chained_starts_in_image.
  uint32_t imports_offset; ///< Offset of imports table in chain_data.
  uint32_t symbols_offset; ///< Offset of symbol strings in chain_data.
  uint32_t imports_count;  ///< Number of imported symbol names.
  uint32_t imports_format; ///< DYLD_CHAINED_IMPORT*
  uint32_t symbols_format; ///< 0 => uncompressed, 1 => zlib compressed
};

/// Image-wide segment start offsets for dyld chained fixups.
///
/// Embedded in the LC_DYLD_CHAINED_FIXUPS payload. Each seg_info_offset
/// entry is the offset to that segment's dyld_chained_starts_in_segment
/// followed by a pool of per-segment start data.
struct dyld_chained_starts_in_image {
  uint32_t seg_count; ///< Number of segments described by seg_info_offset.
  uint32_t seg_info_offset[1]; ///< Offsets to each segment's dyld_chained_starts_in_segment.
};

/// Per-segment page-start table for dyld chained fixups.
struct dyld_chained_starts_in_segment {
  uint32_t size;              ///< Size of this, including chain_starts entries
  uint16_t page_size;         ///< Page size in bytes (0x1000 or 0x4000)
  uint16_t pointer_format;    ///< DYLD_CHAINED_PTR*
  uint64_t segment_offset;    ///< VM offset from the __TEXT segment
  uint32_t max_valid_pointer; ///< Values beyond this are not pointers on 32-bit
  uint16_t page_count;        ///< Length of the page_start array
  uint16_t page_start[1];     ///< Page offset of first fixup on each page, or
                              ///< DYLD_CHAINED_PTR_START_NONE if no fixups
};

// DYLD_CHAINED_IMPORT
/// Chained-fixups import entry without an addend.
struct dyld_chained_import {
  uint32_t lib_ordinal : 8; ///< Dylib ordinal of the imported symbol.
  uint32_t weak_import : 1; ///< Nonzero if the import is weak.
  uint32_t name_offset : 23; ///< Offset of the symbol name in the symbol pool.
};

// DYLD_CHAINED_IMPORT_ADDEND
/// Chained-fixups import entry with a 32-bit addend.
struct dyld_chained_import_addend {
  uint32_t lib_ordinal : 8; ///< Dylib ordinal of the imported symbol.
  uint32_t weak_import : 1; ///< Nonzero if the import is weak.
  uint32_t name_offset : 23; ///< Offset of the symbol name in the symbol pool.
  int32_t addend; ///< Addend applied to the bound or imported value.
};

// DYLD_CHAINED_IMPORT_ADDEND64
/// Chained-fixups import entry with a 64-bit addend.
struct dyld_chained_import_addend64 {
  uint64_t lib_ordinal : 16; ///< Dylib ordinal of the imported symbol.
  uint64_t weak_import : 1; ///< Nonzero if the import is weak.
  uint64_t reserved : 15; ///< Reserved; must be zero.
  uint64_t name_offset : 32; ///< Offset of the symbol name in the symbol pool.
  uint64_t addend; ///< Addend applied to the bound or imported value.
};

// The `bind` field (most significant bit) of the encoded fixup determines
// whether it is dyld_chained_ptr_64_bind or dyld_chained_ptr_64_rebase.

// DYLD_CHAINED_PTR_64/DYLD_CHAINED_PTR_64_OFFSET
/// 64-bit chained pointer encoding a bind fixup.
struct dyld_chained_ptr_64_bind {
  uint64_t ordinal : 24; ///< Import-table ordinal for a bind fixup.
  uint64_t addend : 8; ///< Addend applied to the bound or imported value.
  uint64_t reserved : 19; ///< Reserved; must be zero.
  uint64_t next : 12; ///< Offset in 4-byte units to the next chained fixup (0 = end).
  uint64_t bind : 1; ///< set to 1
};

// DYLD_CHAINED_PTR_64/DYLD_CHAINED_PTR_64_OFFSET
/// 64-bit chained pointer encoding a rebase fixup.
struct dyld_chained_ptr_64_rebase {
  uint64_t target : 36; ///< Rebase target value (vmoffset or absolute).
  uint64_t high8 : 8; ///< High 8 bits of a 64-bit rebase target.
  uint64_t reserved : 7; ///< Reserved; must be zero.
  uint64_t next : 12; ///< Offset in 4-byte units to the next chained fixup (0 = end).
  uint64_t bind : 1; ///< set to 0
};

// Byte order swapping functions for MachO structs

/// Byte-swap the fields of a fat_header.
/// \param mh Structure whose multi-byte fields are swapped in place.
inline void swapStruct(fat_header &mh) {
  sys::swapByteOrder(mh.magic);
  sys::swapByteOrder(mh.nfat_arch);
}

/// Byte-swap the fields of a fat_arch.
/// \param mh Structure whose multi-byte fields are swapped in place.
inline void swapStruct(fat_arch &mh) {
  sys::swapByteOrder(mh.cputype);
  sys::swapByteOrder(mh.cpusubtype);
  sys::swapByteOrder(mh.offset);
  sys::swapByteOrder(mh.size);
  sys::swapByteOrder(mh.align);
}

/// Byte-swap the fields of a fat_arch_64.
/// \param mh Structure whose multi-byte fields are swapped in place.
inline void swapStruct(fat_arch_64 &mh) {
  sys::swapByteOrder(mh.cputype);
  sys::swapByteOrder(mh.cpusubtype);
  sys::swapByteOrder(mh.offset);
  sys::swapByteOrder(mh.size);
  sys::swapByteOrder(mh.align);
  sys::swapByteOrder(mh.reserved);
}

/// Byte-swap the fields of a mach_header.
/// \param mh Structure whose multi-byte fields are swapped in place.
inline void swapStruct(mach_header &mh) {
  sys::swapByteOrder(mh.magic);
  sys::swapByteOrder(mh.cputype);
  sys::swapByteOrder(mh.cpusubtype);
  sys::swapByteOrder(mh.filetype);
  sys::swapByteOrder(mh.ncmds);
  sys::swapByteOrder(mh.sizeofcmds);
  sys::swapByteOrder(mh.flags);
}

/// Byte-swap the fields of a mach_header_64.
/// \param H Structure whose multi-byte fields are swapped in place.
inline void swapStruct(mach_header_64 &H) {
  sys::swapByteOrder(H.magic);
  sys::swapByteOrder(H.cputype);
  sys::swapByteOrder(H.cpusubtype);
  sys::swapByteOrder(H.filetype);
  sys::swapByteOrder(H.ncmds);
  sys::swapByteOrder(H.sizeofcmds);
  sys::swapByteOrder(H.flags);
  sys::swapByteOrder(H.reserved);
}

/// Byte-swap the fields of a load_command.
/// \param lc Structure whose multi-byte fields are swapped in place.
inline void swapStruct(load_command &lc) {
  sys::swapByteOrder(lc.cmd);
  sys::swapByteOrder(lc.cmdsize);
}

/// Byte-swap the fields of a symtab_command.
/// \param lc Structure whose multi-byte fields are swapped in place.
inline void swapStruct(symtab_command &lc) {
  sys::swapByteOrder(lc.cmd);
  sys::swapByteOrder(lc.cmdsize);
  sys::swapByteOrder(lc.symoff);
  sys::swapByteOrder(lc.nsyms);
  sys::swapByteOrder(lc.stroff);
  sys::swapByteOrder(lc.strsize);
}

/// Byte-swap the fields of a segment_command_64.
/// \param seg Structure whose multi-byte fields are swapped in place.
inline void swapStruct(segment_command_64 &seg) {
  sys::swapByteOrder(seg.cmd);
  sys::swapByteOrder(seg.cmdsize);
  sys::swapByteOrder(seg.vmaddr);
  sys::swapByteOrder(seg.vmsize);
  sys::swapByteOrder(seg.fileoff);
  sys::swapByteOrder(seg.filesize);
  sys::swapByteOrder(seg.maxprot);
  sys::swapByteOrder(seg.initprot);
  sys::swapByteOrder(seg.nsects);
  sys::swapByteOrder(seg.flags);
}

/// Byte-swap the fields of a segment_command.
/// \param seg Structure whose multi-byte fields are swapped in place.
inline void swapStruct(segment_command &seg) {
  sys::swapByteOrder(seg.cmd);
  sys::swapByteOrder(seg.cmdsize);
  sys::swapByteOrder(seg.vmaddr);
  sys::swapByteOrder(seg.vmsize);
  sys::swapByteOrder(seg.fileoff);
  sys::swapByteOrder(seg.filesize);
  sys::swapByteOrder(seg.maxprot);
  sys::swapByteOrder(seg.initprot);
  sys::swapByteOrder(seg.nsects);
  sys::swapByteOrder(seg.flags);
}

/// Byte-swap the fields of a section_64.
/// \param sect Structure whose multi-byte fields are swapped in place.
inline void swapStruct(section_64 &sect) {
  sys::swapByteOrder(sect.addr);
  sys::swapByteOrder(sect.size);
  sys::swapByteOrder(sect.offset);
  sys::swapByteOrder(sect.align);
  sys::swapByteOrder(sect.reloff);
  sys::swapByteOrder(sect.nreloc);
  sys::swapByteOrder(sect.flags);
  sys::swapByteOrder(sect.reserved1);
  sys::swapByteOrder(sect.reserved2);
}

/// Byte-swap the fields of a section.
/// \param sect Structure whose multi-byte fields are swapped in place.
inline void swapStruct(section &sect) {
  sys::swapByteOrder(sect.addr);
  sys::swapByteOrder(sect.size);
  sys::swapByteOrder(sect.offset);
  sys::swapByteOrder(sect.align);
  sys::swapByteOrder(sect.reloff);
  sys::swapByteOrder(sect.nreloc);
  sys::swapByteOrder(sect.flags);
  sys::swapByteOrder(sect.reserved1);
  sys::swapByteOrder(sect.reserved2);
}

/// Byte-swap the fields of a dyld_info_command.
/// \param info Structure whose multi-byte fields are swapped in place.
inline void swapStruct(dyld_info_command &info) {
  sys::swapByteOrder(info.cmd);
  sys::swapByteOrder(info.cmdsize);
  sys::swapByteOrder(info.rebase_off);
  sys::swapByteOrder(info.rebase_size);
  sys::swapByteOrder(info.bind_off);
  sys::swapByteOrder(info.bind_size);
  sys::swapByteOrder(info.weak_bind_off);
  sys::swapByteOrder(info.weak_bind_size);
  sys::swapByteOrder(info.lazy_bind_off);
  sys::swapByteOrder(info.lazy_bind_size);
  sys::swapByteOrder(info.export_off);
  sys::swapByteOrder(info.export_size);
}

/// Byte-swap the fields of a dylib_command.
/// \param d Structure whose multi-byte fields are swapped in place.
inline void swapStruct(dylib_command &d) {
  sys::swapByteOrder(d.cmd);
  sys::swapByteOrder(d.cmdsize);
  sys::swapByteOrder(d.dylib.name);
  sys::swapByteOrder(d.dylib.timestamp);
  sys::swapByteOrder(d.dylib.current_version);
  sys::swapByteOrder(d.dylib.compatibility_version);
}

/// Byte-swap the fields of a sub_framework_command.
/// \param s Structure whose multi-byte fields are swapped in place.
inline void swapStruct(sub_framework_command &s) {
  sys::swapByteOrder(s.cmd);
  sys::swapByteOrder(s.cmdsize);
  sys::swapByteOrder(s.umbrella);
}

/// Byte-swap the fields of a sub_umbrella_command.
/// \param s Structure whose multi-byte fields are swapped in place.
inline void swapStruct(sub_umbrella_command &s) {
  sys::swapByteOrder(s.cmd);
  sys::swapByteOrder(s.cmdsize);
  sys::swapByteOrder(s.sub_umbrella);
}

/// Byte-swap the fields of a sub_library_command.
/// \param s Structure whose multi-byte fields are swapped in place.
inline void swapStruct(sub_library_command &s) {
  sys::swapByteOrder(s.cmd);
  sys::swapByteOrder(s.cmdsize);
  sys::swapByteOrder(s.sub_library);
}

/// Byte-swap the fields of a sub_client_command.
/// \param s Structure whose multi-byte fields are swapped in place.
inline void swapStruct(sub_client_command &s) {
  sys::swapByteOrder(s.cmd);
  sys::swapByteOrder(s.cmdsize);
  sys::swapByteOrder(s.client);
}

/// Byte-swap the fields of a routines_command.
/// \param r Structure whose multi-byte fields are swapped in place.
inline void swapStruct(routines_command &r) {
  sys::swapByteOrder(r.cmd);
  sys::swapByteOrder(r.cmdsize);
  sys::swapByteOrder(r.init_address);
  sys::swapByteOrder(r.init_module);
  sys::swapByteOrder(r.reserved1);
  sys::swapByteOrder(r.reserved2);
  sys::swapByteOrder(r.reserved3);
  sys::swapByteOrder(r.reserved4);
  sys::swapByteOrder(r.reserved5);
  sys::swapByteOrder(r.reserved6);
}

/// Byte-swap the fields of a routines_command_64.
/// \param r Structure whose multi-byte fields are swapped in place.
inline void swapStruct(routines_command_64 &r) {
  sys::swapByteOrder(r.cmd);
  sys::swapByteOrder(r.cmdsize);
  sys::swapByteOrder(r.init_address);
  sys::swapByteOrder(r.init_module);
  sys::swapByteOrder(r.reserved1);
  sys::swapByteOrder(r.reserved2);
  sys::swapByteOrder(r.reserved3);
  sys::swapByteOrder(r.reserved4);
  sys::swapByteOrder(r.reserved5);
  sys::swapByteOrder(r.reserved6);
}

/// Byte-swap the fields of a thread_command.
/// \param t Structure whose multi-byte fields are swapped in place.
inline void swapStruct(thread_command &t) {
  sys::swapByteOrder(t.cmd);
  sys::swapByteOrder(t.cmdsize);
}

/// Byte-swap the fields of a dylinker_command.
/// \param d Structure whose multi-byte fields are swapped in place.
inline void swapStruct(dylinker_command &d) {
  sys::swapByteOrder(d.cmd);
  sys::swapByteOrder(d.cmdsize);
  sys::swapByteOrder(d.name);
}

/// Byte-swap the fields of a uuid_command.
/// \param u Structure whose multi-byte fields are swapped in place.
inline void swapStruct(uuid_command &u) {
  sys::swapByteOrder(u.cmd);
  sys::swapByteOrder(u.cmdsize);
}

/// Byte-swap the fields of a rpath_command.
/// \param r Structure whose multi-byte fields are swapped in place.
inline void swapStruct(rpath_command &r) {
  sys::swapByteOrder(r.cmd);
  sys::swapByteOrder(r.cmdsize);
  sys::swapByteOrder(r.path);
}

/// Byte-swap the fields of a source_version_command.
/// \param s Structure whose multi-byte fields are swapped in place.
inline void swapStruct(source_version_command &s) {
  sys::swapByteOrder(s.cmd);
  sys::swapByteOrder(s.cmdsize);
  sys::swapByteOrder(s.version);
}

/// Byte-swap the fields of a entry_point_command.
/// \param e Structure whose multi-byte fields are swapped in place.
inline void swapStruct(entry_point_command &e) {
  sys::swapByteOrder(e.cmd);
  sys::swapByteOrder(e.cmdsize);
  sys::swapByteOrder(e.entryoff);
  sys::swapByteOrder(e.stacksize);
}

/// Byte-swap the fields of a encryption_info_command.
/// \param e Structure whose multi-byte fields are swapped in place.
inline void swapStruct(encryption_info_command &e) {
  sys::swapByteOrder(e.cmd);
  sys::swapByteOrder(e.cmdsize);
  sys::swapByteOrder(e.cryptoff);
  sys::swapByteOrder(e.cryptsize);
  sys::swapByteOrder(e.cryptid);
}

/// Byte-swap the fields of a encryption_info_command_64.
/// \param e Structure whose multi-byte fields are swapped in place.
inline void swapStruct(encryption_info_command_64 &e) {
  sys::swapByteOrder(e.cmd);
  sys::swapByteOrder(e.cmdsize);
  sys::swapByteOrder(e.cryptoff);
  sys::swapByteOrder(e.cryptsize);
  sys::swapByteOrder(e.cryptid);
  sys::swapByteOrder(e.pad);
}

/// Byte-swap the fields of a dysymtab_command.
/// \param dst Structure whose multi-byte fields are swapped in place.
inline void swapStruct(dysymtab_command &dst) {
  sys::swapByteOrder(dst.cmd);
  sys::swapByteOrder(dst.cmdsize);
  sys::swapByteOrder(dst.ilocalsym);
  sys::swapByteOrder(dst.nlocalsym);
  sys::swapByteOrder(dst.iextdefsym);
  sys::swapByteOrder(dst.nextdefsym);
  sys::swapByteOrder(dst.iundefsym);
  sys::swapByteOrder(dst.nundefsym);
  sys::swapByteOrder(dst.tocoff);
  sys::swapByteOrder(dst.ntoc);
  sys::swapByteOrder(dst.modtaboff);
  sys::swapByteOrder(dst.nmodtab);
  sys::swapByteOrder(dst.extrefsymoff);
  sys::swapByteOrder(dst.nextrefsyms);
  sys::swapByteOrder(dst.indirectsymoff);
  sys::swapByteOrder(dst.nindirectsyms);
  sys::swapByteOrder(dst.extreloff);
  sys::swapByteOrder(dst.nextrel);
  sys::swapByteOrder(dst.locreloff);
  sys::swapByteOrder(dst.nlocrel);
}

/// Byte-swap the fields of a any_relocation_info.
/// \param reloc Structure whose multi-byte fields are swapped in place.
inline void swapStruct(any_relocation_info &reloc) {
  sys::swapByteOrder(reloc.r_word0);
  sys::swapByteOrder(reloc.r_word1);
}

/// Byte-swap the fields of a nlist_base.
/// \param S Structure whose multi-byte fields are swapped in place.
inline void swapStruct(nlist_base &S) {
  sys::swapByteOrder(S.n_strx);
  sys::swapByteOrder(S.n_desc);
}

/// Byte-swap the fields of a nlist.
/// \param sym Structure whose multi-byte fields are swapped in place.
inline void swapStruct(nlist &sym) {
  sys::swapByteOrder(sym.n_strx);
  sys::swapByteOrder(sym.n_desc);
  sys::swapByteOrder(sym.n_value);
}

/// Byte-swap the fields of a nlist_64.
/// \param sym Structure whose multi-byte fields are swapped in place.
inline void swapStruct(nlist_64 &sym) {
  sys::swapByteOrder(sym.n_strx);
  sys::swapByteOrder(sym.n_desc);
  sys::swapByteOrder(sym.n_value);
}

/// Byte-swap the fields of a linkedit_data_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(linkedit_data_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.dataoff);
  sys::swapByteOrder(C.datasize);
}

/// Byte-swap the fields of a linker_option_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(linker_option_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.count);
}

/// Byte-swap the fields of a fileset_entry_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(fileset_entry_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.vmaddr);
  sys::swapByteOrder(C.fileoff);
  sys::swapByteOrder(C.entry_id.offset);
  sys::swapByteOrder(C.reserved);
}

/// Byte-swap the fields of a version_min_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(version_min_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.version);
  sys::swapByteOrder(C.sdk);
}

/// Byte-swap the fields of a note_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(note_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.offset);
  sys::swapByteOrder(C.size);
}

/// Byte-swap the fields of a build_version_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(build_version_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.platform);
  sys::swapByteOrder(C.minos);
  sys::swapByteOrder(C.sdk);
  sys::swapByteOrder(C.ntools);
}

/// Byte-swap the fields of a target_triple_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(target_triple_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.triple);
}

/// Byte-swap the fields of a build_tool_version.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(build_tool_version &C) {
  sys::swapByteOrder(C.tool);
  sys::swapByteOrder(C.version);
}

/// Byte-swap the fields of a data_in_code_entry.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(data_in_code_entry &C) {
  sys::swapByteOrder(C.offset);
  sys::swapByteOrder(C.length);
  sys::swapByteOrder(C.kind);
}

/// Byte-swap the fields of a uint32_t.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(uint32_t &C) { sys::swapByteOrder(C); }

// The prebind_cksum_command is obsolete and no longer supported.
/// Byte-swap the fields of a prebind_cksum_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(prebind_cksum_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.cksum);
}

// The twolevel_hints_command is obsolete and no longer supported.
/// Byte-swap the fields of a twolevel_hints_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(twolevel_hints_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.offset);
  sys::swapByteOrder(C.nhints);
}

// The prebound_dylib_command is obsolete and no longer supported.
/// Byte-swap the fields of a prebound_dylib_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(prebound_dylib_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.name);
  sys::swapByteOrder(C.nmodules);
  sys::swapByteOrder(C.linked_modules);
}

// The fvmfile_command is obsolete and no longer supported.
/// Byte-swap the fields of a fvmfile_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(fvmfile_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.name);
  sys::swapByteOrder(C.header_addr);
}

// The symseg_command is obsolete and no longer supported.
/// Byte-swap the fields of a symseg_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(symseg_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  sys::swapByteOrder(C.offset);
  sys::swapByteOrder(C.size);
}

// The ident_command is obsolete and no longer supported.
/// Byte-swap the fields of a ident_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(ident_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
}

/// Byte-swap the fields of a fvmlib.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(fvmlib &C) {
  sys::swapByteOrder(C.name);
  sys::swapByteOrder(C.minor_version);
  sys::swapByteOrder(C.header_addr);
}

// The fvmlib_command is obsolete and no longer supported.
/// Byte-swap the fields of a fvmlib_command.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(fvmlib_command &C) {
  sys::swapByteOrder(C.cmd);
  sys::swapByteOrder(C.cmdsize);
  swapStruct(C.fvmlib);
}

// Get/Set functions from <mach-o/nlist.h>

/// Extract the library ordinal from an nlist n_desc value.
///
/// \param n_desc Symbol description field.
/// \return Library ordinal stored in the high byte of \p n_desc.
inline uint16_t GET_LIBRARY_ORDINAL(uint16_t n_desc) {
  return (((n_desc) >> 8u) & 0xffu);
}

/// Store \p ordinal into the library-ordinal bits of \p n_desc.
///
/// \param n_desc Symbol description field to update.
/// \param ordinal Library ordinal to store (0–255).
inline void SET_LIBRARY_ORDINAL(uint16_t &n_desc, uint8_t ordinal) {
  n_desc = (((n_desc)&0x00ff) | (((ordinal)&0xff) << 8));
}

/// Extract the common-symbol alignment from an nlist n_desc value.
///
/// \param n_desc Symbol description field.
/// \return Common-symbol alignment as a power of two (low 4 bits).
inline uint8_t GET_COMM_ALIGN(uint16_t n_desc) {
  return (n_desc >> 8u) & 0x0fu;
}

/// Store \p align into the common-symbol alignment bits of \p n_desc.
///
/// \param n_desc Symbol description field to update.
/// \param align Alignment as a power of two (low 4 bits).
inline void SET_COMM_ALIGN(uint16_t &n_desc, uint8_t align) {
  n_desc = ((n_desc & 0xf0ffu) | ((align & 0x0fu) << 8u));
}

// Enums from <mach/machine.h>
/// Architecture capability bits OR-ed into CPUType values.
enum : uint32_t {
  // Capability bits used in the definition of cpu_type.
  CPU_ARCH_MASK = 0xff000000, ///< Mask for architecture bits
  CPU_ARCH_ABI64 = 0x01000000, ///< 64 bit ABI
  CPU_ARCH_ABI64_32 = 0x02000000, ///< ILP32 ABI on 64-bit hardware
};

// Constants for the cputype field.
/// Mach-O CPU type codes for mach_header::cputype.
enum CPUType {
  CPU_TYPE_ANY = -1, ///< Matches any CPU type.
  CPU_TYPE_X86 = 7, ///< Intel x86 CPU type.
  CPU_TYPE_I386 = CPU_TYPE_X86, ///< Alias for CPU_TYPE_X86.
  CPU_TYPE_X86_64 = CPU_TYPE_X86 | CPU_ARCH_ABI64, ///< x86_64 CPU type.
  /* CPU_TYPE_MIPS      = 8, */
  CPU_TYPE_MC98000 = 10, ///< Old Motorola PowerPC
  CPU_TYPE_ARM = 12, ///< 32-bit ARM CPU type.
  CPU_TYPE_ARM64 = CPU_TYPE_ARM | CPU_ARCH_ABI64, ///< ARM64 CPU type.
  CPU_TYPE_ARM64_32 = CPU_TYPE_ARM | CPU_ARCH_ABI64_32, ///< ARM64_32 (ILP32) CPU type.
  CPU_TYPE_SPARC = 14, ///< SPARC CPU type.
  CPU_TYPE_POWERPC = 18, ///< PowerPC CPU type.
  CPU_TYPE_POWERPC64 = CPU_TYPE_POWERPC | CPU_ARCH_ABI64, ///< PowerPC 64-bit CPU type.

  CPU_TYPE_RISCV = 24, ///< RISC-V CPU type.
};

/// Capability and special bits for mach_header::cpusubtype.
enum : uint32_t {
  // Capability bits used in the definition of cpusubtype.
  CPU_SUBTYPE_MASK = 0xff000000, ///< Mask for architecture bits
  CPU_SUBTYPE_LIB64 = 0x80000000, ///< 64 bit libraries

  // Special CPU subtype constants.
  CPU_SUBTYPE_MULTIPLE = ~0u ///< Matches any CPU subtype.
};

// Constants for the cpusubtype field.
/// x86 / x86_64 CPU subtype codes.
enum CPUSubTypeX86 {
  CPU_SUBTYPE_I386_ALL = 3, ///< Generic i386 subtype.
  CPU_SUBTYPE_386 = 3, ///< 386.
  CPU_SUBTYPE_486 = 4, ///< 486.
  CPU_SUBTYPE_486SX = 0x84, ///< 486SX.
  CPU_SUBTYPE_586 = 5, ///< 586.
  CPU_SUBTYPE_PENT = CPU_SUBTYPE_586, ///< PENT.
  CPU_SUBTYPE_PENTPRO = 0x16, ///< PENTPRO.
  CPU_SUBTYPE_PENTII_M3 = 0x36, ///< PENTII M3.
  CPU_SUBTYPE_PENTII_M5 = 0x56, ///< PENTII M5.
  CPU_SUBTYPE_CELERON = 0x67, ///< CELERON.
  CPU_SUBTYPE_CELERON_MOBILE = 0x77, ///< CELERON MOBILE.
  CPU_SUBTYPE_PENTIUM_3 = 0x08, ///< PENTIUM 3.
  CPU_SUBTYPE_PENTIUM_3_M = 0x18, ///< PENTIUM 3 M.
  CPU_SUBTYPE_PENTIUM_3_XEON = 0x28, ///< PENTIUM 3 XEON.
  CPU_SUBTYPE_PENTIUM_M = 0x09, ///< PENTIUM M.
  CPU_SUBTYPE_PENTIUM_4 = 0x0a, ///< PENTIUM 4.
  CPU_SUBTYPE_PENTIUM_4_M = 0x1a, ///< PENTIUM 4 M.
  CPU_SUBTYPE_ITANIUM = 0x0b, ///< ITANIUM.
  CPU_SUBTYPE_ITANIUM_2 = 0x1b, ///< ITANIUM 2.
  CPU_SUBTYPE_XEON = 0x0c, ///< XEON.
  CPU_SUBTYPE_XEON_MP = 0x1c, ///< XEON MP.

  CPU_SUBTYPE_X86_ALL = 3, ///< Generic x86 subtype.
  CPU_SUBTYPE_X86_64_ALL = 3, ///< Generic x86_64 subtype.
  CPU_SUBTYPE_X86_ARCH1 = 4, ///< x86 ARCH1 subtype.
  CPU_SUBTYPE_X86_64_H = 8 ///< Haswell x86_64 subtype.
};
/// Encode an Intel CPU subtype from family and model numbers.
///
/// \param Family Intel CPU family.
/// \param Model Intel CPU model.
/// \return Encoded Intel CPU subtype value.
inline int CPU_SUBTYPE_INTEL(int Family, int Model) {
  return Family | (Model << 4);
}
/// Extract the Intel CPU family from a CPU subtype.
///
/// \param ST Intel CPU subtype value.
/// \return The Intel CPU family encoded in \p ST.
inline int CPU_SUBTYPE_INTEL_FAMILY(CPUSubTypeX86 ST) {
  return ((int)ST) & 0x0f;
}
/// Extract the Intel CPU model from a CPU subtype.
///
/// \param ST Intel CPU subtype value.
/// \return The Intel CPU model encoded in \p ST.
inline int CPU_SUBTYPE_INTEL_MODEL(CPUSubTypeX86 ST) { return ((int)ST) >> 4; }
/// Limits and sentinel values for Intel CPU subtype encoding.
enum {
  CPU_SUBTYPE_INTEL_FAMILY_MAX = 15, ///< Maximum Intel CPU family encoding.
  CPU_SUBTYPE_INTEL_MODEL_ALL = 0 ///< Wildcard matching any Intel model.
};

/// 32-bit ARM CPU subtype codes.
enum CPUSubTypeARM {
  CPU_SUBTYPE_ARM_ALL = 0, ///< Generic 32-bit ARM subtype.
  CPU_SUBTYPE_ARM_V4T = 5, ///< V4T.
  CPU_SUBTYPE_ARM_V6 = 6, ///< V6.
  CPU_SUBTYPE_ARM_V5 = 7, ///< V5.
  CPU_SUBTYPE_ARM_V5TEJ = 7, ///< V5TEJ.
  CPU_SUBTYPE_ARM_XSCALE = 8, ///< XSCALE.
  CPU_SUBTYPE_ARM_V7 = 9, ///< V7.
  //  unused  ARM_V7F     = 10,
  CPU_SUBTYPE_ARM_V7S = 11, ///< V7S.
  CPU_SUBTYPE_ARM_V7K = 12, ///< V7K.
  CPU_SUBTYPE_ARM_V6M = 14, ///< V6M.
  CPU_SUBTYPE_ARM_V7M = 15, ///< V7M.
  CPU_SUBTYPE_ARM_V7EM = 16, ///< V7EM.
  CPU_SUBTYPE_ARM_V8M_MAIN = 17, ///< V8M MAIN.
  CPU_SUBTYPE_ARM_V8M_BASE = 18, ///< V8M BASE.
  CPU_SUBTYPE_ARM_V8_1M_MAIN = 19, ///< V8 1M MAIN.
};

/// ARM64 CPU subtype codes and ptrauth ABI capability masks.
enum CPUSubTypeARM64 : uint32_t {
  CPU_SUBTYPE_ARM64_ALL = 0, ///< Generic ARM64 subtype.
  CPU_SUBTYPE_ARM64_V8 = 1, ///< ARMv8 ARM64 subtype.
  CPU_SUBTYPE_ARM64E = 2, ///< arm64e (pointer-authenticated) subtype.

  // arm64e uses the capability bits to encode ptrauth ABI information.
  // Bit 63 marks the binary as Versioned.
  CPU_SUBTYPE_ARM64E_VERSIONED_PTRAUTH_ABI_MASK = 0x80000000U, ///< Marks a versioned ptrauth ABI.
  // Bit 62 marks the binary as using a kernel ABI.
  CPU_SUBTYPE_ARM64E_KERNEL_PTRAUTH_ABI_MASK = 0x40000000U, ///< Marks a kernel ptrauth ABI.
  // Bits [59:56] hold the 4-bit ptrauth ABI version.
  CPU_SUBTYPE_ARM64E_PTRAUTH_MASK = 0x0f000000U, ///< Mask for the 4-bit ptrauth ABI version.
};

/// Extract the 4-bit ptrauth ABI version from an arm64e CPU subtype.
///
/// \param ST ARM64E CPU subtype value.
/// \return The 4-bit ptrauth ABI version encoded in \p ST.
inline unsigned CPU_SUBTYPE_ARM64E_PTRAUTH_VERSION(uint32_t ST) {
  return (ST & CPU_SUBTYPE_ARM64E_PTRAUTH_MASK) >> 24;
}

/// Build an arm64e CPU subtype with the given ptrauth ABI version.
///
/// \param PtrAuthABIVersion 4-bit ptrauth ABI version.
/// \param PtrAuthKernelABIVersion Whether to set the kernel ptrauth ABI bit.
/// \return ARM64E subtype with versioned ptrauth bits encoded.
inline uint32_t
CPU_SUBTYPE_ARM64E_WITH_PTRAUTH_VERSION(unsigned PtrAuthABIVersion,
                                        bool PtrAuthKernelABIVersion) {
  assert((PtrAuthABIVersion <= 0xF) &&
         "ptrauth abi version must fit in 4 bits");
  return CPU_SUBTYPE_ARM64E | CPU_SUBTYPE_ARM64E_VERSIONED_PTRAUTH_ABI_MASK |
         (PtrAuthKernelABIVersion
              ? (uint32_t)CPU_SUBTYPE_ARM64E_KERNEL_PTRAUTH_ABI_MASK
              : 0) |
         (PtrAuthABIVersion << 24);
}

/// Return true if \p ST marks a versioned ptrauth ABI arm64e binary.
///
/// \param ST ARM64E CPU subtype value.
/// \return True if the versioned ptrauth ABI bit is set.
inline bool CPU_SUBTYPE_ARM64E_IS_VERSIONED_PTRAUTH_ABI(uint32_t ST) {
  return ST & CPU_SUBTYPE_ARM64E_VERSIONED_PTRAUTH_ABI_MASK;
}

/// Return true if \p ST marks a kernel ptrauth ABI arm64e binary.
///
/// \param ST ARM64E CPU subtype value.
/// \return True if the kernel ptrauth ABI bit is set.
inline bool CPU_SUBTYPE_ARM64E_IS_KERNEL_PTRAUTH_ABI(uint32_t ST) {
  return ST & CPU_SUBTYPE_ARM64E_KERNEL_PTRAUTH_ABI_MASK;
}

/// ARM64_32 CPU subtype codes.
enum CPUSubTypeARM64_32 {
  CPU_SUBTYPE_ARM64_32_V8 = 1 ///< ARMv8 ARM64_32 subtype.
};

/// SPARC CPU subtype codes.
enum CPUSubTypeSPARC {
  CPU_SUBTYPE_SPARC_ALL = 0 ///< Generic SPARC subtype.
};

/// PowerPC CPU subtype codes.
enum CPUSubTypePowerPC {
  CPU_SUBTYPE_POWERPC_ALL = 0, ///< Generic PowerPC subtype.
  CPU_SUBTYPE_POWERPC_601 = 1, ///< 601.
  CPU_SUBTYPE_POWERPC_602 = 2, ///< 602.
  CPU_SUBTYPE_POWERPC_603 = 3, ///< 603.
  CPU_SUBTYPE_POWERPC_603e = 4, ///< 603e.
  CPU_SUBTYPE_POWERPC_603ev = 5, ///< 603ev.
  CPU_SUBTYPE_POWERPC_604 = 6, ///< 604.
  CPU_SUBTYPE_POWERPC_604e = 7, ///< 604e.
  CPU_SUBTYPE_POWERPC_620 = 8, ///< 620.
  CPU_SUBTYPE_POWERPC_750 = 9, ///< 750.
  CPU_SUBTYPE_POWERPC_7400 = 10, ///< 7400.
  CPU_SUBTYPE_POWERPC_7450 = 11, ///< 7450.
  CPU_SUBTYPE_POWERPC_970 = 100, ///< 970.

  CPU_SUBTYPE_MC980000_ALL = CPU_SUBTYPE_POWERPC_ALL, ///< Alias for CPU_SUBTYPE_POWERPC_ALL.
  CPU_SUBTYPE_MC98601 = CPU_SUBTYPE_POWERPC_601 ///< Alias for CPU_SUBTYPE_POWERPC_601.
};

/// RISC-V CPU subtype codes.
enum CPUSubTypeRISCV {
  CPU_SUBTYPE_RISCV_ALL = 0, ///< Generic RISC-V subtype.
};

/// Map a target triple to a Mach-O CPUType value.
///
/// \param T Target triple to map.
/// \return The Mach-O CPUType value, or an error if \p T is unsupported.
LLVM_ABI Expected<uint32_t> getCPUType(const Triple &T);
/// Map a target triple to a Mach-O CPU subtype value.
///
/// \param T Target triple to map.
/// \return The Mach-O CPU subtype, or an error if \p T is unsupported.
LLVM_ABI Expected<uint32_t> getCPUSubType(const Triple &T);
/// Map a target triple to a Mach-O CPU subtype, including arm64e ptrauth ABI bits.
///
/// \param T Target triple to map.
/// \param PtrAuthABIVersion 4-bit ptrauth ABI version for arm64e.
/// \param PtrAuthKernelABIVersion Whether to set the kernel ptrauth ABI bit.
/// \return The Mach-O CPU subtype, or an error if \p T is unsupported.
LLVM_ABI Expected<uint32_t> getCPUSubType(const Triple &T,
                                          unsigned PtrAuthABIVersion,
                                          bool PtrAuthKernelABIVersion);

/// 32-bit x86 general-purpose thread state.
struct x86_thread_state32_t {
  uint32_t eax; ///< EAX register.
  uint32_t ebx; ///< EBX register.
  uint32_t ecx; ///< ECX register.
  uint32_t edx; ///< EDX register.
  uint32_t edi; ///< EDI register.
  uint32_t esi; ///< ESI register.
  uint32_t ebp; ///< EBP register.
  uint32_t esp; ///< ESP register.
  uint32_t ss; ///< SS segment register.
  uint32_t eflags; ///< EFLAGS register.
  uint32_t eip; ///< EIP register.
  uint32_t cs; ///< CS segment register.
  uint32_t ds; ///< DS segment register.
  uint32_t es; ///< ES segment register.
  uint32_t fs; ///< FS segment register.
  uint32_t gs; ///< GS segment register.
};

/// 64-bit x86_64 general-purpose thread state.
struct x86_thread_state64_t {
  uint64_t rax; ///< RAX register.
  uint64_t rbx; ///< RBX register.
  uint64_t rcx; ///< RCX register.
  uint64_t rdx; ///< RDX register.
  uint64_t rdi; ///< RDI register.
  uint64_t rsi; ///< RSI register.
  uint64_t rbp; ///< RBP register.
  uint64_t rsp; ///< RSP register.
  uint64_t r8; ///< General-purpose register r8.
  uint64_t r9; ///< General-purpose register r9.
  uint64_t r10; ///< General-purpose register r10.
  uint64_t r11; ///< General-purpose register r11.
  uint64_t r12; ///< General-purpose register r12.
  uint64_t r13; ///< General-purpose register r13.
  uint64_t r14; ///< General-purpose register r14.
  uint64_t r15; ///< General-purpose register r15.
  uint64_t rip; ///< RIP register.
  uint64_t rflags; ///< RFLAGS register.
  uint64_t cs; ///< CS segment register.
  uint64_t fs; ///< FS segment register.
  uint64_t gs; ///< GS segment register.
};

/// x87 floating-point precision control encodings.
enum x86_fp_control_precis {
  x86_FP_PREC_24B = 0, ///< 24-bit floating-point precision.
  x86_FP_PREC_53B = 2, ///< 53-bit floating-point precision.
  x86_FP_PREC_64B = 3 ///< 64-bit floating-point precision.
};

/// x87 floating-point rounding-control encodings.
enum x86_fp_control_rc {
  x86_FP_RND_NEAR = 0, ///< Round to nearest.
  x86_FP_RND_DOWN = 1, ///< Round toward negative infinity.
  x86_FP_RND_UP = 2, ///< Round toward positive infinity.
  x86_FP_CHOP = 3 ///< Round toward zero (chop).
};

/// x87 floating-point control word bitfields.
struct fp_control_t {
  unsigned short invalid : 1; ///< Invalid-operation exception mask.
  unsigned short denorm : 1; ///< Denormal-operand exception mask.
  unsigned short zdiv : 1; ///< Divide-by-zero exception mask.
  unsigned short ovrfl : 1; ///< Overflow exception mask.
  unsigned short undfl : 1; ///< Underflow exception mask.
  unsigned short precis : 1; ///< Precision exception mask.
  unsigned short : 2;
  unsigned short pc : 2; ///< Precision control (x86_fp_control_precis).
  unsigned short rc : 2; ///< Rounding control (x86_fp_control_rc).
  unsigned short : 1;
  unsigned short : 3;
};

/// x87 floating-point status word bitfields.
struct fp_status_t {
  unsigned short invalid : 1; ///< Invalid-operation exception flag.
  unsigned short denorm : 1; ///< Denormal-operand exception flag.
  unsigned short zdiv : 1; ///< Divide-by-zero exception flag.
  unsigned short ovrfl : 1; ///< Overflow exception flag.
  unsigned short undfl : 1; ///< Underflow exception flag.
  unsigned short precis : 1; ///< Precision exception flag.
  unsigned short stkflt : 1; ///< Stack-fault exception flag.
  unsigned short errsumm : 1; ///< Error-summary status bit.
  unsigned short c0 : 1; ///< Condition-code bit C0.
  unsigned short c1 : 1; ///< Condition-code bit C1.
  unsigned short c2 : 1; ///< Condition-code bit C2.
  unsigned short tos : 3; ///< Top-of-stack pointer.
  unsigned short c3 : 1; ///< Condition-code bit C3.
  unsigned short busy : 1; ///< FPU busy status bit.
};

/// x87 / MMX register image (10-byte value plus padding).
struct mmst_reg_t {
  char mmst_reg[10]; ///< 80-bit x87/MMX register contents.
  char mmst_rsrv[6]; ///< Padding reserved after the 80-bit register image.
};

/// SSE XMM register image (16 bytes).
struct xmm_reg_t {
  char xmm_reg[16]; ///< 128-bit XMM register contents.
};

/// 64-bit x86 floating-point and SSE register state.
struct x86_float_state64_t {
  int32_t fpu_reserved[2]; ///< Reserved for future use.
  fp_control_t fpu_fcw; ///< x87 floating-point control word.
  fp_status_t fpu_fsw; ///< x87 floating-point status word.
  uint8_t fpu_ftw; ///< x87 tag word.
  uint8_t fpu_rsrv1; ///< Reserved.
  uint16_t fpu_fop; ///< x87 last instruction opcode.
  uint32_t fpu_ip; ///< x87 instruction pointer offset.
  uint16_t fpu_cs; ///< x87 instruction pointer selector.
  uint16_t fpu_rsrv2; ///< Reserved.
  uint32_t fpu_dp; ///< x87 data pointer offset.
  uint16_t fpu_ds; ///< x87 data pointer selector.
  uint16_t fpu_rsrv3; ///< Reserved.
  uint32_t fpu_mxcsr; ///< SSE MXCSR control/status register.
  uint32_t fpu_mxcsrmask; ///< Supported MXCSR bit mask.
  mmst_reg_t fpu_stmm0; ///< x87 ST(0) / MM0 register.
  mmst_reg_t fpu_stmm1; ///< x87 ST(1) / MM1 register.
  mmst_reg_t fpu_stmm2; ///< x87 ST(2) / MM2 register.
  mmst_reg_t fpu_stmm3; ///< x87 ST(3) / MM3 register.
  mmst_reg_t fpu_stmm4; ///< x87 ST(4) / MM4 register.
  mmst_reg_t fpu_stmm5; ///< x87 ST(5) / MM5 register.
  mmst_reg_t fpu_stmm6; ///< x87 ST(6) / MM6 register.
  mmst_reg_t fpu_stmm7; ///< x87 ST(7) / MM7 register.
  xmm_reg_t fpu_xmm0; ///< XMM0 register.
  xmm_reg_t fpu_xmm1; ///< XMM1 register.
  xmm_reg_t fpu_xmm2; ///< XMM2 register.
  xmm_reg_t fpu_xmm3; ///< XMM3 register.
  xmm_reg_t fpu_xmm4; ///< XMM4 register.
  xmm_reg_t fpu_xmm5; ///< XMM5 register.
  xmm_reg_t fpu_xmm6; ///< XMM6 register.
  xmm_reg_t fpu_xmm7; ///< XMM7 register.
  xmm_reg_t fpu_xmm8; ///< XMM8 register.
  xmm_reg_t fpu_xmm9; ///< XMM9 register.
  xmm_reg_t fpu_xmm10; ///< XMM10 register.
  xmm_reg_t fpu_xmm11; ///< XMM11 register.
  xmm_reg_t fpu_xmm12; ///< XMM12 register.
  xmm_reg_t fpu_xmm13; ///< XMM13 register.
  xmm_reg_t fpu_xmm14; ///< XMM14 register.
  xmm_reg_t fpu_xmm15; ///< XMM15 register.
  char fpu_rsrv4[6 * 16]; ///< Reserved SSE state padding.
  uint32_t fpu_reserved1; ///< Reserved; must be zero.
};

/// 64-bit x86 exception state.
struct x86_exception_state64_t {
  uint16_t trapno; ///< Exception trap number.
  uint16_t cpu; ///< CPU number that raised the exception.
  uint32_t err; ///< Exception error code.
  uint64_t faultvaddr; ///< Faulting virtual address.
};

/// Byte-swap the fields of a x86_thread_state32_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(x86_thread_state32_t &x) {
  sys::swapByteOrder(x.eax);
  sys::swapByteOrder(x.ebx);
  sys::swapByteOrder(x.ecx);
  sys::swapByteOrder(x.edx);
  sys::swapByteOrder(x.edi);
  sys::swapByteOrder(x.esi);
  sys::swapByteOrder(x.ebp);
  sys::swapByteOrder(x.esp);
  sys::swapByteOrder(x.ss);
  sys::swapByteOrder(x.eflags);
  sys::swapByteOrder(x.eip);
  sys::swapByteOrder(x.cs);
  sys::swapByteOrder(x.ds);
  sys::swapByteOrder(x.es);
  sys::swapByteOrder(x.fs);
  sys::swapByteOrder(x.gs);
}

/// Byte-swap the fields of a x86_thread_state64_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(x86_thread_state64_t &x) {
  sys::swapByteOrder(x.rax);
  sys::swapByteOrder(x.rbx);
  sys::swapByteOrder(x.rcx);
  sys::swapByteOrder(x.rdx);
  sys::swapByteOrder(x.rdi);
  sys::swapByteOrder(x.rsi);
  sys::swapByteOrder(x.rbp);
  sys::swapByteOrder(x.rsp);
  sys::swapByteOrder(x.r8);
  sys::swapByteOrder(x.r9);
  sys::swapByteOrder(x.r10);
  sys::swapByteOrder(x.r11);
  sys::swapByteOrder(x.r12);
  sys::swapByteOrder(x.r13);
  sys::swapByteOrder(x.r14);
  sys::swapByteOrder(x.r15);
  sys::swapByteOrder(x.rip);
  sys::swapByteOrder(x.rflags);
  sys::swapByteOrder(x.cs);
  sys::swapByteOrder(x.fs);
  sys::swapByteOrder(x.gs);
}

/// Byte-swap the fields of a x86_float_state64_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(x86_float_state64_t &x) {
  sys::swapByteOrder(x.fpu_reserved[0]);
  sys::swapByteOrder(x.fpu_reserved[1]);
  // TODO swap: fp_control_t fpu_fcw;
  // TODO swap: fp_status_t fpu_fsw;
  sys::swapByteOrder(x.fpu_fop);
  sys::swapByteOrder(x.fpu_ip);
  sys::swapByteOrder(x.fpu_cs);
  sys::swapByteOrder(x.fpu_rsrv2);
  sys::swapByteOrder(x.fpu_dp);
  sys::swapByteOrder(x.fpu_ds);
  sys::swapByteOrder(x.fpu_rsrv3);
  sys::swapByteOrder(x.fpu_mxcsr);
  sys::swapByteOrder(x.fpu_mxcsrmask);
  sys::swapByteOrder(x.fpu_reserved1);
}

/// Byte-swap the fields of a x86_exception_state64_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(x86_exception_state64_t &x) {
  sys::swapByteOrder(x.trapno);
  sys::swapByteOrder(x.cpu);
  sys::swapByteOrder(x.err);
  sys::swapByteOrder(x.faultvaddr);
}

/// Flavor/count header preceding an x86 thread-state blob.
struct x86_state_hdr_t {
  uint32_t flavor; ///< Thread-state flavor (architecture-specific enum).
  uint32_t count; ///< Number of entries that follow.
};

/// Flavor-tagged x86 thread state (32- or 64-bit).
struct x86_thread_state_t {
  x86_state_hdr_t tsh; ///< Thread-state header (flavor and count).
  /// Architecture-specific state payload selected by the header flavor.
  union {
    x86_thread_state64_t ts64; ///< 64-bit thread state.
    x86_thread_state32_t ts32; ///< 32-bit thread state.
  } uts; ///< Union of architecture-specific thread-state layouts.
};

/// Flavor-tagged x86 floating-point state.
struct x86_float_state_t {
  x86_state_hdr_t fsh; ///< Float-state header (flavor and count).
  /// Architecture-specific state payload selected by the header flavor.
  union {
    x86_float_state64_t fs64; ///< 64-bit float state.
  } ufs; ///< Union of architecture-specific float-state layouts.
};

/// Flavor-tagged x86 exception state.
struct x86_exception_state_t {
  x86_state_hdr_t esh; ///< Exception-state header (flavor and count).
  /// Architecture-specific state payload selected by the header flavor.
  union {
    x86_exception_state64_t es64; ///< 64-bit exception state.
  } ues; ///< Union of architecture-specific exception-state layouts.
};

/// Byte-swap the fields of a x86_state_hdr_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(x86_state_hdr_t &x) {
  sys::swapByteOrder(x.flavor);
  sys::swapByteOrder(x.count);
}

/// Flavor codes selecting an x86 thread/float/exception state layout.
enum X86ThreadFlavors {
  x86_THREAD_STATE32 = 1, ///< 32-bit x86 thread state flavor.
  x86_FLOAT_STATE32 = 2, ///< 32-bit x86 float state flavor.
  x86_EXCEPTION_STATE32 = 3, ///< 32-bit x86 exception state flavor.
  x86_THREAD_STATE64 = 4, ///< 64-bit x86 thread state flavor.
  x86_FLOAT_STATE64 = 5, ///< 64-bit x86 float state flavor.
  x86_EXCEPTION_STATE64 = 6, ///< 64-bit x86 exception state flavor.
  x86_THREAD_STATE = 7, ///< Generic x86 thread state flavor.
  x86_FLOAT_STATE = 8, ///< Generic x86 float state flavor.
  x86_EXCEPTION_STATE = 9, ///< Generic x86 exception state flavor.
  x86_DEBUG_STATE32 = 10, ///< 32-bit x86 debug state flavor.
  x86_DEBUG_STATE64 = 11, ///< 64-bit x86 debug state flavor.
  x86_DEBUG_STATE = 12 ///< Generic x86 debug state flavor.
};

/// Byte-swap the fields of a x86_thread_state_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(x86_thread_state_t &x) {
  swapStruct(x.tsh);
  if (x.tsh.flavor == x86_THREAD_STATE64)
    swapStruct(x.uts.ts64);
}

/// Byte-swap the fields of a x86_float_state_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(x86_float_state_t &x) {
  swapStruct(x.fsh);
  if (x.fsh.flavor == x86_FLOAT_STATE64)
    swapStruct(x.ufs.fs64);
}

/// Byte-swap the fields of a x86_exception_state_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(x86_exception_state_t &x) {
  swapStruct(x.esh);
  if (x.esh.flavor == x86_EXCEPTION_STATE64)
    swapStruct(x.ues.es64);
}

/// Count of uint32_t words in x86_thread_state32_t.
const uint32_t x86_THREAD_STATE32_COUNT =
    sizeof(x86_thread_state32_t) / sizeof(uint32_t);

/// Count of uint32_t words in x86_thread_state64_t.
const uint32_t x86_THREAD_STATE64_COUNT =
    sizeof(x86_thread_state64_t) / sizeof(uint32_t);
/// Count of uint32_t words in x86_float_state64_t.
const uint32_t x86_FLOAT_STATE64_COUNT =
    sizeof(x86_float_state64_t) / sizeof(uint32_t);
/// Count of uint32_t words in x86_exception_state64_t.
const uint32_t x86_EXCEPTION_STATE64_COUNT =
    sizeof(x86_exception_state64_t) / sizeof(uint32_t);

/// Count of uint32_t words in x86_thread_state_t.
const uint32_t x86_THREAD_STATE_COUNT =
    sizeof(x86_thread_state_t) / sizeof(uint32_t);
/// Count of uint32_t words in x86_float_state_t.
const uint32_t x86_FLOAT_STATE_COUNT =
    sizeof(x86_float_state_t) / sizeof(uint32_t);
/// Count of uint32_t words in x86_exception_state_t.
const uint32_t x86_EXCEPTION_STATE_COUNT =
    sizeof(x86_exception_state_t) / sizeof(uint32_t);

/// 32-bit ARM general-purpose thread state.
struct arm_thread_state32_t {
  uint32_t r[13]; ///< General-purpose registers r0–r12.
  uint32_t sp; ///< Stack pointer.
  uint32_t lr; ///< Link register.
  uint32_t pc; ///< Program counter.
  uint32_t cpsr; ///< Current program status register.
};

/// Byte-swap the fields of a arm_thread_state32_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(arm_thread_state32_t &x) {
  for (int i = 0; i < 13; i++)
    sys::swapByteOrder(x.r[i]);
  sys::swapByteOrder(x.sp);
  sys::swapByteOrder(x.lr);
  sys::swapByteOrder(x.pc);
  sys::swapByteOrder(x.cpsr);
}

/// 64-bit AArch64 general-purpose thread state.
struct arm_thread_state64_t {
  uint64_t x[29]; ///< General-purpose registers x0–x28.
  uint64_t fp; ///< Frame pointer (x29).
  uint64_t lr; ///< Link register.
  uint64_t sp; ///< Stack pointer.
  uint64_t pc; ///< Program counter.
  uint32_t cpsr; ///< Current program status register.
  uint32_t pad; ///< Padding to maintain alignment.
};

/// Byte-swap the fields of a arm_thread_state64_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(arm_thread_state64_t &x) {
  for (int i = 0; i < 29; i++)
    sys::swapByteOrder(x.x[i]);
  sys::swapByteOrder(x.fp);
  sys::swapByteOrder(x.lr);
  sys::swapByteOrder(x.sp);
  sys::swapByteOrder(x.pc);
  sys::swapByteOrder(x.cpsr);
}

/// Flavor/count header preceding an ARM thread-state blob.
struct arm_state_hdr_t {
  uint32_t flavor; ///< Thread-state flavor (architecture-specific enum).
  uint32_t count; ///< Number of entries that follow.
};

/// Flavor-tagged ARM thread state.
struct arm_thread_state_t {
  arm_state_hdr_t tsh; ///< Thread-state header (flavor and count).
  /// Architecture-specific state payload selected by the header flavor.
  union {
    arm_thread_state32_t ts32; ///< 32-bit thread state.
  } uts; ///< Union of architecture-specific thread-state layouts.
};

/// Byte-swap the fields of a arm_state_hdr_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(arm_state_hdr_t &x) {
  sys::swapByteOrder(x.flavor);
  sys::swapByteOrder(x.count);
}

/// Flavor codes selecting an ARM thread/VFP/exception state layout.
enum ARMThreadFlavors {
  ARM_THREAD_STATE = 1, ///< 32-bit ARM thread state flavor.
  ARM_VFP_STATE = 2, ///< ARM VFP state flavor.
  ARM_EXCEPTION_STATE = 3, ///< ARM exception state flavor.
  ARM_DEBUG_STATE = 4, ///< ARM debug state flavor.
  ARN_THREAD_STATE_NONE = 5, ///< No ARM thread state (legacy NONE spelling).
  ARM_THREAD_STATE64 = 6, ///< 64-bit ARM thread state flavor.
  ARM_EXCEPTION_STATE64 = 7 ///< 64-bit ARM exception state flavor.
};

/// Byte-swap the fields of a arm_thread_state_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(arm_thread_state_t &x) {
  swapStruct(x.tsh);
  if (x.tsh.flavor == ARM_THREAD_STATE)
    swapStruct(x.uts.ts32);
}

/// Count of uint32_t words in arm_thread_state32_t.
const uint32_t ARM_THREAD_STATE_COUNT =
    sizeof(arm_thread_state32_t) / sizeof(uint32_t);

/// Count of uint32_t words in arm_thread_state64_t.
const uint32_t ARM_THREAD_STATE64_COUNT =
    sizeof(arm_thread_state64_t) / sizeof(uint32_t);

/// 32-bit PowerPC general-purpose thread state.
struct ppc_thread_state32_t {
  uint32_t srr0; ///< Machine status save/restore register 0 (PC).
  uint32_t srr1; ///< Machine status save/restore register 1 (MSR).
  uint32_t r0; ///< General-purpose register r0.
  uint32_t r1; ///< General-purpose register r1.
  uint32_t r2; ///< General-purpose register r2.
  uint32_t r3; ///< General-purpose register r3.
  uint32_t r4; ///< General-purpose register r4.
  uint32_t r5; ///< General-purpose register r5.
  uint32_t r6; ///< General-purpose register r6.
  uint32_t r7; ///< General-purpose register r7.
  uint32_t r8; ///< General-purpose register r8.
  uint32_t r9; ///< General-purpose register r9.
  uint32_t r10; ///< General-purpose register r10.
  uint32_t r11; ///< General-purpose register r11.
  uint32_t r12; ///< General-purpose register r12.
  uint32_t r13; ///< General-purpose register r13.
  uint32_t r14; ///< General-purpose register r14.
  uint32_t r15; ///< General-purpose register r15.
  uint32_t r16; ///< General-purpose register r16.
  uint32_t r17; ///< General-purpose register r17.
  uint32_t r18; ///< General-purpose register r18.
  uint32_t r19; ///< General-purpose register r19.
  uint32_t r20; ///< General-purpose register r20.
  uint32_t r21; ///< General-purpose register r21.
  uint32_t r22; ///< General-purpose register r22.
  uint32_t r23; ///< General-purpose register r23.
  uint32_t r24; ///< General-purpose register r24.
  uint32_t r25; ///< General-purpose register r25.
  uint32_t r26; ///< General-purpose register r26.
  uint32_t r27; ///< General-purpose register r27.
  uint32_t r28; ///< General-purpose register r28.
  uint32_t r29; ///< General-purpose register r29.
  uint32_t r30; ///< General-purpose register r30.
  uint32_t r31; ///< General-purpose register r31.
  uint32_t ct; ///< Condition register.
  uint32_t xer; ///< Integer exception register.
  uint32_t lr; ///< Link register.
  uint32_t ctr; ///< Count register.
  uint32_t mq; ///< MQ register (601).
  uint32_t vrsave; ///< VRSAVE register.
};

/// Byte-swap the fields of a ppc_thread_state32_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(ppc_thread_state32_t &x) {
  sys::swapByteOrder(x.srr0);
  sys::swapByteOrder(x.srr1);
  sys::swapByteOrder(x.r0);
  sys::swapByteOrder(x.r1);
  sys::swapByteOrder(x.r2);
  sys::swapByteOrder(x.r3);
  sys::swapByteOrder(x.r4);
  sys::swapByteOrder(x.r5);
  sys::swapByteOrder(x.r6);
  sys::swapByteOrder(x.r7);
  sys::swapByteOrder(x.r8);
  sys::swapByteOrder(x.r9);
  sys::swapByteOrder(x.r10);
  sys::swapByteOrder(x.r11);
  sys::swapByteOrder(x.r12);
  sys::swapByteOrder(x.r13);
  sys::swapByteOrder(x.r14);
  sys::swapByteOrder(x.r15);
  sys::swapByteOrder(x.r16);
  sys::swapByteOrder(x.r17);
  sys::swapByteOrder(x.r18);
  sys::swapByteOrder(x.r19);
  sys::swapByteOrder(x.r20);
  sys::swapByteOrder(x.r21);
  sys::swapByteOrder(x.r22);
  sys::swapByteOrder(x.r23);
  sys::swapByteOrder(x.r24);
  sys::swapByteOrder(x.r25);
  sys::swapByteOrder(x.r26);
  sys::swapByteOrder(x.r27);
  sys::swapByteOrder(x.r28);
  sys::swapByteOrder(x.r29);
  sys::swapByteOrder(x.r30);
  sys::swapByteOrder(x.r31);
  sys::swapByteOrder(x.ct);
  sys::swapByteOrder(x.xer);
  sys::swapByteOrder(x.lr);
  sys::swapByteOrder(x.ctr);
  sys::swapByteOrder(x.mq);
  sys::swapByteOrder(x.vrsave);
}

/// Flavor/count header preceding a PowerPC thread-state blob.
struct ppc_state_hdr_t {
  uint32_t flavor; ///< Thread-state flavor (architecture-specific enum).
  uint32_t count; ///< Number of entries that follow.
};

/// Flavor-tagged PowerPC thread state.
struct ppc_thread_state_t {
  ppc_state_hdr_t tsh; ///< Thread-state header (flavor and count).
  /// Architecture-specific state payload selected by the header flavor.
  union {
    ppc_thread_state32_t ts32; ///< 32-bit thread state.
  } uts; ///< Union of architecture-specific thread-state layouts.
};

/// Byte-swap the fields of a ppc_state_hdr_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(ppc_state_hdr_t &x) {
  sys::swapByteOrder(x.flavor);
  sys::swapByteOrder(x.count);
}

/// Flavor codes selecting a PowerPC thread/float/exception state layout.
enum PPCThreadFlavors {
  PPC_THREAD_STATE = 1, ///< 32-bit PowerPC thread state flavor.
  PPC_FLOAT_STATE = 2, ///< PowerPC float state flavor.
  PPC_EXCEPTION_STATE = 3, ///< PowerPC exception state flavor.
  PPC_VECTOR_STATE = 4, ///< PowerPC vector state flavor.
  PPC_THREAD_STATE64 = 5, ///< 64-bit PowerPC thread state flavor.
  PPC_EXCEPTION_STATE64 = 6, ///< 64-bit PowerPC exception state flavor.
  PPC_THREAD_STATE_NONE = 7 ///< No PowerPC thread state.
};

/// Byte-swap the fields of a ppc_thread_state_t.
/// \param x Structure whose multi-byte fields are swapped in place.
inline void swapStruct(ppc_thread_state_t &x) {
  swapStruct(x.tsh);
  if (x.tsh.flavor == PPC_THREAD_STATE)
    swapStruct(x.uts.ts32);
}

/// Count of uint32_t words in ppc_thread_state32_t.
const uint32_t PPC_THREAD_STATE_COUNT =
    sizeof(ppc_thread_state32_t) / sizeof(uint32_t);

// Define a union of all load command structs
#define LOAD_COMMAND_STRUCT(LCStruct) LCStruct LCStruct##_data;

LLVM_PACKED_START
/// Discriminated union of every Mach-O load-command structure.
union alignas(4) macho_load_command {
#include "llvm/BinaryFormat/MachO.def"
};
LLVM_PACKED_END

/// Byte-swap the fields of a dyld_chained_fixups_header.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(dyld_chained_fixups_header &C) {
  sys::swapByteOrder(C.fixups_version);
  sys::swapByteOrder(C.starts_offset);
  sys::swapByteOrder(C.imports_offset);
  sys::swapByteOrder(C.symbols_offset);
  sys::swapByteOrder(C.imports_count);
  sys::swapByteOrder(C.imports_format);
  sys::swapByteOrder(C.symbols_format);
}

/// Byte-swap the fields of a dyld_chained_starts_in_image.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(dyld_chained_starts_in_image &C) {
  sys::swapByteOrder(C.seg_count);
  // getStructOrErr() cannot copy the variable-length seg_info_offset array.
  // Its elements must be byte swapped manually.
}

/// Byte-swap the fields of a dyld_chained_starts_in_segment.
/// \param C Structure whose multi-byte fields are swapped in place.
inline void swapStruct(dyld_chained_starts_in_segment &C) {
  sys::swapByteOrder(C.size);
  sys::swapByteOrder(C.page_size);
  sys::swapByteOrder(C.pointer_format);
  sys::swapByteOrder(C.segment_offset);
  sys::swapByteOrder(C.max_valid_pointer);
  sys::swapByteOrder(C.page_count);
  // seg_info_offset entries must be byte swapped manually.
}



/// Code-signing attribute flags for a process or signature.
enum CodeSignAttrs {
  CS_VALID = 0x00000001,          ///< dynamically valid
  CS_ADHOC = 0x00000002,          ///< ad hoc signed
  CS_GET_TASK_ALLOW = 0x00000004, ///< has get-task-allow entitlement
  CS_INSTALLER = 0x00000008,      ///< has installer entitlement

  /// Library validation required by Hardened System Policy.
  CS_FORCED_LV =
      0x00000010, ///< Library Validation required by Hardened System Policy
  CS_INVALID_ALLOWED = 0x00000020, ///< (macOS Only) Page invalidation allowed by task port policy

  CS_HARD = 0x00000100,             ///< don't load invalid pages
  CS_KILL = 0x00000200,             ///< kill process if it becomes invalid
  CS_CHECK_EXPIRATION = 0x00000400, ///< force expiration checking
  CS_RESTRICT = 0x00000800,         ///< tell dyld to treat restricted

  CS_ENFORCEMENT = 0x00001000, ///< require enforcement
  CS_REQUIRE_LV = 0x00002000,  ///< require library validation
  /// Signature permits restricted entitlements.
  CS_ENTITLEMENTS_VALIDATED =
      0x00004000, ///< code signature permits restricted entitlements
  /// Has restricted-NVRAM heritable entitlement.
  CS_NVRAM_UNRESTRICTED =
      0x00008000, ///< has com.apple.rootless.restricted-nvram-variables.heritable entitlement

  CS_RUNTIME = 0x00010000,       ///< Apply hardened runtime policies
  CS_LINKER_SIGNED = 0x00020000, ///< Automatically signed by the linker

  /// Mask of code-signing attrs allowed on Mach-O binaries.
  CS_ALLOWED_MACHO =
      (CS_ADHOC | CS_HARD | CS_KILL | CS_CHECK_EXPIRATION | CS_RESTRICT |
       CS_ENFORCEMENT | CS_REQUIRE_LV | CS_RUNTIME | CS_LINKER_SIGNED),

  CS_EXEC_SET_HARD = 0x00100000, ///< set CS_HARD on any exec'ed process
  CS_EXEC_SET_KILL = 0x00200000, ///< set CS_KILL on any exec'ed process
  /// Set CS_ENFORCEMENT on any exec'ed process.
  CS_EXEC_SET_ENFORCEMENT =
      0x00400000, ///< set CS_ENFORCEMENT on any exec'ed process
  /// Set CS_INSTALLER on any exec'ed process.
  CS_EXEC_INHERIT_SIP =
      0x00800000, ///< set CS_INSTALLER on any exec'ed process

  CS_KILLED = 0x01000000, ///< was killed by kernel for invalidity
  /// Loaded by a platform dyld.
  CS_DYLD_PLATFORM =
      0x02000000, ///< dyld used to load this is a platform binary
  CS_PLATFORM_BINARY = 0x04000000, ///< this is a platform binary
  /// Platform binary by path (macOS only).
  CS_PLATFORM_PATH =
      0x08000000, ///< platform binary by the fact of path (osx only)

  CS_DEBUGGED = 0x10000000, ///< process is currently or has previously been debugged and allowed to run with invalid pages
  CS_SIGNED = 0x20000000, ///< process has a signature (may have gone invalid)
  /// Development-signed code.
  CS_DEV_CODE =
      0x40000000, ///< code is dev signed, cannot be loaded into prod signed code (will go away with rdar://problem/28322552)
  /// Has Data Vault controller entitlement.
  CS_DATAVAULT_CONTROLLER =
      0x80000000, ///< has Data Vault controller entitlement

  CS_ENTITLEMENT_FLAGS = (CS_GET_TASK_ALLOW | CS_INSTALLER | ///< Mask of entitlement-related attribute flags.
                          CS_DATAVAULT_CONTROLLER | CS_NVRAM_UNRESTRICTED),
};



/// Executable-segment flags stored in CS_CodeDirectory::execSegFlags.
enum CodeSignExecSegFlags {

  CS_EXECSEG_MAIN_BINARY = 0x1,     ///< executable segment denotes main binary
  CS_EXECSEG_ALLOW_UNSIGNED = 0x10, ///< allow unsigned pages (for debugging)
  CS_EXECSEG_DEBUGGER = 0x20,       ///< main binary is debugger
  CS_EXECSEG_JIT = 0x40,            ///< JIT enabled
  CS_EXECSEG_SKIP_LV = 0x80,        ///< OBSOLETE: skip library validation
  CS_EXECSEG_CAN_LOAD_CDHASH = 0x100, ///< can bless cdhash for execution
  CS_EXECSEG_CAN_EXEC_CDHASH = 0x200, ///< can execute blessed cdhash

};



/// Magic numbers, slot indices, and hash constants for code signing blobs.
enum CodeSignMagic {
  CSMAGIC_REQUIREMENT = 0xfade0c00, ///< single Requirement blob
  /// Requirements vector (internal requirements).
  CSMAGIC_REQUIREMENTS =
      0xfade0c01, ///< Requirements vector (internal requirements)
  CSMAGIC_CODEDIRECTORY = 0xfade0c02,      ///< CodeDirectory blob
  CSMAGIC_EMBEDDED_SIGNATURE = 0xfade0cc0, ///< embedded form of signature data
  CSMAGIC_EMBEDDED_SIGNATURE_OLD = 0xfade0b02, ///< XXX
  CSMAGIC_EMBEDDED_ENTITLEMENTS = 0xfade7171,  ///< embedded entitlements
  /// Multi-arch collection of embedded signatures.
  CSMAGIC_DETACHED_SIGNATURE =
      0xfade0cc1, ///< multi-arch collection of embedded signatures
  CSMAGIC_BLOBWRAPPER = 0xfade0b01, ///< CMS Signature, among other things

  CS_SUPPORTSSCATTER = 0x20100, ///< CodeDirectory version that supports scatter.
  CS_SUPPORTSTEAMID = 0x20200, ///< CodeDirectory version that supports team ID.
  CS_SUPPORTSCODELIMIT64 = 0x20300, ///< CodeDirectory version that supports 64-bit codeLimit.
  CS_SUPPORTSEXECSEG = 0x20400, ///< CodeDirectory version that supports exec segment fields.
  CS_SUPPORTSRUNTIME = 0x20500, ///< CodeDirectory version that supports hardened runtime.
  CS_SUPPORTSLINKAGE = 0x20600, ///< CodeDirectory version that supports linkage.

  CSSLOT_CODEDIRECTORY = 0, ///< slot index for CodeDirectory
  CSSLOT_INFOSLOT = 1, ///< Slot index for info plist.
  CSSLOT_REQUIREMENTS = 2, ///< Slot index for requirements.
  CSSLOT_RESOURCEDIR = 3, ///< Slot index for resource directory.
  CSSLOT_APPLICATION = 4, ///< Slot index for application-specific data.
  CSSLOT_ENTITLEMENTS = 5, ///< Slot index for entitlements.

  /// First alternate CodeDirectory slot index.
  CSSLOT_ALTERNATE_CODEDIRECTORIES =
      0x1000, ///< first alternate CodeDirectory, if any
  CSSLOT_ALTERNATE_CODEDIRECTORY_MAX = 5, ///< max number of alternate CD slots
  /// One past the last alternate CodeDirectory slot.
  CSSLOT_ALTERNATE_CODEDIRECTORY_LIMIT =
      CSSLOT_ALTERNATE_CODEDIRECTORIES +
      CSSLOT_ALTERNATE_CODEDIRECTORY_MAX, ///< one past the last

  CSSLOT_SIGNATURESLOT = 0x10000, ///< CMS Signature
  CSSLOT_IDENTIFICATIONSLOT = 0x10001, ///< Slot index for identification.
  CSSLOT_TICKETSLOT = 0x10002, ///< Slot index for ticket.

  CSTYPE_INDEX_REQUIREMENTS = 0x00000002, ///< compat with amfi
  CSTYPE_INDEX_ENTITLEMENTS = 0x00000005, ///< compat with amfi

  CS_HASHTYPE_SHA1 = 1, ///< SHA-1 hash type.
  CS_HASHTYPE_SHA256 = 2, ///< SHA-256 hash type.
  CS_HASHTYPE_SHA256_TRUNCATED = 3, ///< Truncated SHA-256 hash type.
  CS_HASHTYPE_SHA384 = 4, ///< SHA-384 hash type.

  CS_SHA1_LEN = 20, ///< SHA-1 digest length in bytes.
  CS_SHA256_LEN = 32, ///< SHA-256 digest length in bytes.
  CS_SHA256_TRUNCATED_LEN = 20, ///< Truncated SHA-256 digest length in bytes.

  CS_CDHASH_LEN = 20,    ///< always - larger hashes are truncated
  CS_HASH_MAX_SIZE = 48, ///< max size of the hash we'll support

  /*
   * Currently only to support Legacy VPN plugins, and Mac App Store
   * but intended to replace all the various platform code, dev code etc. bits.
   */
  CS_SIGNER_TYPE_UNKNOWN = 0, ///< Unknown code signer type.
  CS_SIGNER_TYPE_LEGACYVPN = 5, ///< Legacy VPN plugin signer type.
  CS_SIGNER_TYPE_MAC_APP_STORE = 6, ///< Mac App Store signer type.

  CS_SUPPL_SIGNER_TYPE_UNKNOWN = 0, ///< Unknown supplemental signer type.
  CS_SUPPL_SIGNER_TYPE_TRUSTCACHE = 7, ///< Trust-cache supplemental signer type.
  CS_SUPPL_SIGNER_TYPE_LOCAL = 8, ///< Local supplemental signer type.
};

/// CodeDirectory blob describing hashes of signed code pages.
struct CS_CodeDirectory {
  uint32_t magic;         ///< magic number (CSMAGIC_CODEDIRECTORY)
  uint32_t length;        ///< total length of CodeDirectory blob
  uint32_t version;       ///< compatibility version
  uint32_t flags;         ///< setup and mode flags
  uint32_t hashOffset;    ///< offset of hash slot element at index zero
  uint32_t identOffset;   ///< offset of identifier string
  uint32_t nSpecialSlots; ///< number of special hash slots
  uint32_t nCodeSlots;    ///< number of ordinary (code) hash slots
  uint32_t codeLimit;     ///< limit to main image signature range
  uint8_t hashSize;       ///< size of each hash in bytes
  uint8_t hashType;       ///< type of hash (cdHashType* constants)
  uint8_t platform;       ///< platform identifier; zero if not platform binary
  uint8_t pageSize;       ///< log2(page size in bytes); 0 => infinite
  uint32_t spare2;        ///< unused (must be zero)

  /* Version 0x20100 */
  uint32_t scatterOffset; ///< offset of optional scatter vector

  /* Version 0x20200 */
  uint32_t teamOffset; ///< offset of optional team identifier

  /* Version 0x20300 */
  uint32_t spare3;      ///< unused (must be zero)
  uint64_t codeLimit64; ///< limit to main image signature range, 64 bits

  /* Version 0x20400 */
  uint64_t execSegBase;  ///< offset of executable segment
  uint64_t execSegLimit; ///< limit of executable segment
  uint64_t execSegFlags; ///< executable segment flags
};

static_assert(sizeof(CS_CodeDirectory) == 88);

/// Index entry locating one blob inside a CS_SuperBlob.
struct CS_BlobIndex {
  uint32_t type;   ///< type of entry
  uint32_t offset; ///< offset of entry
};

/// Container blob that indexes subordinate code-signing blobs.
struct CS_SuperBlob {
  uint32_t magic;  ///< magic number
  uint32_t length; ///< total length of SuperBlob
  uint32_t count;  ///< number of index entries following
  /* followed by Blobs in no particular order as indicated by index offsets */
};

/// Digest algorithms used by code-signing hashes.
enum SecCSDigestAlgorithm {
  kSecCodeSignatureNoHash = 0,     ///< null value
  kSecCodeSignatureHashSHA1 = 1,   ///< SHA-1
  kSecCodeSignatureHashSHA256 = 2, ///< SHA-256
  /// SHA-256 truncated to 20 bytes.
  kSecCodeSignatureHashSHA256Truncated =
      3,                           ///< SHA-256 truncated to first 20 bytes
  kSecCodeSignatureHashSHA384 = 4, ///< SHA-384
  kSecCodeSignatureHashSHA512 = 5, ///< SHA-512
};

/// Linker optimization hint (LOH) kinds for ARM64.
enum LinkerOptimizationHintKind {
  LOH_ARM64_ADRP_ADRP = 1, ///< ADRP paired with ADRP.
  LOH_ARM64_ADRP_LDR = 2, ///< ADRP paired with LDR.
  LOH_ARM64_ADRP_ADD_LDR = 3, ///< ADRP/ADD paired with LDR.
  LOH_ARM64_ADRP_LDR_GOT_LDR = 4, ///< ADRP/LDR GOT paired with LDR.
  LOH_ARM64_ADRP_ADD_STR = 5, ///< ADRP/ADD paired with STR.
  LOH_ARM64_ADRP_LDR_GOT_STR = 6, ///< ADRP/LDR GOT paired with STR.
  LOH_ARM64_ADRP_ADD = 7, ///< ADRP paired with ADD.
  LOH_ARM64_ADRP_LDR_GOT = 8, ///< ADRP paired with LDR of a GOT slot.
};

} // end namespace MachO
} // end namespace llvm

#endif
