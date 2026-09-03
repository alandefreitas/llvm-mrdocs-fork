//===- DWARFYAML.h - DWARF YAMLIO implementation ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation
/// of DWARF Debug Info.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_DWARFYAML_H
#define LLVM_OBJECTYAML_DWARFYAML_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace llvm {
namespace DWARFYAML {

/// YAML representation of a DWARF attribute abbreviation.
struct AttributeAbbrev {
  /// DWARF attribute name for this abbreviation.
  llvm::dwarf::Attribute Attribute;
  /// DWARF form used to encode the attribute value.
  llvm::dwarf::Form Form;
  /// Optional attribute value used by some DWARF 5 attributes.
  llvm::yaml::Hex64 Value;
};

/// YAML representation of a single DWARF abbreviation declaration.
struct Abbrev {
  /// Abbreviation code identifying this declaration.
  std::optional<yaml::Hex64> Code;
  /// DWARF tag associated with this abbreviation.
  llvm::dwarf::Tag Tag;
  /// Whether DIEs using this abbreviation have children.
  llvm::dwarf::Constants Children;
  /// Attribute abbreviations belonging to this declaration.
  std::vector<AttributeAbbrev> Attributes;
};

/// YAML representation of a DWARF abbreviation table.
struct AbbrevTable {
  /// Optional identifier used to refer to this abbreviation table.
  std::optional<uint64_t> ID;
  /// Abbreviation declarations contained in this table.
  std::vector<Abbrev> Table;
};

/// YAML representation of a single address-range descriptor.
struct ARangeDescriptor {
  /// Starting address of the range.
  llvm::yaml::Hex64 Address;
  /// Length of the range in bytes.
  yaml::Hex64 Length;
};

/// YAML representation of a \c .debug_aranges set.
struct ARange {
  /// DWARF format (32-bit or 64-bit) of the set header.
  dwarf::DwarfFormat Format;
  /// Length of the set, excluding the length field itself.
  std::optional<yaml::Hex64> Length;
  /// Version of the address-range table format.
  uint16_t Version;
  /// Offset of the corresponding compilation unit.
  yaml::Hex64 CuOffset;
  /// Size of an address in this set.
  std::optional<yaml::Hex8> AddrSize;
  /// Size of a segment selector in this set.
  yaml::Hex8 SegSize;
  /// Address-range descriptors in this set.
  std::vector<ARangeDescriptor> Descriptors;
};

/// Class that describes a range list entry, or a base address selection entry
/// within a range list in the .debug_ranges section.
struct RangeEntry {
  /// Low address offset, or the base address for a base-address selection.
  llvm::yaml::Hex64 LowOffset;
  /// High address offset, or zero for a base-address selection entry.
  llvm::yaml::Hex64 HighOffset;
};

/// Class that describes a single range list inside the .debug_ranges section.
struct Ranges {
  /// Optional offset of this range list within \c .debug_ranges.
  std::optional<llvm::yaml::Hex64> Offset;
  /// Optional address size used by entries in this range list.
  std::optional<llvm::yaml::Hex8> AddrSize;
  /// Range-list entries belonging to this list.
  std::vector<RangeEntry> Entries;
};

/// YAML representation of a public-name or public-type table entry.
struct PubEntry {
  /// Offset of the DIE relative to the compilation unit.
  llvm::yaml::Hex32 DieOffset;
  /// GNU-style descriptor byte present in GNU pub sections.
  llvm::yaml::Hex8 Descriptor;
  /// Name of the public symbol or type.
  StringRef Name;
};

/// YAML representation of a DWARF public-names or public-types section.
struct PubSection {
  /// DWARF format (32-bit or 64-bit) of the section header.
  dwarf::DwarfFormat Format;
  /// Length of the section contribution.
  yaml::Hex64 Length;
  /// Version of the pubnames/pubtypes format.
  uint16_t Version;
  /// Offset of the corresponding compilation unit.
  uint32_t UnitOffset;
  /// Size of the corresponding compilation unit.
  uint32_t UnitSize;
  /// Public entries contained in this section.
  std::vector<PubEntry> Entries;
};

/// YAML representation of a DWARF form value.
struct FormValue {
  /// Integer value used for scalar forms.
  llvm::yaml::Hex64 Value;
  /// C-string value used for string forms.
  StringRef CStr;
  /// Raw block bytes used for block forms.
  std::vector<llvm::yaml::Hex8> BlockData;
};

/// YAML representation of a DIE entry within a DWARF unit.
struct Entry {
  /// Abbreviation code selecting the DIE layout.
  llvm::yaml::Hex32 AbbrCode;
  /// Attribute values for the selected abbreviation.
  std::vector<FormValue> Values;
};

/// Class that contains helpful context information when mapping YAML into DWARF
/// data structures.
struct DWARFContext {
  /// Whether the current pub section uses the GNU pubnames/pubtypes format.
  bool IsGNUPubSec = false;
};

/// YAML representation of a DWARF compilation, type, or split unit.
struct Unit {
  /// DWARF format (32-bit or 64-bit) of the unit header.
  dwarf::DwarfFormat Format;
  /// Length of the unit, excluding the length field itself.
  std::optional<yaml::Hex64> Length;
  /// DWARF version of this unit.
  uint16_t Version;
  /// Size of an address in this unit.
  std::optional<uint8_t> AddrSize;
  /// Unit type code added in DWARF 5.
  llvm::dwarf::UnitType Type;
  /// Optional identifier of the abbreviation table used by this unit.
  std::optional<uint64_t> AbbrevTableID;
  /// Offset of the abbreviation table within \c .debug_abbrev.
  std::optional<yaml::Hex64> AbbrOffset;
  /// Type signature or DWO ID for type or split units.
  yaml::Hex64 TypeSignatureOrDwoID;
  /// Offset of the type DIE for type units.
  yaml::Hex64 TypeOffset;

