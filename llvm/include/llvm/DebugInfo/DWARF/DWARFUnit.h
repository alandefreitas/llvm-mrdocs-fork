//===- DWARFUnit.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFUNIT_H
#define LLVM_DEBUGINFO_DWARF_DWARFUNIT_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFLocationExpression.h"
#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataExtractor.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

/// A set of DWARF abbreviation declarations.
class DWARFAbbreviationDeclarationSet;
class DWARFContext;
/// Parsed .debug_abbrev abbreviations for a DWARF object.
class DWARFDebugAbbrev;
class DWARFUnit;
/// A DWARF .debug_ranges range list.
class DWARFDebugRangeList;
/// Table of DWARF location list entries for a unit.
class DWARFLocationTable;
class DWARFObject;
class raw_ostream;
struct DIDumpOptions;
struct DWARFSection;
/// DWARF linker facilities.
namespace dwarf_linker {
/// Parallel DWARF linker implementation.
namespace parallel {
/// A DWARF compile unit specialization used by the parallel linker.
class CompileUnit;
}
} // namespace dwarf_linker

/// Base class describing the header of any kind of DWARF unit.
///
/// Some information is specific to certain unit types. We separate this class
/// out so we can parse the header before deciding what specific kind of unit
/// to construct.
class DWARFUnitHeader {
  // Offset within section.
  uint64_t Offset = 0;
  // Version, address size, and DWARF format.
  dwarf::FormParams FormParams;
  uint64_t Length = 0;
  uint64_t AbbrOffset = 0;

  // For DWO units only.
  const DWARFUnitIndex::Entry *IndexEntry = nullptr;

  // For type units only.
  uint64_t TypeHash = 0;
  uint64_t TypeOffset = 0;

  // For v5 split or skeleton compile units only.
  std::optional<uint64_t> DWOId;

  // Unit type as parsed, or derived from the section kind.
  uint8_t UnitType = 0;

  // Size as parsed. uint8_t for compactness.
  uint8_t Size = 0;

public:
  /// Parse a unit header from \p debug_info starting at \p offset_ptr.
  ///
  /// Note that \p SectionKind is used as a hint to guess the unit type for
  /// DWARF formats prior to DWARFv5. In DWARFv5 the unit type is explicitly
  /// defined in the header and the hint is ignored.
  ///
  /// \param Context DWARF context used while parsing.
  /// \param debug_info Extractor for the .debug_info/.debug_types section.
  /// \param offset_ptr Byte offset of the header; advanced past it on success.
  /// \param SectionKind Hint for the unit type on pre-DWARFv5 formats.
  /// \returns Success, or an error if the header could not be parsed.
  LLVM_ABI Error extract(DWARFContext &Context,
                         const DWARFDataExtractor &debug_info,
                         uint64_t *offset_ptr, DWARFSectionKind SectionKind);
  /// Apply a DWARF package-file index entry to this header.
  ///
  /// Remembers the index entry and updates the abbreviation offset read by
  /// extract().
  ///
  /// \param Entry Package-file index entry for this unit.
  /// \returns Success, or an error if the index entry could not be applied.
  LLVM_ABI Error applyIndexEntry(const DWARFUnitIndex::Entry *Entry);
  /// Byte offset of this unit header within its .debug_info/.debug_types section.
  ///
  /// \returns Absolute offset of this header within its section.
  uint64_t getOffset() const { return Offset; }
  /// Form parameters (version, address size, format) from this unit header.
  ///
  /// \returns Form parameters from this unit header.
  const dwarf::FormParams &getFormParams() const { return FormParams; }
  /// DWARF version number from this unit header.
  ///
  /// \returns DWARF version number from this unit header.
  uint16_t getVersion() const { return FormParams.Version; }
  /// DWARF 32/64 format of this unit header.
  ///
  /// \returns DWARF 32/64 format of this unit header.
  dwarf::DwarfFormat getFormat() const { return FormParams.Format; }
  /// Address size in bytes for this unit.
  ///
  /// \returns Address size in bytes for this unit.
  uint8_t getAddressByteSize() const { return FormParams.AddrSize; }
  /// Byte size of a DW_FORM_ref_addr value for this unit's form parameters.
  ///
  /// \returns Byte size of a DW_FORM_ref_addr value for this unit.
  uint8_t getRefAddrByteSize() const { return FormParams.getRefAddrByteSize(); }
  /// Byte size of a DWARF section offset for this unit's format (4 or 8).
  ///
  /// \returns Byte size of a DWARF section offset for this unit's format.
  uint8_t getDwarfOffsetByteSize() const {
    return FormParams.getDwarfOffsetByteSize();
  }
  /// Length of this unit from the unit header length field.
  ///
  /// \returns Length of this unit from the unit header length field.
  uint64_t getLength() const { return Length; }
  /// Offset of this unit's abbreviations within .debug_abbrev.
  ///
  /// \returns Offset of this unit's abbreviations within .debug_abbrev.
  uint64_t getAbbrOffset() const { return AbbrOffset; }
  /// DWO ID for split/skeleton units, if present.
  ///
  /// \returns DWO ID for split/skeleton units, or std::nullopt if absent.
  std::optional<uint64_t> getDWOId() const { return DWOId; }
  /// Set the DWO ID for this unit (must match any existing value).
  ///
  /// \param Id DWO ID to record on this header.
  void setDWOId(uint64_t Id) {
    assert((!DWOId || *DWOId == Id) && "setting DWOId to a different value");
    DWOId = Id;
  }
  /// Package-file index entry for this unit, or nullptr if none.
  ///
  /// \returns Package-file index entry for this unit, or nullptr if none.
  const DWARFUnitIndex::Entry *getIndexEntry() const { return IndexEntry; }
  /// Type signature hash for type units.
  ///
  /// \returns Type signature hash for type units.
  uint64_t getTypeHash() const { return TypeHash; }
  /// Offset of the type DIE within a type unit.
  ///
  /// \returns Offset of the type DIE within a type unit.
  uint64_t getTypeOffset() const { return TypeOffset; }
  /// DWARF unit type (e.g. DW_UT_compile), as parsed or inferred.
  ///
  /// \returns DWARF unit type as parsed or inferred.
  uint8_t getUnitType() const { return UnitType; }
  /// True if this header describes a type unit (including split type units).
  ///
  /// \returns True if this header describes a type unit.
  bool isTypeUnit() const {
    return UnitType == dwarf::DW_UT_type || UnitType == dwarf::DW_UT_split_type;
  }
  /// Size in bytes of the parsed unit header.
  ///
  /// \returns Size in bytes of the parsed unit header.
  uint8_t getSize() const { return Size; }
  /// Byte size of the unit_length field for this header's DWARF format.
  ///
  /// \returns Byte size of the unit_length field for this header's format.
  uint8_t getUnitLengthFieldByteSize() const {
    return dwarf::getUnitLengthFieldByteSize(FormParams.Format);
  }
  /// Offset of the next unit after this one in the same section.
  ///
  /// \returns Offset of the next unit after this one in the same section.
  uint64_t getNextUnitOffset() const {
    return Offset + Length + getUnitLengthFieldByteSize();
  }
};

