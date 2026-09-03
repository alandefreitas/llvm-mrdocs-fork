//===- MachOYAML.h - Mach-O YAMLIO implementation ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation
/// of Mach-O.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_MACHOYAML_H
#define LLVM_OBJECTYAML_MACHOYAML_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/ObjectYAML/DWARFYAML.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
/// YAML representations of Mach-O object files and fat binaries.
namespace MachOYAML {

/// YAML representation of a Mach-O relocation entry.
struct Relocation {
  /// Offset in the section of the location being relocated.
  llvm::yaml::Hex32 address;
  /// Symbol index if \c is_extern is true, otherwise a section index.
  uint32_t symbolnum;
  /// Whether the relocation is PC-relative.
  bool is_pcrel;
  /// Encoded length where the real length is \c 2 ^ length.
  uint8_t length;
  /// Whether \c symbolnum refers to an external symbol.
  bool is_extern;
  /// Relocation type specific to the target architecture.
  uint8_t type;
  /// Whether this is a scattered relocation.
  bool is_scattered;
  /// Scattered relocation value when \c is_scattered is true.
  int32_t value;
};

/// YAML representation of a Mach-O section.
struct Section {
  /// Section name (up to 16 bytes).
  char sectname[16];
  /// Segment name that contains this section (up to 16 bytes).
  char segname[16];
  /// Virtual memory address of the section.
  llvm::yaml::Hex64 addr;
  /// Size of the section in bytes.
  uint64_t size;
  /// File offset of the section contents.
  llvm::yaml::Hex32 offset;
  /// Section alignment as a power of two.
  uint32_t align;
  /// File offset of the section's relocation entries.
  llvm::yaml::Hex32 reloff;
  /// Number of relocation entries.
  uint32_t nreloc;
  /// Section type and attribute flags.
  llvm::yaml::Hex32 flags;
  /// Reserved field (usage depends on section type).
  llvm::yaml::Hex32 reserved1;
  /// Reserved field (usage depends on section type).
  llvm::yaml::Hex32 reserved2;
  /// Reserved field used by 64-bit section headers.
  llvm::yaml::Hex32 reserved3;
  /// Optional raw section contents.
  std::optional<llvm::yaml::BinaryRef> content;
  /// Relocations that apply to this section.
  std::vector<Relocation> relocations;
};

/// YAML representation of a Mach-O file header.
struct FileHeader {
  /// Mach-O magic number identifying the file format and endianness.
  llvm::yaml::Hex32 magic;
  /// CPU type of the target architecture.
  llvm::yaml::Hex32 cputype;
  /// CPU subtype of the target architecture.
  llvm::yaml::Hex32 cpusubtype;
  /// Mach-O file type (for example, object, executable, or dylib).
  llvm::yaml::Hex32 filetype;
  /// Number of load commands that follow the header.
  uint32_t ncmds;
  /// Total size in bytes of all load commands.
  uint32_t sizeofcmds;
  /// File header flags.
  llvm::yaml::Hex32 flags;
  /// Reserved field used by 64-bit Mach-O headers.
  llvm::yaml::Hex32 reserved;
};

/// YAML representation of a Mach-O load command.
struct LLVM_ABI LoadCommand {
  /// Destroy a load command.
  virtual ~LoadCommand();