  /// DIE entries belonging to this unit.
  std::vector<Entry> Entries;
};

/// YAML representation of a DWARF name-index attribute form pair.
struct IdxForm {
  /// Name-index attribute kind.
  dwarf::Index Idx;
  /// Form used to encode the index attribute.
  dwarf::Form Form;
};

/// YAML representation of a \c .debug_names abbreviation.
struct DebugNameAbbreviation {
  /// Abbreviation code identifying this name-index abbreviation.
  yaml::Hex64 Code;
  /// DWARF tag associated with this abbreviation.
  dwarf::Tag Tag;
  /// Index attributes described by this abbreviation.
  std::vector<IdxForm> Indices;
};

/// YAML representation of a \c .debug_names entry.
struct DebugNameEntry {
  /// Offset of the name string within \c .debug_str.
  yaml::Hex32 NameStrp;
  /// Abbreviation code selecting the entry layout.
  yaml::Hex64 Code;
  /// Index attribute values for this entry.
  std::vector<yaml::Hex64> Values;
};

/// YAML representation of a \c .debug_names section.
struct DebugNamesSection {
  /// Abbreviations used by name-index entries.
  std::vector<DebugNameAbbreviation> Abbrevs;
  /// Name-index entries in the section.
  std::vector<DebugNameEntry> Entries;
};

/// YAML representation of a DWARF line-table file entry (DWARF v4 and earlier).
struct File {
  /// File name.
  StringRef Name;
  /// Index into the include-directories table.
  uint64_t DirIdx;
  /// Modification time of the file.
  uint64_t ModTime;
  /// Length of the file in bytes.
  uint64_t Length;
};

/// YAML representation of a line-number content-type and form pair.
struct LnctForm {
  /// Line-number content type being described.
  dwarf::LineNumberEntryFormat ContentType;
  /// Form used to encode the content value.
  dwarf::Form Form;
};

/// YAML representation of a DWARF line-number program opcode.
struct LineTableOpcode {
  /// Primary opcode for this line-number instruction.
  dwarf::LineNumberOps Opcode;
  /// Length of an extended opcode operand sequence, when present.
  std::optional<uint64_t> ExtLen;
  /// Extended opcode discriminator, when \c Opcode is extended.
  dwarf::LineNumberExtendedOps SubOpcode;
  /// Unsigned immediate operand for standard or special opcodes.
  uint64_t Data;
  /// Signed immediate operand for opcodes that require one.
  int64_t SData;
  /// File entry operand for \c DW_LNE_define_file.
  File FileEntry;
  /// Operand bytes for an unrecognized opcode.
  std::vector<llvm::yaml::Hex8> UnknownOpcodeData;
  /// Operand values for a recognized standard opcode.
  std::vector<llvm::yaml::Hex64> StandardOpcodeData;
};

/// YAML representation of a DWARF line-number program.
struct LineTable {
  /// DWARF format (32-bit or 64-bit) of the line table header.
  dwarf::DwarfFormat Format;
  /// Length of the line-number program contribution.
  std::optional<uint64_t> Length;
  /// Version of the line-number program format.
  uint16_t Version;
  /// Size of an address in this line table.
  uint8_t AddressSize;
  /// Size of a segment selector in this line table.
  uint8_t SegmentSelectorSize;
  /// Length of the line-table prologue.
  std::optional<uint64_t> PrologueLength;
  /// Minimum instruction length.
  uint8_t MinInstLength;
  /// Maximum number of operations per instruction.
  uint8_t MaxOpsPerInst;
  /// Default value of the \c is_stmt register.
  uint8_t DefaultIsStmt;
  /// Base value used when computing special opcodes.
  uint8_t LineBase;
  /// Range value used when computing special opcodes.
  uint8_t LineRange;
  /// First special opcode value.
  std::optional<uint8_t> OpcodeBase;
  /// Number of operands for each standard opcode.
  std::optional<std::vector<uint8_t>> StandardOpcodeLengths;