/// Return the CU or TU index table for section kind \p Kind.
///
/// \param Context DWARF context that owns the index tables.
/// \param Kind Section kind selecting the CU or TU index.
/// \returns The CU or TU index table for \p Kind.
LLVM_ABI const DWARFUnitIndex &getDWARFUnitIndex(DWARFContext &Context,
                                                 DWARFSectionKind Kind);

/// True if \p U is a compile unit (not a type unit).
///
/// \param U Unit unique_ptr to test.
/// \returns True if \p U is a compile unit (not a type unit).
bool isCompileUnit(const std::unique_ptr<DWARFUnit> &U);

/// Describe a collection of units. Intended to hold all units either from
/// .debug_info and .debug_types, or from .debug_info.dwo and .debug_types.dwo.
class DWARFUnitVector final : public SmallVector<std::unique_ptr<DWARFUnit>, 1> {
  std::function<std::unique_ptr<DWARFUnit>(uint64_t, DWARFSectionKind,
                                           const DWARFSection *,
                                           const DWARFUnitIndex::Entry *)>
      Parser;
  int NumInfoUnits = -1;

public:
  /// Underlying storage type for units owned by this vector.
  using UnitVector = SmallVectorImpl<std::unique_ptr<DWARFUnit>>;
  /// Iterator over unique_ptr<DWARFUnit> elements in this vector.
  using iterator = UnitVector::iterator;
  /// Iterator range over units in this vector.
  using iterator_range = llvm::iterator_range<UnitVector::iterator>;

  /// Filtered range over compile units (excluding type units) in this vector.
  using compile_unit_range =
      decltype(make_filter_range(std::declval<iterator_range>(), isCompileUnit));

  /// Return the unit whose DIE data covers absolute offset \p Offset.
  ///
  /// \param Offset Absolute offset within the info/types section.
  /// \returns The unit covering \p Offset, or nullptr if none.
  LLVM_ABI DWARFUnit *getUnitForOffset(uint64_t Offset) const;
  /// Return the unit for package-file index entry \p E.
  ///
  /// \param E Package-file index entry identifying the unit.
  /// \param Sec Section kind for the unit (.debug_info or .debug_types).
  /// \param Section Optional section containing the unit data.
  /// \returns The unit for index entry \p E.
  LLVM_ABI DWARFUnit *
  getUnitForIndexEntry(const DWARFUnitIndex::Entry &E, DWARFSectionKind Sec,
                       const DWARFSection *Section = nullptr);

  /// Read units from a .debug_info or .debug_types section.
  ///
  /// Calls made before finishedInfoUnits() are assumed to be for .debug_info
  /// sections, calls after finishedInfoUnits() are for .debug_types sections.
  /// Caller must not mix calls to addUnitsForSection and
  /// addUnitsForDWOSection.
  ///
  /// \param C DWARF context that owns the resulting units.
  /// \param Section .debug_info or .debug_types section to parse.
  /// \param SectionKind Kind of \p Section (.debug_info or .debug_types).
  LLVM_ABI void addUnitsForSection(DWARFContext &C, const DWARFSection &Section,
                                   DWARFSectionKind SectionKind);
  /// Read units from a .debug_info.dwo or .debug_types.dwo section.
  ///
  /// Calls made before finishedInfoUnits() are assumed to be for
  /// .debug_info.dwo sections, calls after finishedInfoUnits() are for
  /// .debug_types.dwo sections. Caller must not mix calls to
  /// addUnitsForSection and addUnitsForDWOSection.
  ///
  /// \param C DWARF context that owns the resulting units.
  /// \param DWOSection .debug_info.dwo or .debug_types.dwo section to parse.
  /// \param SectionKind Kind of \p DWOSection.
  /// \param Lazy If true, defer parsing units until needed.
  LLVM_ABI void addUnitsForDWOSection(DWARFContext &C,
                                      const DWARFSection &DWOSection,
                                      DWARFSectionKind SectionKind,
                                      bool Lazy = false);

  /// Add an existing DWARFUnit to this UnitVector.
  ///
  /// This is used by the DWARF verifier to process units separately.
  ///
  /// \param Unit Unit to take ownership of and append.
  /// \returns Pointer to the added unit.
  LLVM_ABI DWARFUnit *addUnit(std::unique_ptr<DWARFUnit> Unit);