  /// Native Mach-O load command payload.
  llvm::MachO::macho_load_command Data;
  /// Sections associated with segment load commands.
  std::vector<Section> Sections;
  /// Build tool versions for build-version load commands.
  std::vector<MachO::build_tool_version> Tools;
  /// Raw payload bytes for otherwise unstructured load commands.
  std::vector<llvm::yaml::Hex8> PayloadBytes;
  /// Optional string content carried by the load command.
  std::string Content;
  /// Number of zero padding bytes appended after the payload.
  uint64_t ZeroPadBytes;
};

/// YAML representation of a Mach-O symbol table (\c nlist) entry.
struct NListEntry {
  /// Index into the string table for the symbol name.
  uint32_t n_strx;
  /// Symbol type and attributes.
  llvm::yaml::Hex8 n_type;
  /// Section ordinal, or zero for symbols not in a section.
  uint8_t n_sect;
  /// Symbol description flags.
  uint16_t n_desc;
  /// Symbol value (address or other meaning depending on type).
  uint64_t n_value;
};

/// YAML representation of a Mach-O dyld rebase opcode.
struct RebaseOpcode {
  /// Rebase opcode enumerator.
  MachO::RebaseOpcode Opcode;
  /// Immediate operand encoded with the opcode.
  uint8_t Imm;
  /// Additional ULEB payloads consumed by the opcode.
  std::vector<yaml::Hex64> ExtraData;
};

/// YAML representation of a Mach-O dyld bind opcode.
struct BindOpcode {
  /// Bind opcode enumerator.
  MachO::BindOpcode Opcode;
  /// Immediate operand encoded with the opcode.
  uint8_t Imm;
  /// Additional unsigned ULEB payloads consumed by the opcode.
  std::vector<yaml::Hex64> ULEBExtraData;
  /// Additional signed SLEB payloads consumed by the opcode.
  std::vector<int64_t> SLEBExtraData;
  /// Symbol name associated with the bind operation.
  StringRef Symbol;
};

/// YAML representation of a node in a Mach-O export trie.
struct ExportEntry {
  /// Size in bytes of the terminal information for this node.
  uint64_t TerminalSize = 0;
  /// Offset of this node within the export trie.
  uint64_t NodeOffset = 0;
  /// Edge name leading to this export node.
  std::string Name;
  /// Export flags for a terminal node.
  llvm::yaml::Hex64 Flags = 0;
  /// Export address for a terminal node.
  llvm::yaml::Hex64 Address = 0;
  /// Additional terminal data whose meaning depends on \c Flags.
  llvm::yaml::Hex64 Other = 0;
  /// Imported symbol name for re-exported symbols.
  std::string ImportName;
  /// Child edges of this export trie node.
  std::vector<MachOYAML::ExportEntry> Children;
};

/// YAML representation of a Mach-O data-in-code entry.
struct DataInCodeEntry {
  /// Offset from the start of the text section to the data region.
  llvm::yaml::Hex32 Offset;
  /// Length in bytes of the data region.
  uint16_t Length;
  /// Kind of data embedded in the code section.
  llvm::yaml::Hex16 Kind;
};

/// YAML representation of Mach-O link-edit segment contents.
struct LinkEditData {
  /// Dyld rebase opcodes.
  std::vector<MachOYAML::RebaseOpcode> RebaseOpcodes;
  /// Dyld bind opcodes.
  std::vector<MachOYAML::BindOpcode> BindOpcodes;
  /// Dyld weak bind opcodes.
  std::vector<MachOYAML::BindOpcode> WeakBindOpcodes;
  /// Dyld lazy bind opcodes.
  std::vector<MachOYAML::BindOpcode> LazyBindOpcodes;
  /// Root of the export trie.
  MachOYAML::ExportEntry ExportTrie;
  /// Symbol table entries.
  std::vector<NListEntry> NameList;
  /// String table contents.
  std::vector<StringRef> StringTable;
  /// Indirect symbol table entries.
  std::vector<yaml::Hex32> IndirectSymbols;
  /// Function starts addresses.
  std::vector<yaml::Hex64> FunctionStarts;
  /// Data-in-code table entries.
  std::vector<DataInCodeEntry> DataInCode;
  /// Raw chained fixups bytes.
  std::vector<yaml::Hex8> ChainedFixups;