  // For DWARF<=v4
  /// Include directories used by DWARF v4 and earlier file entries.
  std::vector<StringRef> IncludeDirs;
  /// File entries used by DWARF v4 and earlier line tables.
  std::vector<File> Files;

  // For DWARF>=v5
  /// Number of directory entry format descriptors (DWARF v5).
  uint8_t DirectoryEntryFormatCount;
  /// Directory entry format descriptors (DWARF v5).
  std::vector<LnctForm> DirectoryEntryFormat;
  /// Number of directories in the DWARF v5 directories table.
  uint64_t DirectoriesCount;
  /// Directory entries encoded according to \c DirectoryEntryFormat.
  std::vector<std::vector<FormValue>> Directories;
  /// Number of file-name entry format descriptors (DWARF v5).
  uint8_t FileNameEntryFormatCount;
  /// File-name entry format descriptors (DWARF v5).
  std::vector<LnctForm> FileNameEntryFormat;
  /// Number of file names in the DWARF v5 file-names table.
  uint64_t FileNamesCount;
  /// File-name entries encoded according to \c FileNameEntryFormat.
  std::vector<std::vector<FormValue>> FileNames;

  /// Line-number program opcodes following the prologue.
  std::vector<LineTableOpcode> Opcodes;
};

/// YAML representation of a segment and address pair.
struct SegAddrPair {
  /// Segment selector value.
  yaml::Hex64 Segment;
  /// Address within the segment.
  yaml::Hex64 Address;
};

/// YAML representation of a \c .debug_addr table contribution.
struct AddrTableEntry {
  /// DWARF format (32-bit or 64-bit) of the address table header.
  dwarf::DwarfFormat Format;
  /// Length of the address table contribution.
  std::optional<yaml::Hex64> Length;
  /// Version of the address table format.
  yaml::Hex16 Version;
  /// Size of an address in this table.
  std::optional<yaml::Hex8> AddrSize;
  /// Size of a segment selector in this table.
  yaml::Hex8 SegSelectorSize;
  /// Segment/address pairs stored in this table.
  std::vector<SegAddrPair> SegAddrPairs;
};

/// YAML representation of a \c .debug_str_offsets table contribution.
struct StringOffsetsTable {
  /// DWARF format (32-bit or 64-bit) of the string-offsets header.
  dwarf::DwarfFormat Format;
  /// Length of the string-offsets contribution.
  std::optional<yaml::Hex64> Length;
  /// Version of the string-offsets table format.
  yaml::Hex16 Version;
  /// Padding bytes following the version field.
  yaml::Hex16 Padding;
  /// Offsets into the string table.
  std::vector<yaml::Hex64> Offsets;
};

/// YAML representation of a DWARF location expression operation.
struct DWARFOperation {
  /// Location expression opcode.
  dwarf::LocationAtom Operator;
  /// Operands consumed by \c Operator.
  std::vector<yaml::Hex64> Values;
};

/// YAML representation of a DWARF range-list entry.
struct RnglistEntry {
  /// Range-list entry kind.
  dwarf::RnglistEntries Operator;
  /// Operands consumed by \c Operator.
  std::vector<yaml::Hex64> Values;
};

/// YAML representation of a DWARF location-list entry.
struct LoclistEntry {
  /// Location-list entry kind.
  dwarf::LoclistEntries Operator;
  /// Operands consumed by \c Operator.
  std::vector<yaml::Hex64> Values;
  /// Optional length of the location description that follows.
  std::optional<yaml::Hex64> DescriptionsLength;
  /// Location description operations for this entry.
  std::vector<DWARFOperation> Descriptions;
};

/// YAML representation of a DWARF list (range or location) within a list table.
template <typename EntryType> struct ListEntries {
  /// Structured list entries, when present.
  std::optional<std::vector<EntryType>> Entries;
  /// Raw binary content used instead of structured entries.
  std::optional<yaml::BinaryRef> Content;
};

/// YAML representation of a DWARF range-list or location-list table.
template <typename EntryType> struct ListTable {
  /// DWARF format (32-bit or 64-bit) of the list table header.
  dwarf::DwarfFormat Format;
  /// Length of the list table contribution.
  std::optional<yaml::Hex64> Length;
  /// Version of the list table format.
  yaml::Hex16 Version;
  /// Size of an address in this table.
  std::optional<yaml::Hex8> AddrSize;
  /// Size of a segment selector in this table.
  yaml::Hex8 SegSelectorSize;
  /// Number of offset entries in the table's offset array.
  std::optional<uint32_t> OffsetEntryCount;
  /// Offsets of individual lists within the table.
  std::optional<std::vector<yaml::Hex64>> Offsets;
  /// Individual lists contained in this table.
  std::vector<ListEntries<EntryType>> Lists;
};

/// YAML representation of collected DWARF debug sections.
struct Data {
  /// Whether multi-byte values are little-endian.
  bool IsLittleEndian;
  /// Whether addresses are 64-bit rather than 32-bit.
  bool Is64BitAddrSize;
  /// Contents of the \c .debug_abbrev section.
  std::vector<AbbrevTable> DebugAbbrev;
  /// Contents of the \c .debug_str section.
  std::optional<std::vector<StringRef>> DebugStrings;
  /// Contents of the \c .debug_str_offsets section.
  std::optional<std::vector<StringOffsetsTable>> DebugStrOffsets;
  /// Contents of the \c .debug_aranges section.
  std::optional<std::vector<ARange>> DebugAranges;
  /// Contents of the \c .debug_ranges section.
  std::optional<std::vector<Ranges>> DebugRanges;
  /// Contents of the \c .debug_addr section.
  std::optional<std::vector<AddrTableEntry>> DebugAddr;
  /// Contents of the \c .debug_pubnames section.
  std::optional<PubSection> PubNames;
  /// Contents of the \c .debug_pubtypes section.
  std::optional<PubSection> PubTypes;