  /// Returns number of all units held by this instance.
  ///
  /// \returns Number of all units held by this instance.
  unsigned getNumUnits() const { return size(); }
  /// Returns number of units from all .debug_info[.dwo] sections.
  ///
  /// \returns Number of units from all .debug_info[.dwo] sections.
  unsigned getNumInfoUnits() const {
    return NumInfoUnits == -1 ? size() : NumInfoUnits;
  }
  /// Returns number of units from all .debug_types[.dwo] sections.
  ///
  /// \returns Number of units from all .debug_types[.dwo] sections.
  unsigned getNumTypesUnits() const { return size() - NumInfoUnits; }
  /// Indicate that parsing .debug_info[.dwo] is done, and remaining units
  /// will be from .debug_types[.dwo].
  void finishedInfoUnits() { NumInfoUnits = size(); }

private:
  void addUnitsImpl(DWARFContext &Context, const DWARFObject &Obj,
                    const DWARFSection &Section, const DWARFDebugAbbrev *DA,
                    const DWARFSection *RS, const DWARFSection *LocSection,
                    StringRef SS, const DWARFSection &SOS,
                    const DWARFSection *AOS, const DWARFSection &LS, bool LE,
                    bool IsDWO, bool Lazy, DWARFSectionKind SectionKind);
};

/// Represents base address of the CU.
/// Represents a unit's contribution to the string offsets table.
struct StrOffsetsContributionDescriptor {
  /// Base offset of this contribution within .debug_str_offsets.
  uint64_t Base = 0;
  /// The contribution size not including the header.
  uint64_t Size = 0;
  /// Format and version.
  dwarf::FormParams FormParams = {0, 0, dwarf::DwarfFormat::DWARF32};

  /// Construct a contribution starting at \p Base with \p Size bytes.
  ///
  /// \param Base Base offset within .debug_str_offsets.
  /// \param Size Contribution size not including the header.
  /// \param Version DWARF version of the contribution.
  /// \param Format DWARF 32/64 format of the contribution.
  StrOffsetsContributionDescriptor(uint64_t Base, uint64_t Size,
                                   uint8_t Version, dwarf::DwarfFormat Format)
      : Base(Base), Size(Size), FormParams({Version, 0, Format}) {}
  /// Construct an empty contribution with default form parameters.
  StrOffsetsContributionDescriptor() = default;

  /// DWARF version of this string-offsets contribution.
  ///
  /// \returns DWARF version of this string-offsets contribution.
  uint8_t getVersion() const { return FormParams.Version; }
  /// DWARF format (32- or 64-bit) of this string-offsets contribution.
  ///
  /// \returns DWARF format of this string-offsets contribution.
  dwarf::DwarfFormat getFormat() const { return FormParams.Format; }
  /// Byte size of a DWARF section offset for this contribution's format.
  ///
  /// \returns Byte size of a DWARF section offset for this contribution's format.
  uint8_t getDwarfOffsetByteSize() const {
    return FormParams.getDwarfOffsetByteSize();
  }
  /// Validate this contribution against the size of section \p DA.
  ///
  /// Checks that the contribution is consistent with the relevant section
  /// size and that its length is a multiple of the size of one of its
  /// entries.
  ///
  /// \param DA Data extractor for the string-offsets section.
  /// \returns This contribution on success, or an error if invalid.
  LLVM_ABI Expected<StrOffsetsContributionDescriptor>
  validateContributionSize(DWARFDataExtractor &DA);
};

/// A DWARF compile or type unit and its parsed DIEs.
class LLVM_ABI DWARFUnit {
  DWARFContext &Context;
  /// Section containing this DWARFUnit.
  const DWARFSection &InfoSection;

  DWARFUnitHeader Header;
  const DWARFDebugAbbrev *Abbrev;
  const DWARFSection *RangeSection;
  uint64_t RangeSectionBase;
  uint64_t LocSectionBase;

  /// Location table of this unit.
  std::unique_ptr<DWARFLocationTable> LocTable;

  const DWARFSection &LineSection;
  StringRef StringSection;
  const DWARFSection &StringOffsetSection;
  const DWARFSection *AddrOffsetSection;
  DWARFUnit *SU;
  std::optional<uint64_t> AddrOffsetSectionBase;
  bool IsLittleEndian;
  bool IsDWO;
  const DWARFUnitVector &UnitVector;

  /// Start, length, and DWARF format of the unit's contribution to the string
  /// offsets table (DWARF v5).
  std::optional<StrOffsetsContributionDescriptor>
      StringOffsetsTableContribution;

  mutable const DWARFAbbreviationDeclarationSet *Abbrevs;
  std::optional<object::SectionedAddress> BaseAddr;
  /// The compile unit debug information entry items.
  std::vector<DWARFDebugInfoEntry> DieArray;

  /// Map from range's start address to end address and corresponding DIE.
  /// IntervalMap does not support range removal, as a result, we use the
  /// std::map::upper_bound for address range lookup.
  std::map<uint64_t, std::pair<uint64_t, DWARFDie>> AddrDieMap;

  /// Map from the location (interpreted DW_AT_location) of a DW_TAG_variable,
  /// to the end address and the corresponding DIE.
  std::map<uint64_t, std::pair<uint64_t, DWARFDie>> VariableDieMap;
  DenseSet<uint64_t> RootsParsedForVariables;

  using die_iterator_range =
      iterator_range<std::vector<DWARFDebugInfoEntry>::iterator>;