  /// Return whether this link-edit data has no populated fields.
  /// \return \c true if every field is empty.
  LLVM_ABI bool isEmpty() const;
};

/// YAML representation of a complete Mach-O object file.
struct Object {
  /// Whether the object uses little-endian encoding.
  bool IsLittleEndian;
  /// Mach-O file header.
  FileHeader Header;
  /// Load commands in the object.
  std::vector<LoadCommand> LoadCommands;
  /// Top-level sections not nested under a load command.
  std::vector<Section> Sections;
  /// Structured link-edit segment contents.
  LinkEditData LinkEdit;
  /// Optional raw \c __LINKEDIT segment bytes.
  std::optional<llvm::yaml::BinaryRef> RawLinkEditSegment;
  /// Optional DWARF debug information embedded with the object.
  DWARFYAML::Data DWARF;
};

/// YAML representation of a Mach-O fat (universal) binary header.
struct FatHeader {
  /// Fat binary magic number.
  llvm::yaml::Hex32 magic;
  /// Number of architecture slices described by the fat header.
  uint32_t nfat_arch;
};

/// YAML representation of one architecture descriptor in a fat binary.
struct FatArch {
  /// CPU type of the slice.
  llvm::yaml::Hex32 cputype;
  /// CPU subtype of the slice.
  llvm::yaml::Hex32 cpusubtype;
  /// File offset of the slice within the fat binary.
  llvm::yaml::Hex64 offset;
  /// Size in bytes of the slice.
  uint64_t size;
  /// Alignment of the slice as a power of two.
  uint32_t align;
  /// Reserved field used by 64-bit fat architecture headers.
  llvm::yaml::Hex32 reserved;
};

/// YAML representation of a Mach-O universal (fat) binary.
struct UniversalBinary {
  /// Fat binary header.
  FatHeader Header;
  /// Architecture descriptors for each slice.
  std::vector<FatArch> FatArchs;
  /// Mach-O object slices contained in the fat binary.
  std::vector<Object> Slices;
};

} // end namespace MachOYAML
} // end namespace llvm

namespace llvm {
namespace yaml {

/// Sequences of Mach-O load commands use block formatting.
template <> struct SequenceElementTraits<llvm::MachOYAML::LoadCommand> {
  /// Emit sequences of Mach-O load commands in block style.
  static const bool flow = false;
};

/// Sequences of Mach-O relocations use block formatting.
template <> struct SequenceElementTraits<llvm::MachOYAML::Relocation> {
  /// Emit sequences of Mach-O relocations in block style.
  static const bool flow = false;
};

/// Sequences of Mach-O sections use block formatting.
template <> struct SequenceElementTraits<llvm::MachOYAML::Section> {
  /// Emit sequences of Mach-O sections in block style.
  static const bool flow = false;
};

/// Sequences of Mach-O rebase opcodes use block formatting.
template <> struct SequenceElementTraits<llvm::MachOYAML::RebaseOpcode> {
  /// Emit sequences of Mach-O rebase opcodes in block style.
  static const bool flow = false;
};

/// Sequences of Mach-O bind opcodes use block formatting.
template <> struct SequenceElementTraits<llvm::MachOYAML::BindOpcode> {
  /// Emit sequences of Mach-O bind opcodes in block style.
  static const bool flow = false;
};

/// Sequences of Mach-O export trie entries use block formatting.
template <> struct SequenceElementTraits<llvm::MachOYAML::ExportEntry> {
  /// Emit sequences of Mach-O export trie entries in block style.
  static const bool flow = false;
};

/// Sequences of Mach-O nlist entries use block formatting.
template <> struct SequenceElementTraits<llvm::MachOYAML::NListEntry> {
  /// Emit sequences of Mach-O nlist entries in block style.
  static const bool flow = false;
};

/// Sequences of Mach-O objects use block formatting.
template <> struct SequenceElementTraits<llvm::MachOYAML::Object> {
  /// Emit sequences of Mach-O objects in block style.
  static const bool flow = false;
};

/// Sequences of Mach-O fat architecture descriptors use block formatting.
template <> struct SequenceElementTraits<llvm::MachOYAML::FatArch> {
  /// Emit sequences of Mach-O fat architecture descriptors in block style.
  static const bool flow = false;
};

/// Sequences of Mach-O data-in-code entries use block formatting.
template <> struct SequenceElementTraits<llvm::MachOYAML::DataInCodeEntry> {
  /// Emit sequences of Mach-O data-in-code entries in block style.
  static const bool flow = false;
};

/// Sequences of Mach-O build tool versions use block formatting.
template <> struct SequenceElementTraits<llvm::MachO::build_tool_version> {
  /// Emit sequences of Mach-O build tool versions in block style.
  static const bool flow = false;
};

} // end namespace yaml
} // end namespace llvm