  /// Contents of the \c .debug_gnu_pubnames section.
  std::optional<PubSection> GNUPubNames;
  /// Contents of the \c .debug_gnu_pubtypes section.
  std::optional<PubSection> GNUPubTypes;

  /// Compilation, type, and split units from \c .debug_info / \c .debug_types.
  std::vector<Unit> Units;

  /// Contents of the \c .debug_line section.
  std::vector<LineTable> DebugLines;
  /// Contents of the \c .debug_rnglists section.
  std::optional<std::vector<ListTable<RnglistEntry>>> DebugRnglists;
  /// Contents of the \c .debug_loclists section.
  std::optional<std::vector<ListTable<LoclistEntry>>> DebugLoclists;
  /// Contents of the \c .debug_names section.
  std::optional<DebugNamesSection> DebugNames;

  /// Return whether no DWARF sections are present.
  /// \return \c true if every section field is empty or unset.
  LLVM_ABI bool isEmpty() const;

  /// Return the names of DWARF sections that contain data.
  /// \return Set of non-empty section names in stable order.
  LLVM_ABI SetVector<StringRef> getNonEmptySectionNames() const;

  /// Cached index and offset of an abbreviation table.
  struct AbbrevTableInfo {
    /// Index of the abbreviation table within \c DebugAbbrev.
    uint64_t Index;
    /// Byte offset of the abbreviation table in the emitted section.
    uint64_t Offset;
  };
  /// Look up abbreviation table index and offset by table ID.
  /// \param ID Abbreviation table identifier to find.
  /// \return Index and offset of the matching table, or an error.
  LLVM_ABI Expected<AbbrevTableInfo> getAbbrevTableInfoByID(uint64_t ID) const;
  /// Return the serialized content of an abbreviation table by index.
  /// \param Index Index into \c DebugAbbrev.
  /// \return Serialized abbreviation table bytes.
  LLVM_ABI StringRef getAbbrevTableContentByIndex(uint64_t Index) const;

private:
  mutable DenseMap<uint64_t, AbbrevTableInfo> AbbrevTableInfoMap;
  // getAbbrevTableContentByIndex returns StringRefs into the values. DenseMap
  // cannot be used.
  mutable std::unordered_map<uint64_t, std::string> AbbrevTableContents;
};

} // end namespace DWARFYAML
} // end namespace llvm

namespace llvm {
namespace yaml {

/// Sequences of DWARF attribute abbreviations use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::AttributeAbbrev> {
  /// Emit sequences of DWARF attribute abbreviations in block style.
  static const bool flow = false;
};

/// Sequences of DWARF abbreviations use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::Abbrev> {
  /// Emit sequences of DWARF abbreviations in block style.
  static const bool flow = false;
};

/// Sequences of DWARF abbreviation tables use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::AbbrevTable> {
  /// Emit sequences of DWARF abbreviation tables in block style.
  static const bool flow = false;
};

/// Sequences of address-range descriptors use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::ARangeDescriptor> {
  /// Emit sequences of address-range descriptors in block style.
  static const bool flow = false;
};

/// Sequences of address-range sets use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::ARange> {
  /// Emit sequences of address-range sets in block style.
  static const bool flow = false;
};