  std::shared_ptr<DWARFUnit> DWO;

protected:
  friend dwarf_linker::parallel::CompileUnit;

  /// Return the index of a \p Die entry inside the unit's DIE vector.
  ///
  /// It is illegal to call this method with a DIE that hasn't be
  /// created by this unit. In other word, it's illegal to call this
  /// method on a DIE that isn't accessible by following
  /// children/sibling links starting from this unit's getUnitDIE().
  ///
  /// \param Die Debug-info entry owned by this unit.
  /// \returns Index of \p Die within this unit's DIE vector.
  uint32_t getDIEIndex(const DWARFDebugInfoEntry *Die) const {
    auto First = DieArray.data();
    assert(Die >= First && Die < First + DieArray.size());
    return Die - First;
  }

  /// Return DWARFDebugInfoEntry for the specified index \p Index.
  ///
  /// \param Index Index into this unit's DIE array.
  /// \returns Debug-info entry at \p Index in this unit's DIE array.
  const DWARFDebugInfoEntry *getDebugInfoEntry(unsigned Index) const {
    assert(Index < DieArray.size());
    return &DieArray[Index];
  }

  /// Return the parent debug-info entry of \p Die, or nullptr.
  ///
  /// \param Die Debug-info entry whose parent is requested.
  /// \returns Parent of \p Die, or nullptr if it has none.
  const DWARFDebugInfoEntry *
  getParentEntry(const DWARFDebugInfoEntry *Die) const;
  /// Return the next sibling entry of \p Die, or nullptr.
  ///
  /// \param Die Debug-info entry whose next sibling is requested.
  /// \returns Next sibling of \p Die, or nullptr if none.
  const DWARFDebugInfoEntry *
  getSiblingEntry(const DWARFDebugInfoEntry *Die) const;
  /// Return the previous sibling entry of \p Die, or nullptr.
  ///
  /// \param Die Debug-info entry whose previous sibling is requested.
  /// \returns Previous sibling of \p Die, or nullptr if none.
  const DWARFDebugInfoEntry *
  getPreviousSiblingEntry(const DWARFDebugInfoEntry *Die) const;
  /// Return the first child entry of \p Die, or nullptr.
  ///
  /// \param Die Debug-info entry whose first child is requested.
  /// \returns First child of \p Die, or nullptr if it has none.
  const DWARFDebugInfoEntry *
  getFirstChildEntry(const DWARFDebugInfoEntry *Die) const;
  /// Return the last child entry of \p Die, or nullptr.
  ///
  /// \param Die Debug-info entry whose last child is requested.
  /// \returns Last child of \p Die, or nullptr if it has none.
  const DWARFDebugInfoEntry *
  getLastChildEntry(const DWARFDebugInfoEntry *Die) const;

  /// Return this unit's parsed header.
  ///
  /// \returns This unit's parsed header.
  const DWARFUnitHeader &getHeader() const { return Header; }

  /// Find this unit's contribution to the string offsets table.
  ///
  /// Determines its length and form. The given offset is expected to be
  /// derived from the unit DIE's DW_AT_str_offsets_base attribute.
  ///
  /// \param DA Data extractor for the .debug_str_offsets section.
  /// \returns This unit's string-offsets contribution, or an error.
  Expected<std::optional<StrOffsetsContributionDescriptor>>
  determineStringOffsetsTableContribution(DWARFDataExtractor &DA);

  /// Find this DWO/DWP unit's contribution to the string offsets table.
  ///
  /// Determines its length and form. The given offset is expected to be 0 in
  /// a dwo file or, in a dwp file, the start of the unit's contribution to
  /// the string offsets table section (as determined by the index table).
  ///
  /// \param DA Data extractor for the .debug_str_offsets[.dwo] section.
  /// \returns This DWO unit's string-offsets contribution, or an error.
  Expected<std::optional<StrOffsetsContributionDescriptor>>
  determineStringOffsetsTableContributionDWO(DWARFDataExtractor &DA);

public:
  /// Construct a unit for \p Section using \p Header and related DWARF sections.
  ///
  /// \param Context DWARF context that owns this unit.
  /// \param Section .debug_info or .debug_types section for this unit.
  /// \param Header Parsed unit header for this unit.
  /// \param DA Abbreviation table for this unit.
  /// \param RS .debug_ranges or .debug_rnglists section, or nullptr.
  /// \param LocSection .debug_loc or .debug_loclists section, or nullptr.
  /// \param SS .debug_str section contents.
  /// \param SOS .debug_str_offsets section.
  /// \param AOS .debug_addr section, or nullptr.
  /// \param LS .debug_line section.
  /// \param LE True if the object is little-endian.
  /// \param IsDWO True if this unit came from a .dwo/.dwp object.
  /// \param UnitVector Unit vector that will own this unit.
  DWARFUnit(DWARFContext &Context, const DWARFSection &Section,
            const DWARFUnitHeader &Header, const DWARFDebugAbbrev *DA,
            const DWARFSection *RS, const DWARFSection *LocSection,
            StringRef SS, const DWARFSection &SOS, const DWARFSection *AOS,
            const DWARFSection &LS, bool LE, bool IsDWO,
            const DWARFUnitVector &UnitVector);

  /// Destroy this unit and release owned DWO state.
  virtual ~DWARFUnit();