namespace llvm {

class raw_ostream;

namespace yaml {

/// YAMLIO mapping traits for \c MachOYAML::FileHeader.
template <> struct MappingTraits<MachOYAML::FileHeader> {
  /// Map Mach-O file header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param FileHeader File header being mapped.
  LLVM_ABI static void mapping(IO &IO, MachOYAML::FileHeader &FileHeader);
};

/// YAMLIO mapping traits for \c MachOYAML::Object.
template <> struct MappingTraits<MachOYAML::Object> {
  /// Map Mach-O object fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Object Object being mapped.
  LLVM_ABI static void mapping(IO &IO, MachOYAML::Object &Object);
};

/// YAMLIO mapping traits for \c MachOYAML::FatHeader.
template <> struct MappingTraits<MachOYAML::FatHeader> {
  /// Map Mach-O fat header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param FatHeader Fat header being mapped.
  LLVM_ABI static void mapping(IO &IO, MachOYAML::FatHeader &FatHeader);
};

/// YAMLIO mapping traits for \c MachOYAML::FatArch.
template <> struct MappingTraits<MachOYAML::FatArch> {
  /// Map Mach-O fat architecture fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param FatArch Fat architecture descriptor being mapped.
  LLVM_ABI static void mapping(IO &IO, MachOYAML::FatArch &FatArch);
};

/// YAMLIO mapping traits for \c MachOYAML::UniversalBinary.
template <> struct MappingTraits<MachOYAML::UniversalBinary> {
  /// Map Mach-O universal binary fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param UniversalBinary Universal binary being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               MachOYAML::UniversalBinary &UniversalBinary);
};

/// YAMLIO mapping traits for \c MachOYAML::LoadCommand.
template <> struct MappingTraits<MachOYAML::LoadCommand> {
  /// Map Mach-O load command fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param LoadCommand Load command being mapped.
  LLVM_ABI static void mapping(IO &IO, MachOYAML::LoadCommand &LoadCommand);
};

/// YAMLIO mapping traits for \c MachOYAML::LinkEditData.
template <> struct MappingTraits<MachOYAML::LinkEditData> {
  /// Map Mach-O link-edit data fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param LinkEditData Link-edit data being mapped.
  LLVM_ABI static void mapping(IO &IO, MachOYAML::LinkEditData &LinkEditData);
};

/// YAMLIO mapping traits for \c MachOYAML::RebaseOpcode.
template <> struct MappingTraits<MachOYAML::RebaseOpcode> {
  /// Map Mach-O rebase opcode fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param RebaseOpcode Rebase opcode being mapped.
  LLVM_ABI static void mapping(IO &IO, MachOYAML::RebaseOpcode &RebaseOpcode);
};

/// YAMLIO mapping traits for \c MachOYAML::BindOpcode.
template <> struct MappingTraits<MachOYAML::BindOpcode> {
  /// Map Mach-O bind opcode fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param BindOpcode Bind opcode being mapped.
  LLVM_ABI static void mapping(IO &IO, MachOYAML::BindOpcode &BindOpcode);
};

/// YAMLIO mapping traits for \c MachOYAML::ExportEntry.
template <> struct MappingTraits<MachOYAML::ExportEntry> {
  /// Map Mach-O export trie entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ExportEntry Export trie entry being mapped.
  LLVM_ABI static void mapping(IO &IO, MachOYAML::ExportEntry &ExportEntry);
};

/// YAMLIO mapping traits for \c MachOYAML::Relocation.
template <> struct MappingTraits<MachOYAML::Relocation> {
  /// Map Mach-O relocation fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param R Relocation being mapped.
  LLVM_ABI static void mapping(IO &IO, MachOYAML::Relocation &R);
};

/// YAMLIO mapping traits for \c MachOYAML::Section.
template <> struct MappingTraits<MachOYAML::Section> {
  /// Map Mach-O section fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Section Section being mapped.
  LLVM_ABI static void mapping(IO &IO, MachOYAML::Section &Section);
  /// Validate a mapped Mach-O section.
  /// \param io YAML input/output state.
  /// \param Section Section being validated.
  /// \return Empty string on success, or an error message.
  LLVM_ABI static std::string validate(IO &io, MachOYAML::Section &Section);
};

/// YAMLIO mapping traits for \c MachOYAML::NListEntry.
template <> struct MappingTraits<MachOYAML::NListEntry> {
  /// Map Mach-O nlist entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param NListEntry Nlist entry being mapped.
  LLVM_ABI static void mapping(IO &IO, MachOYAML::NListEntry &NListEntry);
};

/// YAMLIO mapping traits for \c MachO::build_tool_version.
template <> struct MappingTraits<MachO::build_tool_version> {
  /// Map Mach-O build tool version fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param tool Build tool version being mapped.
  LLVM_ABI static void mapping(IO &IO, MachO::build_tool_version &tool);
};

/// YAMLIO mapping traits for \c MachOYAML::DataInCodeEntry.
template <> struct MappingTraits<MachOYAML::DataInCodeEntry> {
  /// Map Mach-O data-in-code entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param DataInCodeEntry Data-in-code entry being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               MachOYAML::DataInCodeEntry &DataInCodeEntry);
};

#define HANDLE_LOAD_COMMAND(LCName, LCValue, LCStruct)                         \
  io.enumCase(value, #LCName, MachO::LCName);

/// YAMLIO scalar enumeration traits for \c MachO::LoadCommandType.
template <> struct ScalarEnumerationTraits<MachO::LoadCommandType> {
  /// Map Mach-O load command type enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Load command type being mapped.
  static void enumeration(IO &io, MachO::LoadCommandType &value) {
#include "llvm/BinaryFormat/MachO.def"
    io.enumFallback<Hex32>(value);
  }
};

#define ENUM_CASE(Enum) io.enumCase(value, #Enum, MachO::Enum);

/// YAMLIO scalar enumeration traits for \c MachO::RebaseOpcode.
template <> struct ScalarEnumerationTraits<MachO::RebaseOpcode> {
  /// Map Mach-O rebase opcode enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Rebase opcode being mapped.
  static void enumeration(IO &io, MachO::RebaseOpcode &value) {
    ENUM_CASE(REBASE_OPCODE_DONE)
    ENUM_CASE(REBASE_OPCODE_SET_TYPE_IMM)
    ENUM_CASE(REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB)
    ENUM_CASE(REBASE_OPCODE_ADD_ADDR_ULEB)
    ENUM_CASE(REBASE_OPCODE_ADD_ADDR_IMM_SCALED)
    ENUM_CASE(REBASE_OPCODE_DO_REBASE_IMM_TIMES)
    ENUM_CASE(REBASE_OPCODE_DO_REBASE_ULEB_TIMES)
    ENUM_CASE(REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB)
    ENUM_CASE(REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB)
    io.enumFallback<Hex8>(value);
  }
};

/// YAMLIO scalar enumeration traits for \c MachO::BindOpcode.
template <> struct ScalarEnumerationTraits<MachO::BindOpcode> {
  /// Map Mach-O bind opcode enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Bind opcode being mapped.
  static void enumeration(IO &io, MachO::BindOpcode &value) {
    ENUM_CASE(BIND_OPCODE_DONE)
    ENUM_CASE(BIND_OPCODE_SET_DYLIB_ORDINAL_IMM)
    ENUM_CASE(BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB)
    ENUM_CASE(BIND_OPCODE_SET_DYLIB_SPECIAL_IMM)
    ENUM_CASE(BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM)
    ENUM_CASE(BIND_OPCODE_SET_TYPE_IMM)
    ENUM_CASE(BIND_OPCODE_SET_ADDEND_SLEB)
    ENUM_CASE(BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB)
    ENUM_CASE(BIND_OPCODE_ADD_ADDR_ULEB)
    ENUM_CASE(BIND_OPCODE_DO_BIND)
    ENUM_CASE(BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB)
    ENUM_CASE(BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED)
    ENUM_CASE(BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB)
    io.enumFallback<Hex8>(value);
  }
};

/// Fixed-size 16-byte character array used for Mach-O string fields.
using char_16 = char[16];

/// YAMLIO scalar traits for \c char_16.
template <> struct ScalarTraits<char_16> {
  /// Write \p Val as a YAML scalar to \p Out.
  /// \param Val 16-byte character array to write.
  /// \param Ctx Optional YAML context pointer.
  /// \param Out Output stream.
  LLVM_ABI static void output(const char_16 &Val, void *Ctx, raw_ostream &Out);
  /// Parse YAML scalar \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Optional YAML context pointer.
  /// \param Val Destination 16-byte character array.
  /// \return Empty string on success, or an error message.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, char_16 &Val);
  /// Return whether \p S must be quoted in YAML.
  /// \param S Scalar text being considered.
  /// \return Quoting requirement for \p S.
  LLVM_ABI static QuotingType mustQuote(StringRef S);
};

/// UUID value type used by Mach-O YAML traits, formatted like otool.
using uuid_t = raw_ostream::uuid_t;

/// YAMLIO scalar traits for \c uuid_t.
template <> struct ScalarTraits<uuid_t> {
  /// Write \p Val as a YAML scalar to \p Out.
  /// \param Val UUID value to write.
  /// \param Ctx Optional YAML context pointer.
  /// \param Out Output stream.
  LLVM_ABI static void output(const uuid_t &Val, void *Ctx, raw_ostream &Out);
  /// Parse YAML scalar \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Optional YAML context pointer.
  /// \param Val Destination UUID value.
  /// \return Empty string on success, or an error message.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, uuid_t &Val);
  /// Return whether \p S must be quoted in YAML.
  /// \param S Scalar text being considered.
  /// \return Quoting requirement for \p S.
  LLVM_ABI static QuotingType mustQuote(StringRef S);
};

// Load Command struct mapping traits

#define LOAD_COMMAND_STRUCT(LCStruct)                                          \
  template <> struct MappingTraits<MachO::LCStruct> {                          \
    static void mapping(IO &IO, MachO::LCStruct &LoadCommand);                 \
  };

#include "llvm/BinaryFormat/MachO.def"

// Extra structures used by load commands
/// YAMLIO mapping traits for \c MachO::dylib.
template <> struct MappingTraits<MachO::dylib> {
  /// Map Mach-O dylib fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param LoadCommand Dylib structure being mapped.
  LLVM_ABI static void mapping(IO &IO, MachO::dylib &LoadCommand);
};

/// YAMLIO mapping traits for \c MachO::fvmlib.
template <> struct MappingTraits<MachO::fvmlib> {
  /// Map Mach-O fixed virtual memory library fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param LoadCommand Fixed VM library structure being mapped.
  LLVM_ABI static void mapping(IO &IO, MachO::fvmlib &LoadCommand);
};

/// YAMLIO mapping traits for \c MachO::section.
template <> struct MappingTraits<MachO::section> {
  /// Map Mach-O 32-bit section header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param LoadCommand Section header being mapped.
  LLVM_ABI static void mapping(IO &IO, MachO::section &LoadCommand);
};

/// YAMLIO mapping traits for \c MachO::section_64.
template <> struct MappingTraits<MachO::section_64> {
  /// Map Mach-O 64-bit section header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param LoadCommand 64-bit section header being mapped.
  LLVM_ABI static void mapping(IO &IO, MachO::section_64 &LoadCommand);
};

} // end namespace yaml

} // end namespace llvm

#endif // LLVM_OBJECTYAML_MACHOYAML_H