/// Sequences of range-list entries use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::RangeEntry> {
  /// Emit sequences of range-list entries in block style.
  static const bool flow = false;
};

/// Sequences of range lists use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::Ranges> {
  /// Emit sequences of range lists in block style.
  static const bool flow = false;
};

/// Sequences of public-name entries use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::PubEntry> {
  /// Emit sequences of public-name entries in block style.
  static const bool flow = false;
};

/// Sequences of DWARF units use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::Unit> {
  /// Emit sequences of DWARF units in block style.
  static const bool flow = false;
};

/// Sequences of DWARF form values use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::FormValue> {
  /// Emit sequences of DWARF form values in block style.
  static const bool flow = false;
};

/// Sequences of form-value vectors use block formatting.
template <>
struct SequenceElementTraits<std::vector<llvm::DWARFYAML::FormValue>> {
  /// Emit sequences of form-value vectors in block style.
  static const bool flow = false;
};

/// Sequences of DIE entries use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::Entry> {
  /// Emit sequences of DIE entries in block style.
  static const bool flow = false;
};

/// Sequences of line-table file entries use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::File> {
  /// Emit sequences of line-table file entries in block style.
  static const bool flow = false;
};

/// Sequences of line-number content-type forms use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::LnctForm> {
  /// Emit sequences of line-number content-type forms in block style.
  static const bool flow = false;
};

/// Sequences of line tables use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::LineTable> {
  /// Emit sequences of line tables in block style.
  static const bool flow = false;
};

/// Sequences of line-table opcodes use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::LineTableOpcode> {
  /// Emit sequences of line-table opcodes in block style.
  static const bool flow = false;
};

/// Sequences of segment/address pairs use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::SegAddrPair> {
  /// Emit sequences of segment/address pairs in block style.
  static const bool flow = false;
};

/// Sequences of address table contributions use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::AddrTableEntry> {
  /// Emit sequences of address table contributions in block style.
  static const bool flow = false;
};

/// Sequences of string-offsets tables use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::StringOffsetsTable> {
  /// Emit sequences of string-offsets tables in block style.
  static const bool flow = false;
};

/// Sequences of range-list tables use block formatting.
template <>
struct SequenceElementTraits<
    llvm::DWARFYAML::ListTable<DWARFYAML::RnglistEntry>> {
  /// Emit sequences of range-list tables in block style.
  static const bool flow = false;
};

/// Sequences of range-list entry groups use block formatting.
template <>
struct SequenceElementTraits<
    llvm::DWARFYAML::ListEntries<DWARFYAML::RnglistEntry>> {
  /// Emit sequences of range-list entry groups in block style.
  static const bool flow = false;
};

/// Sequences of range-list entries use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::RnglistEntry> {
  /// Emit sequences of range-list entries in block style.
  static const bool flow = false;
};

/// Sequences of location-list tables use block formatting.
template <>
struct SequenceElementTraits<
    llvm::DWARFYAML::ListTable<DWARFYAML::LoclistEntry>> {
  /// Emit sequences of location-list tables in block style.
  static const bool flow = false;
};

/// Sequences of location-list entry groups use block formatting.
template <>
struct SequenceElementTraits<
    llvm::DWARFYAML::ListEntries<DWARFYAML::LoclistEntry>> {
  /// Emit sequences of location-list entry groups in block style.
  static const bool flow = false;
};

/// Sequences of location-list entries use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::LoclistEntry> {
  /// Emit sequences of location-list entries in block style.
  static const bool flow = false;
};

/// Sequences of DWARF location operations use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::DWARFOperation> {
  /// Emit sequences of DWARF location operations in block style.
  static const bool flow = false;
};

/// Sequences of debug-names entries use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::DebugNameEntry> {
  /// Emit sequences of debug-names entries in block style.
  static const bool flow = false;
};

/// Sequences of debug-names abbreviations use block formatting.
template <>
struct SequenceElementTraits<llvm::DWARFYAML::DebugNameAbbreviation> {
  /// Emit sequences of debug-names abbreviations in block style.
  static const bool flow = false;
};

/// Sequences of name-index form pairs use block formatting.
template <> struct SequenceElementTraits<llvm::DWARFYAML::IdxForm> {
  /// Emit sequences of name-index form pairs in block style.
  static const bool flow = false;
};

/// YAMLIO mapping traits for \c DWARFYAML::Data.
template <> struct MappingTraits<DWARFYAML::Data> {
  /// Map DWARF debug section fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param DWARF Collected DWARF data being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::Data &DWARF);
};

/// YAMLIO mapping traits for \c DWARFYAML::AbbrevTable.
template <> struct MappingTraits<DWARFYAML::AbbrevTable> {
  /// Map abbreviation table fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param AbbrevTable Abbreviation table being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::AbbrevTable &AbbrevTable);
};