  /// True if this unit's data is little-endian.
  ///
  /// \returns True if this unit's data is little-endian.
  bool isLittleEndian() const { return IsLittleEndian; }
  /// True if this unit came from a .dwo/.dwp split DWARF object.
  ///
  /// \returns True if this unit came from a .dwo/.dwp object.
  bool isDWOUnit() const { return IsDWO; }
  /// Return the DWARF context that owns this unit.
  ///
  /// \returns The DWARF context that owns this unit.
  DWARFContext& getContext() const { return Context; }
  /// Section containing this unit's .debug_info/.debug_types data.
  ///
  /// \returns Section containing this unit's info/types data.
  const DWARFSection &getInfoSection() const { return InfoSection; }
  /// Byte offset of this unit within its info/types section.
  ///
  /// \returns Byte offset of this unit within its info/types section.
  uint64_t getOffset() const { return Header.getOffset(); }
  /// Form parameters (version, address size, format) for this unit.
  ///
  /// \returns Form parameters for this unit.
  const dwarf::FormParams &getFormParams() const {
    return Header.getFormParams();
  }
  /// DWARF version number from this unit's header.
  ///
  /// \returns DWARF version number from this unit's header.
  uint16_t getVersion() const { return Header.getVersion(); }
  /// Address size in bytes for this unit.
  ///
  /// \returns Address size in bytes for this unit.
  uint8_t getAddressByteSize() const { return Header.getAddressByteSize(); }
  /// Byte size of a DW_FORM_ref_addr value for this unit.
  ///
  /// \returns Byte size of a DW_FORM_ref_addr value for this unit.
  uint8_t getRefAddrByteSize() const { return Header.getRefAddrByteSize(); }
  /// Byte size of a DWARF section offset for this unit's format (4 or 8).
  ///
  /// \returns Byte size of a DWARF section offset for this unit's format.
  uint8_t getDwarfOffsetByteSize() const {
    return Header.getDwarfOffsetByteSize();
  }
  /// Size in bytes of the parsed unit header.
  ///
  /// \returns Size in bytes of the parsed unit header.
  uint32_t getHeaderSize() const { return Header.getSize(); }
  /// Length of this unit's contribution from the unit header.
  ///
  /// \returns Length of this unit's contribution from the unit header.
  uint64_t getLength() const { return Header.getLength(); }
  /// DWARF format (32- or 64-bit) of this unit.
  ///
  /// \returns DWARF format of this unit.
  dwarf::DwarfFormat getFormat() const { return Header.getFormat(); }
  /// DWARF unit type (e.g. DW_UT_compile) from the unit header.
  ///
  /// \returns DWARF unit type from the unit header.
  uint8_t getUnitType() const { return Header.getUnitType(); }
  /// True if this is a type unit (including split type units).
  ///
  /// \returns True if this is a type unit.
  bool isTypeUnit() const { return Header.isTypeUnit(); }
  /// Offset of this unit's abbreviations within .debug_abbrev.
  ///
  /// \returns Offset of this unit's abbreviations within .debug_abbrev.
  uint64_t getAbbrOffset() const { return Header.getAbbrOffset(); }
  /// Absolute section offset of the unit that follows this one.
  ///
  /// \returns Absolute section offset of the unit that follows this one.
  uint64_t getNextUnitOffset() const { return Header.getNextUnitOffset(); }
  /// Return the .debug_line section associated with this unit.
  ///
  /// \returns The .debug_line section associated with this unit.
  const DWARFSection &getLineSection() const { return LineSection; }
  /// Return the .debug_str section associated with this unit.
  ///
  /// \returns The .debug_str section associated with this unit.
  StringRef getStringSection() const { return StringSection; }
  /// Return the .debug_str_offsets section associated with this unit.
  ///
  /// \returns The .debug_str_offsets section associated with this unit.
  const DWARFSection &getStringOffsetSection() const {
    return StringOffsetSection;
  }

  /// Associate skeleton unit \p SU with this split/DWO unit.
  ///
  /// \param SU Skeleton unit linked to this DWO unit.
  void setSkeletonUnit(DWARFUnit *SU) { this->SU = SU; }
  /// Return this unit, or its skeleton unit when this is a split DWO unit.
  ///
  /// \returns This unit, or its skeleton unit when this is a split DWO unit.
  DWARFUnit *getLinkedUnit() { return IsDWO ? SU : this; }

  /// Set the .debug_addr section and this unit's base offset within it.
  ///
  /// \param AOS .debug_addr section for this unit.
  /// \param Base Base offset of this unit's contribution within \p AOS.
  void setAddrOffsetSection(const DWARFSection *AOS, uint64_t Base) {
    AddrOffsetSection = AOS;
    AddrOffsetSectionBase = Base;
  }

  /// Base offset of this unit's contribution within .debug_addr, if set.
  ///
  /// \returns Base offset within .debug_addr, or std::nullopt if unset.
  std::optional<uint64_t> getAddrOffsetSectionBase() const {
    return AddrOffsetSectionBase;
  }

  /// Returns offset to the indexed address value inside .debug_addr section.
  ///
  /// \param Index Index into this unit's .debug_addr contribution.
  /// \returns Absolute offset of the indexed address, or std::nullopt if unset.
  std::optional<uint64_t> getIndexedAddressOffset(uint64_t Index) {
    if (std::optional<uint64_t> AddrOffsetSectionBase =
            getAddrOffsetSectionBase())
      return *AddrOffsetSectionBase + Index * getAddressByteSize();

    return std::nullopt;
  }

  /// Recursively update address to Die map.
  ///
  /// \param Die DIE whose address ranges should be indexed.
  void updateAddressDieMap(DWARFDie Die);

  /// Recursively update address to variable Die map.
  ///
  /// \param Die Variable DIE whose location should be indexed.
  void updateVariableDieMap(DWARFDie Die);

  /// Set the .debug_ranges/.debug_rnglists section and this unit's base offset.
  ///
  /// \param RS Ranges section for this unit.
  /// \param Base Base offset of this unit's contribution within \p RS.
  void setRangesSection(const DWARFSection *RS, uint64_t Base) {
    RangeSection = RS;
    RangeSectionBase = Base;
  }

  /// Base offset of this unit's contribution within the location section.
  ///
  /// \returns Base offset of this unit's contribution within the location section.
  uint64_t getLocSectionBase() const {
    return LocSectionBase;
  }

  /// Look up address \p Index in this unit's .debug_addr contribution.
  ///
  /// \param Index Index into this unit's .debug_addr contribution.
  /// \returns The relocated address and section index, or std::nullopt if the
  /// address table base is unset or the index is out of range.
  std::optional<object::SectionedAddress>
  getAddrOffsetSectionItem(uint32_t Index) const;
  /// Look up string-offset \p Index in this unit's .debug_str_offsets contribution.
  ///
  /// \param Index Index into this unit's .debug_str_offsets contribution.
  /// \returns The relocated offset, or an error if the contribution is missing
  /// or the index is out of range.
  Expected<uint64_t> getStringOffsetSectionItem(uint32_t Index) const;

  /// Data extractor over this unit's .debug_info/.debug_types bytes.
  ///
  /// \returns Data extractor over this unit's info/types bytes.
  DWARFDataExtractor getDebugInfoExtractor() const;

  /// Data extractor over this unit's .debug_str section bytes.
  ///
  /// \returns Data extractor over this unit's .debug_str section bytes.
  DataExtractor getStringExtractor() const {
    return DataExtractor(StringSection, false);
  }

  /// Location list table for this unit.
  ///
  /// \returns Location list table for this unit.
  const DWARFLocationTable &getLocationTable() { return *LocTable; }

  /// Extract a range list from .debug_ranges for this compile unit.
  ///
  /// If the extraction is unsuccessful, an error is returned. Successful
  /// extraction requires that the compile unit has already been extracted.
  ///
  /// \param RangeListOffset Offset of the range list within .debug_ranges.
  /// \param RangeList Receives the extracted range list on success.
  /// \returns Success, or an error if the range list could not be extracted.
  Error extractRangeList(uint64_t RangeListOffset,
                         DWARFDebugRangeList &RangeList) const;
  /// Clear parsed DIE and abbreviation state for this unit.
  void clear();

  /// Return this unit's contribution to .debug_str_offsets (extracting the CU DIE if needed).
  ///
  /// \returns This unit's contribution to .debug_str_offsets.
  const std::optional<StrOffsetsContributionDescriptor> &
  getStringOffsetsTableContribution() {
    extractDIEsIfNeeded(true /*CUDIeOnly*/);
    return StringOffsetsTableContribution;
  }

  /// Byte size of one entry in this unit's string-offsets contribution.
  ///
  /// \returns Byte size of one entry in this unit's string-offsets contribution.
  uint8_t getDwarfStringOffsetsByteSize() const {
    assert(StringOffsetsTableContribution);
    return StringOffsetsTableContribution->getDwarfOffsetByteSize();
  }

  /// Base offset of this unit's contribution to .debug_str_offsets.
  ///
  /// \returns Base offset of this unit's contribution to .debug_str_offsets.
  uint64_t getStringOffsetsBase() const {
    assert(StringOffsetsTableContribution);
    return StringOffsetsTableContribution->Base;
  }

  /// Offset of this unit's abbreviation set within .debug_abbrev.
  ///
  /// \returns Offset of this unit's abbreviation set within .debug_abbrev.
  uint64_t getAbbreviationsOffset() const { return Header.getAbbrOffset(); }

  /// Abbreviation set for this unit, or nullptr if unavailable.
  ///
  /// \returns Abbreviation set for this unit, or nullptr if unavailable.
  const DWARFAbbreviationDeclarationSet *getAbbreviations() const;

  /// True if DIE tag \p Tag is valid for unit type \p UnitType.
  ///
  /// \param UnitType DWARF unit type (e.g. DW_UT_compile).
  /// \param Tag DIE tag expected for that unit type.
  /// \returns True if \p Tag is valid for \p UnitType.
  static bool isMatchingUnitTypeAndTag(uint8_t UnitType, dwarf::Tag Tag) {
    switch (UnitType) {
    case dwarf::DW_UT_compile:
      return Tag == dwarf::DW_TAG_compile_unit;
    case dwarf::DW_UT_type:
      return Tag == dwarf::DW_TAG_type_unit;
    case dwarf::DW_UT_partial:
      return Tag == dwarf::DW_TAG_partial_unit;
    case dwarf::DW_UT_skeleton:
      return Tag == dwarf::DW_TAG_skeleton_unit;
    case dwarf::DW_UT_split_compile:
    case dwarf::DW_UT_split_type:
      return dwarf::isUnitType(Tag);
    }
    return false;
  }

  /// Unit base address from DW_AT_low_pc or DW_AT_entry_pc, if present.
  ///
  /// \returns Unit base address, or std::nullopt if absent.
  std::optional<object::SectionedAddress> getBaseAddress();

  /// Return the root unit DIE, optionally extracting only that DIE.
  ///
  /// \param ExtractUnitDIEOnly If true, only the unit DIE is extracted.
  /// \returns The root unit DIE, or an invalid DIE if none.
  DWARFDie getUnitDIE(bool ExtractUnitDIEOnly = true) {
    extractDIEsIfNeeded(ExtractUnitDIEOnly);
    if (DieArray.empty())
      return DWARFDie();
    return DWARFDie(this, &DieArray[0]);
  }