/// YAMLIO mapping traits for \c DWARFYAML::Abbrev.
template <> struct MappingTraits<DWARFYAML::Abbrev> {
  /// Map abbreviation declaration fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Abbrev Abbreviation declaration being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::Abbrev &Abbrev);
};

/// YAMLIO mapping traits for \c DWARFYAML::AttributeAbbrev.
template <> struct MappingTraits<DWARFYAML::AttributeAbbrev> {
  /// Map attribute abbreviation fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param AttAbbrev Attribute abbreviation being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::AttributeAbbrev &AttAbbrev);
};

/// YAMLIO mapping traits for \c DWARFYAML::ARangeDescriptor.
template <> struct MappingTraits<DWARFYAML::ARangeDescriptor> {
  /// Map address-range descriptor fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Descriptor Address-range descriptor being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::ARangeDescriptor &Descriptor);
};

/// YAMLIO mapping traits for \c DWARFYAML::ARange.
template <> struct MappingTraits<DWARFYAML::ARange> {
  /// Map address-range set fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ARange Address-range set being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::ARange &ARange);
};

/// YAMLIO mapping traits for \c DWARFYAML::RangeEntry.
template <> struct MappingTraits<DWARFYAML::RangeEntry> {
  /// Map range-list entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Entry Range-list entry being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::RangeEntry &Entry);
};

/// YAMLIO mapping traits for \c DWARFYAML::Ranges.
template <> struct MappingTraits<DWARFYAML::Ranges> {
  /// Map range-list fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Ranges Range list being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::Ranges &Ranges);
};

/// YAMLIO mapping traits for \c DWARFYAML::PubEntry.
template <> struct MappingTraits<DWARFYAML::PubEntry> {
  /// Map public-name entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Entry Public-name entry being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::PubEntry &Entry);
};

/// YAMLIO mapping traits for \c DWARFYAML::PubSection.
template <> struct MappingTraits<DWARFYAML::PubSection> {
  /// Map public-names or public-types section fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Section Public section being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::PubSection &Section);
};

/// YAMLIO mapping traits for \c DWARFYAML::Unit.
template <> struct MappingTraits<DWARFYAML::Unit> {
  /// Map DWARF unit fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Unit DWARF unit being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::Unit &Unit);
};

/// YAMLIO mapping traits for \c DWARFYAML::DebugNamesSection.
template <> struct MappingTraits<DWARFYAML::DebugNamesSection> {
  /// Map debug-names section fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param DebugNames Debug-names section being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               DWARFYAML::DebugNamesSection &DebugNames);
};
/// YAMLIO mapping traits for \c DWARFYAML::DebugNameEntry.
template <> struct MappingTraits<DWARFYAML::DebugNameEntry> {
  /// Map debug-names entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Entry Debug-names entry being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::DebugNameEntry &Entry);
};
/// YAMLIO mapping traits for \c DWARFYAML::DebugNameAbbreviation.
template <> struct MappingTraits<DWARFYAML::DebugNameAbbreviation> {
  /// Map debug-names abbreviation fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Abbrev Debug-names abbreviation being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               DWARFYAML::DebugNameAbbreviation &Abbrev);
};
/// YAMLIO mapping traits for \c DWARFYAML::IdxForm.
template <> struct MappingTraits<DWARFYAML::IdxForm> {
  /// Map name-index form pair fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param IdxForm Name-index form pair being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::IdxForm &IdxForm);
};

/// YAMLIO mapping traits for \c DWARFYAML::Entry.
template <> struct MappingTraits<DWARFYAML::Entry> {
  /// Map DIE entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Entry DIE entry being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::Entry &Entry);
};

/// YAMLIO mapping traits for \c DWARFYAML::FormValue.
template <> struct MappingTraits<DWARFYAML::FormValue> {
  /// Map form-value fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param FormValue Form value being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::FormValue &FormValue);
};

/// YAMLIO mapping traits for \c DWARFYAML::File.
template <> struct MappingTraits<DWARFYAML::File> {
  /// Map line-table file entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param File File entry being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::File &File);
};

/// YAMLIO mapping traits for \c DWARFYAML::LnctForm.
template <> struct MappingTraits<DWARFYAML::LnctForm> {
  /// Map line-number content-type form fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param LnctForm Content-type form pair being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::LnctForm &LnctForm);
};

/// YAMLIO mapping traits for \c DWARFYAML::LineTableOpcode.
template <> struct MappingTraits<DWARFYAML::LineTableOpcode> {
  /// Map line-table opcode fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param LineTableOpcode Line-table opcode being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               DWARFYAML::LineTableOpcode &LineTableOpcode);
};