  /// Return the split/DWO unit DIE when available, otherwise this unit's DIE.
  ///
  /// Parses the DWO context first, optionally from \p DWOAlternativeLocation.
  ///
  /// \param ExtractUnitDIEOnly If true, only the unit DIE is extracted.
  /// \param DWOAlternativeLocation Optional alternate path for the DWO file.
  /// \returns The split/DWO unit DIE when available, otherwise this unit's DIE.
  DWARFDie getNonSkeletonUnitDIE(bool ExtractUnitDIEOnly = true,
                                 StringRef DWOAlternativeLocation = {}) {
    parseDWO(DWOAlternativeLocation);
    return DWO ? DWO->getUnitDIE(ExtractUnitDIEOnly)
               : getUnitDIE(ExtractUnitDIEOnly);
  }

  /// Return the split unit this skeleton unit currently owns, or null if its
  /// DWO context is not open.
  ///
  /// \returns The owned split unit, or nullptr if the DWO context is not open.
  DWARFUnit *getDWO() const { return DWO.get(); }

  /// Release the DWO context owned by this skeleton unit.
  ///
  /// Frees the memory held by its DWARFContext and parsed unit vector. This
  /// is safe to call once the split-unit debug info has been fully processed;
  /// a subsequent parseDWO() will transparently re-open it on demand.
  void clearDWO() { DWO.reset(); }

  /// Return the DW_AT_comp_dir compilation directory string, or nullptr if absent.
  ///
  /// \returns The compilation directory string, or nullptr if absent.
  const char *getCompilationDir();
  /// DWO ID from the unit DIE/header for split DWARF, if present.
  ///
  /// \returns DWO ID for split DWARF, or std::nullopt if absent.
  std::optional<uint64_t> getDWOId() {
    extractDIEsIfNeeded(/*CUDieOnly*/ true);
    return getHeader().getDWOId();
  }
  /// Set the DWO ID on this unit's header.
  ///
  /// \param NewID DWO ID to record for this unit.
  void setDWOId(uint64_t NewID) { Header.setDWOId(NewID); }

  /// Return address ranges from the range list at offset \p Offset.
  ///
  /// \param Offset Offset of the (possibly encoded) range list.
  /// \returns Address ranges from the range list, or an error on failure.
  Expected<DWARFAddressRangesVector> findRnglistFromOffset(uint64_t Offset);

  /// Return address ranges from the indexed range list at \p Index.
  ///
  /// The offset is found via a table lookup (DWARF v5 and later).
  ///
  /// \param Index Index into the rangelist table's offset array.
  /// \returns Address ranges from the indexed range list, or an error on failure.
  Expected<DWARFAddressRangesVector> findRnglistFromIndex(uint32_t Index);

  /// Return a rangelist's offset based on an index.
  ///
  /// The index designates an entry in the rangelist table's offset array and
  /// is supplied by DW_FORM_rnglistx.
  ///
  /// \param Index Index into the rangelist offset array.
  /// \returns Absolute rangelist offset, or std::nullopt if unavailable.
  std::optional<uint64_t> getRnglistOffset(uint32_t Index);

  /// Return a loclist's offset based on index \p Index (DW_FORM_loclistx).
  ///
  /// \param Index Index into the loclist offset array.
  /// \returns Absolute loclist offset, or std::nullopt if unavailable.
  std::optional<uint64_t> getLoclistOffset(uint32_t Index);

  /// Collect address ranges covered by this unit's DIE tree.
  ///
  /// \returns Address ranges covered by this unit, or an error on failure.
  Expected<DWARFAddressRangesVector> collectAddressRanges();

  /// Return location expressions from the loclist at offset \p Offset.
  ///
  /// \param Offset Offset of the loclist within the location section.
  /// \returns Location expressions from the loclist, or an error on failure.
  Expected<DWARFLocationExpressionsVector>
  findLoclistFromOffset(uint64_t Offset);

  /// Return the subprogram DIE whose address range covers \p Address.
  ///
  /// The pointer is alive as long as parsed compile unit DIEs are not
  /// cleared.
  ///
  /// \param Address Program-counter address to look up.
  /// \returns Subprogram DIE covering \p Address, or an invalid DIE if none.
  DWARFDie getSubroutineForAddress(uint64_t Address);

  /// Return the variable DIE for address \p Address.
  ///
  /// The pointer is alive as long as parsed compile unit DIEs are not
  /// cleared.
  ///
  /// \param Address Program-counter address to look up.
  /// \returns Variable DIE for \p Address, or an invalid DIE if none.
  DWARFDie getVariableForAddress(uint64_t Address);

  /// Fetch the inlined call chain for a given address.
  ///
  /// Returns an empty chain if there is no subprogram containing address.
  /// The chain is valid as long as parsed compile unit DIEs are not cleared.
  ///
  /// \param Address Program-counter address to look up.
  /// \param InlinedChain Receives the inlined DIE chain for \p Address.
  void getInlinedChainForAddress(uint64_t Address,
                                 SmallVectorImpl<DWARFDie> &InlinedChain);

  /// Return the DWARFUnitVector containing this unit.
  ///
  /// \returns The DWARFUnitVector containing this unit.
  const DWARFUnitVector &getUnitVector() const { return UnitVector; }

  /// Returns the number of DIEs in the unit. Parses the unit
  /// if necessary.
  ///
  /// \returns Number of DIEs in the unit.
  unsigned getNumDIEs() {
    extractDIEsIfNeeded(false);
    return DieArray.size();
  }

  /// Return the index of a DIE inside the unit's DIE vector.
  ///
  /// It is illegal to call this method with a DIE that hasn't be
  /// created by this unit. In other word, it's illegal to call this
  /// method on a DIE that isn't accessible by following
  /// children/sibling links starting from this unit's getUnitDIE().
  ///
  /// \param D DIE owned by this unit.
  /// \returns Index of \p D within this unit's DIE vector.
  uint32_t getDIEIndex(const DWARFDie &D) const {
    return getDIEIndex(D.getDebugInfoEntry());
  }