/// YAMLIO mapping traits for \c DWARFYAML::LineTable.
template <> struct MappingTraits<DWARFYAML::LineTable> {
  /// Map line-table fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param LineTable Line table being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::LineTable &LineTable);
};

/// YAMLIO mapping traits for \c DWARFYAML::SegAddrPair.
template <> struct MappingTraits<DWARFYAML::SegAddrPair> {
  /// Map segment/address pair fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param SegAddrPair Segment/address pair being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::SegAddrPair &SegAddrPair);
};

/// YAMLIO mapping traits for \c DWARFYAML::DWARFOperation.
template <> struct MappingTraits<DWARFYAML::DWARFOperation> {
  /// Map location-expression operation fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param DWARFOperation Location operation being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               DWARFYAML::DWARFOperation &DWARFOperation);
};

/// YAMLIO mapping traits for \c DWARFYAML::ListTable.
template <typename EntryType>
struct MappingTraits<DWARFYAML::ListTable<EntryType>> {
  /// Map list-table fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ListTable List table being mapped.
  static void mapping(IO &IO, DWARFYAML::ListTable<EntryType> &ListTable);
};

/// YAMLIO mapping traits for \c DWARFYAML::ListEntries.
template <typename EntryType>
struct MappingTraits<DWARFYAML::ListEntries<EntryType>> {
  /// Map list-entry group fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ListEntries List-entry group being mapped.
  static void mapping(IO &IO, DWARFYAML::ListEntries<EntryType> &ListEntries);
  /// Validate that a list-entry group does not set conflicting fields.
  /// \param IO YAML input/output state.
  /// \param ListEntries List-entry group being validated.
  /// \return Empty string on success, or an error message.
  static std::string validate(IO &IO,
                              DWARFYAML::ListEntries<EntryType> &ListEntries);
};

/// YAMLIO mapping traits for \c DWARFYAML::RnglistEntry.
template <> struct MappingTraits<DWARFYAML::RnglistEntry> {
  /// Map range-list entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param RnglistEntry Range-list entry being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::RnglistEntry &RnglistEntry);
};

/// YAMLIO mapping traits for \c DWARFYAML::LoclistEntry.
template <> struct MappingTraits<DWARFYAML::LoclistEntry> {
  /// Map location-list entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param LoclistEntry Location-list entry being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::LoclistEntry &LoclistEntry);
};

/// YAMLIO mapping traits for \c DWARFYAML::AddrTableEntry.
template <> struct MappingTraits<DWARFYAML::AddrTableEntry> {
  /// Map address-table contribution fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param AddrTable Address-table contribution being mapped.
  LLVM_ABI static void mapping(IO &IO, DWARFYAML::AddrTableEntry &AddrTable);
};

/// YAMLIO mapping traits for \c DWARFYAML::StringOffsetsTable.
template <> struct MappingTraits<DWARFYAML::StringOffsetsTable> {
  /// Map string-offsets table fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param StrOffsetsTable String-offsets table being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               DWARFYAML::StringOffsetsTable &StrOffsetsTable);
};

/// YAMLIO scalar enumeration traits for \c dwarf::DwarfFormat.
template <> struct ScalarEnumerationTraits<dwarf::DwarfFormat> {
  /// Map DWARF format enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Format DWARF format being mapped.
  static void enumeration(IO &IO, dwarf::DwarfFormat &Format) {
    IO.enumCase(Format, "DWARF32", dwarf::DWARF32);
    IO.enumCase(Format, "DWARF64", dwarf::DWARF64);
  }
};