  /// Return the DIE object at the given index \p Index.
  ///
  /// \param Index Index into this unit's DIE array.
  /// \returns DIE object at \p Index.
  DWARFDie getDIEAtIndex(unsigned Index) {
    return DWARFDie(this, getDebugInfoEntry(Index));
  }

  /// Return the parent DIE of \p Die, or an invalid DIE if it has none.
  ///
  /// \param Die Debug-info entry whose parent is requested.
  /// \returns Parent DIE of \p Die, or an invalid DIE if it has none.
  DWARFDie getParent(const DWARFDebugInfoEntry *Die);
  /// Return the next sibling DIE of \p Die, or an invalid DIE if none.
  ///
  /// \param Die Debug-info entry whose next sibling is requested.
  /// \returns Next sibling DIE of \p Die, or an invalid DIE if none.
  DWARFDie getSibling(const DWARFDebugInfoEntry *Die);
  /// Return the previous sibling DIE of \p Die, or an invalid DIE if none.
  ///
  /// \param Die Debug-info entry whose previous sibling is requested.
  /// \returns Previous sibling DIE of \p Die, or an invalid DIE if none.
  DWARFDie getPreviousSibling(const DWARFDebugInfoEntry *Die);
  /// Return the first child DIE of \p Die, or an invalid DIE if it has none.
  ///
  /// \param Die Debug-info entry whose first child is requested.
  /// \returns First child DIE of \p Die, or an invalid DIE if it has none.
  DWARFDie getFirstChild(const DWARFDebugInfoEntry *Die);
  /// Return the last child DIE of \p Die (the terminating null), or invalid if none.
  ///
  /// \param Die Debug-info entry whose last child is requested.
  /// \returns Last child DIE of \p Die, or an invalid DIE if none.
  DWARFDie getLastChild(const DWARFDebugInfoEntry *Die);

  /// Return the DIE object for a given offset \p Offset inside the
  /// unit's DIE vector.
  ///
  /// \param Offset Absolute offset of the DIE within this unit.
  /// \returns DIE at \p Offset, or an invalid DIE if not found.
  DWARFDie getDIEForOffset(uint64_t Offset) {
    if (std::optional<uint32_t> DieIdx = getDIEIndexForOffset(Offset))
      return DWARFDie(this, &DieArray[*DieIdx]);

    return DWARFDie();
  }

  /// Return the DIE index for a given offset \p Offset inside the
  /// unit's DIE vector.
  ///
  /// \param Offset Absolute offset of the DIE within this unit.
  /// \returns Index of the DIE at \p Offset, or std::nullopt if not found.
  std::optional<uint32_t> getDIEIndexForOffset(uint64_t Offset) {
    extractDIEsIfNeeded(false);
    auto It =
        llvm::partition_point(DieArray, [=](const DWARFDebugInfoEntry &DIE) {
          return DIE.getOffset() < Offset;
        });
    if (It != DieArray.end() && It->getOffset() == Offset)
      return It - DieArray.begin();
    return std::nullopt;
  }

  /// Offset of this unit's .debug_line contribution from the package index, or 0.
  ///
  /// \returns Line-table contribution offset from the package index, or 0.
  uint32_t getLineTableOffset() const {
    if (auto IndexEntry = Header.getIndexEntry())
      if (const auto *Contrib = IndexEntry->getContribution(DW_SECT_LINE))
        return Contrib->getOffset32();
    return 0;
  }

  /// Return an iterator range over this unit's DIEs, extracting them if needed.
  ///
  /// \returns Iterator range over this unit's DIEs.
  die_iterator_range dies() {
    extractDIEsIfNeeded(false);
    return DieArray;
  }

  /// Dump this unit to \p OS using the given dump options.
  ///
  /// \param OS Output stream to write the dump to.
  /// \param DumpOpts Options controlling dump formatting.
  virtual void dump(raw_ostream &OS, DIDumpOptions DumpOpts) = 0;

  /// Parse this unit's DIEs if needed; return an error instead of reporting it.
  ///
  /// \param CUDieOnly If true, only extract the unit DIE; otherwise extract all DIEs.
  /// \returns Success, or an error if DIE extraction failed.
  Error tryExtractDIEsIfNeeded(bool CUDieOnly);

private:
  /// Size in bytes of the .debug_info data associated with this compile unit.
  size_t getDebugInfoSize() const {
    return Header.getLength() + Header.getUnitLengthFieldByteSize() -
           getHeaderSize();
  }

  /// extractDIEsIfNeeded - Parses a compile unit and indexes its DIEs if it
  /// hasn't already been done
  void extractDIEsIfNeeded(bool CUDieOnly);

  /// extractDIEsToVector - Appends all parsed DIEs to a vector.
  void extractDIEsToVector(bool AppendCUDie, bool AppendNonCUDIEs,
                           std::vector<DWARFDebugInfoEntry> &DIEs) const;

  /// clearDIEs - Clear parsed DIEs to keep memory usage low.
  void clearDIEs(bool KeepCUDie);

  /// parseDWO - Parses .dwo file for current compile unit. Returns true if
  /// it was actually constructed.
  /// The \p AlternativeLocation specifies an alternative location to get
  /// the DWARF context for the DWO object; this is the case when it has
  /// been moved from its original location.
  bool parseDWO(StringRef AlternativeLocation = {});
};

inline bool isCompileUnit(const std::unique_ptr<DWARFUnit> &U) {
  return !U->isTypeUnit();
}

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFUNIT_H