#define HANDLE_DW_TAG(unused, name, unused2, unused3, unused4)                 \
  io.enumCase(value, "DW_TAG_" #name, dwarf::DW_TAG_##name);

/// YAMLIO scalar enumeration traits for \c dwarf::Tag.
template <> struct ScalarEnumerationTraits<dwarf::Tag> {
  /// Map DWARF tag enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value DWARF tag being mapped.
  static void enumeration(IO &io, dwarf::Tag &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_LNS(unused, name)                                            \
  io.enumCase(value, "DW_LNS_" #name, dwarf::DW_LNS_##name);

/// YAMLIO scalar enumeration traits for \c dwarf::LineNumberOps.
template <> struct ScalarEnumerationTraits<dwarf::LineNumberOps> {
  /// Map DWARF line-number standard opcode enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Line-number opcode being mapped.
  static void enumeration(IO &io, dwarf::LineNumberOps &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex8>(value);
  }
};

#define HANDLE_DW_LNE(unused, name)                                            \
  io.enumCase(value, "DW_LNE_" #name, dwarf::DW_LNE_##name);

/// YAMLIO scalar enumeration traits for \c dwarf::LineNumberExtendedOps.
template <> struct ScalarEnumerationTraits<dwarf::LineNumberExtendedOps> {
  /// Map DWARF line-number extended opcode enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Extended line-number opcode being mapped.
  static void enumeration(IO &io, dwarf::LineNumberExtendedOps &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_LNCT(unused, name)                                           \
  io.enumCase(value, "DW_LNCT_" #name, dwarf::DW_LNCT_##name);

/// YAMLIO scalar enumeration traits for \c dwarf::LineNumberEntryFormat.
template <> struct ScalarEnumerationTraits<dwarf::LineNumberEntryFormat> {
  /// Map DWARF line-number entry content-type enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Line-number entry content type being mapped.
  static void enumeration(IO &io, dwarf::LineNumberEntryFormat &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_AT(unused, name, unused2, unused3)                           \
  io.enumCase(value, "DW_AT_" #name, dwarf::DW_AT_##name);

/// YAMLIO scalar enumeration traits for \c dwarf::Attribute.
template <> struct ScalarEnumerationTraits<dwarf::Attribute> {
  /// Map DWARF attribute enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value DWARF attribute being mapped.
  static void enumeration(IO &io, dwarf::Attribute &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_FORM(unused, name, unused2, unused3)                         \
  io.enumCase(value, "DW_FORM_" #name, dwarf::DW_FORM_##name);

/// YAMLIO scalar enumeration traits for \c dwarf::Form.
template <> struct ScalarEnumerationTraits<dwarf::Form> {
  /// Map DWARF form enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value DWARF form being mapped.
  static void enumeration(IO &io, dwarf::Form &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_IDX(unused, name)                                            \
  io.enumCase(value, "DW_IDX_" #name, dwarf::DW_IDX_##name);

/// YAMLIO scalar enumeration traits for \c dwarf::Index.
template <> struct ScalarEnumerationTraits<dwarf::Index> {
  /// Map DWARF name-index attribute enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Name-index attribute being mapped.
  static void enumeration(IO &io, dwarf::Index &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_UT(unused, name)                                             \
  io.enumCase(value, "DW_UT_" #name, dwarf::DW_UT_##name);

/// YAMLIO scalar enumeration traits for \c dwarf::UnitType.
template <> struct ScalarEnumerationTraits<dwarf::UnitType> {
  /// Map DWARF unit-type enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Unit type being mapped.
  static void enumeration(IO &io, dwarf::UnitType &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<Hex8>(value);
  }
};

/// YAMLIO scalar enumeration traits for \c dwarf::Constants.
template <> struct ScalarEnumerationTraits<dwarf::Constants> {
  /// Map DWARF children-flag enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Children flag being mapped.
  static void enumeration(IO &io, dwarf::Constants &value) {
    io.enumCase(value, "DW_CHILDREN_no", dwarf::DW_CHILDREN_no);
    io.enumCase(value, "DW_CHILDREN_yes", dwarf::DW_CHILDREN_yes);
    io.enumFallback<Hex16>(value);
  }
};

#define HANDLE_DW_RLE(unused, name)                                            \
  io.enumCase(value, "DW_RLE_" #name, dwarf::DW_RLE_##name);

/// YAMLIO scalar enumeration traits for \c dwarf::RnglistEntries.
template <> struct ScalarEnumerationTraits<dwarf::RnglistEntries> {
  /// Map DWARF range-list entry enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Range-list entry kind being mapped.
  static void enumeration(IO &io, dwarf::RnglistEntries &value) {
#include "llvm/BinaryFormat/Dwarf.def"
  }
};

#define HANDLE_DW_LLE(unused, name)                                            \
  io.enumCase(value, "DW_LLE_" #name, dwarf::DW_LLE_##name);

/// YAMLIO scalar enumeration traits for \c dwarf::LoclistEntries.
template <> struct ScalarEnumerationTraits<dwarf::LoclistEntries> {
  /// Map DWARF location-list entry enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Location-list entry kind being mapped.
  static void enumeration(IO &io, dwarf::LoclistEntries &value) {
#include "llvm/BinaryFormat/Dwarf.def"
  }
};

#define HANDLE_DW_OP(id, name, operands, arity, version, vendor)               \
  io.enumCase(value, "DW_OP_" #name, dwarf::DW_OP_##name);

/// YAMLIO scalar enumeration traits for \c dwarf::LocationAtom.
template <> struct ScalarEnumerationTraits<dwarf::LocationAtom> {
  /// Map DWARF location-expression opcode enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Location-expression opcode being mapped.
  static void enumeration(IO &io, dwarf::LocationAtom &value) {
#include "llvm/BinaryFormat/Dwarf.def"
    io.enumFallback<yaml::Hex8>(value);
  }
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_DWARFYAML_H
