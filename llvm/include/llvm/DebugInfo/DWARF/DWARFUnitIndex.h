//===- DWARFUnitIndex.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFUNITINDEX_H
#define LLVM_DEBUGINFO_DWARF_DWARFUNITINDEX_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <memory>

namespace llvm {

class raw_ostream;
class DataExtractor;

/// The enum of section identifiers to be used in internal interfaces.
///
/// Pre-standard implementation of package files defined a number of section
/// identifiers with values that clash definitions in the DWARFv5 standard.
/// See https://gcc.gnu.org/wiki/DebugFissionDWP and Section 7.3.5.3 in DWARFv5.
///
/// The following identifiers are the same in the proposal and in DWARFv5:
/// - DW_SECT_INFO         = 1 (.debug_info.dwo)
/// - DW_SECT_ABBREV       = 3 (.debug_abbrev.dwo)
/// - DW_SECT_LINE         = 4 (.debug_line.dwo)
/// - DW_SECT_STR_OFFSETS  = 6 (.debug_str_offsets.dwo)
///
/// The following identifiers are defined only in DWARFv5:
/// - DW_SECT_LOCLISTS     = 5 (.debug_loclists.dwo)
/// - DW_SECT_RNGLISTS     = 8 (.debug_rnglists.dwo)
///
/// The following identifiers are defined only in the GNU proposal:
/// - DW_SECT_TYPES        = 2 (.debug_types.dwo)
/// - DW_SECT_LOC          = 5 (.debug_loc.dwo)
/// - DW_SECT_MACINFO      = 7 (.debug_macinfo.dwo)
///
/// DW_SECT_MACRO for the .debug_macro.dwo section is defined in both standards,
/// but with different values, 8 in GNU and 7 in DWARFv5.
///
/// This enum defines constants to represent the identifiers of both sets.
/// For DWARFv5 ones, the values are the same as defined in the standard.
/// For pre-standard ones that correspond to sections being deprecated in
/// DWARFv5, the values are chosen arbitrary and a tag "_EXT_" is added to
/// the names.
///
/// The enum is for internal use only. The user should not expect the values
/// to correspond to any input/output constants. Special conversion functions,
/// serializeSectionKind() and deserializeSectionKind(), should be used for
/// the translation.
enum DWARFSectionKind {
  /// Denotes a value read from an index section that does not correspond
  /// to any of the supported standards.
  DW_SECT_EXT_unknown = 0,
/// Expand one DWARF package-file section kind enumerator from Dwarf.def.
///
/// \param ID Numeric section kind identifier.
/// \param NAME Enumerator name suffix (forms \c DW_SECT_##NAME).
#define HANDLE_DW_SECT(ID, NAME) DW_SECT_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_SECT_EXT_TYPES = 2, ///< Pre-standard GNU .debug_types.dwo section kind.
  DW_SECT_EXT_LOC = 9,      ///< Pre-standard GNU .debug_loc.dwo section kind.
  DW_SECT_EXT_MACINFO = 10, ///< Pre-standard GNU .debug_macinfo.dwo section kind.
};

/// Return a printable name for DWARF package-file section kind \p Kind.
///
/// \param Kind Section kind to convert to a printable name.
/// \return Null-terminated printable name for \p Kind.
inline const char *toString(DWARFSectionKind Kind) {
  switch (Kind) {
  case DW_SECT_EXT_unknown:
    return "Unknown DW_SECT value 0";
#define STRINGIZE(X) #X
#define HANDLE_DW_SECT(ID, NAME)                                               \
  case DW_SECT_##NAME:                                                         \
    return "DW_SECT_" STRINGIZE(NAME);
#include "llvm/BinaryFormat/Dwarf.def"
  case DW_SECT_EXT_TYPES:
    return "DW_SECT_TYPES";
  case DW_SECT_EXT_LOC:
    return "DW_SECT_LOC";
  case DW_SECT_EXT_MACINFO:
    return "DW_SECT_MACINFO";
  }
  llvm_unreachable("unknown DWARFSectionKind");
}

/// Convert the internal value for a section kind to an on-disk value.
///
/// The conversion depends on the version of the index section.
/// IndexVersion is expected to be either 2 for pre-standard GNU proposal
/// or 5 for DWARFv5 package file.
///
/// \param Kind Internal section kind to serialize.
/// \param IndexVersion Index section version (2 for GNU, 5 for DWARFv5).
/// \return On-disk section kind identifier for \p Kind.
LLVM_ABI uint32_t serializeSectionKind(DWARFSectionKind Kind,
                                       unsigned IndexVersion);

/// Convert a value read from an index section to the internal representation.
///
/// The conversion depends on the index section version, which is expected
/// to be either 2 for pre-standard GNU proposal or 5 for DWARFv5 package file.
///
/// \param Value On-disk section kind identifier from the index.
/// \param IndexVersion Index section version (2 for GNU, 5 for DWARFv5).
/// \return Internal \c DWARFSectionKind corresponding to \p Value.
LLVM_ABI DWARFSectionKind deserializeSectionKind(uint32_t Value,
                                                 unsigned IndexVersion);

/// A parsed .debug_cu_index or .debug_tu_index table from a DWARF package file.
class DWARFUnitIndex {
  struct Header {
    uint32_t Version;
    uint32_t NumColumns;
    uint32_t NumUnits;
    uint32_t NumBuckets = 0;

    LLVM_ABI bool parse(DataExtractor IndexData, uint64_t *OffsetPtr);
    LLVM_ABI void dump(raw_ostream &OS) const;
  };

public:
  /// One hashed row in a .debug_cu_index / .debug_tu_index table.
  class Entry {
  public:
    /// Offset and length of one unit's contribution to a DWARF section.
    class SectionContribution {
    private:
      uint64_t Offset;
      uint64_t Length;

    public:
      /// Construct an empty contribution with zero offset and length.
      SectionContribution() : Offset(0), Length(0) {}
      /// Construct a contribution covering \p Length bytes at \p Offset.
      ///
      /// \param Offset Offset of this contribution within its section.
      /// \param Length Length of this contribution in bytes.
      SectionContribution(uint64_t Offset, uint64_t Length)
          : Offset(Offset), Length(Length) {}

      /// Set the offset of this section contribution within its section.
      ///
      /// \param Value New offset within the section.
      void setOffset(uint64_t Value) { Offset = Value; }
      /// Set the length of this section contribution in bytes.
      ///
      /// \param Value New length in bytes.
      void setLength(uint64_t Value) { Length = Value; }
      /// Offset of this section contribution within its section.
      ///
      /// \return Offset of this contribution within its section.
      uint64_t getOffset() const { return Offset; }
      /// Length of this section contribution in bytes.
      ///
      /// \return Length of this contribution in bytes.
      uint64_t getLength() const { return Length; }
      /// Offset of this contribution truncated to 32 bits.
      ///
      /// \return Offset truncated to 32 bits.
      uint32_t getOffset32() const { return (uint32_t)Offset; }
      /// Length of this contribution truncated to 32 bits.
      ///
      /// \return Length truncated to 32 bits.
      uint32_t getLength32() const { return (uint32_t)Length; }
    };

  private:
    const DWARFUnitIndex *Index;
    uint64_t Signature;
    std::unique_ptr<SectionContribution[]> Contributions;
    friend class DWARFUnitIndex;

  public:
    /// Return the contribution for section kind \p Sec, or nullptr if absent.
    ///
    /// \param Sec Section kind whose contribution to look up.
    /// \return Pointer to the contribution, or nullptr if absent.
    LLVM_ABI const SectionContribution *
    getContribution(DWARFSectionKind Sec) const;
    /// Contribution for the index's info column (CU/TU .debug_info/.debug_types).
    ///
    /// \return Pointer to the info-column contribution.
    LLVM_ABI const SectionContribution *getContribution() const;
    /// Mutable contribution for the index's info column.
    ///
    /// \return Mutable reference to the info-column contribution.
    LLVM_ABI SectionContribution &getContribution();

    /// Contiguous array of per-column section contributions for this entry.
    ///
    /// \return Pointer to the contiguous array of section contributions.
    const SectionContribution *getContributions() const {
      return Contributions.get();
    }

    /// Unit signature hash for this index entry.
    ///
    /// \return Unit signature hash for this index entry.
    uint64_t getSignature() const { return Signature; }
    /// True if this entry is associated with a parsed unit index.
    ///
    /// \return True if this entry is associated with a parsed unit index.
    bool isValid() { return Index; }
  };

private:
  struct Header Header;

  DWARFSectionKind InfoColumnKind;
  int InfoColumn = -1;
  std::unique_ptr<DWARFSectionKind[]> ColumnKinds;
  // This is a parallel array of section identifiers as they read from the input
  // file. The mapping from raw values to DWARFSectionKind is not revertable in
  // case of unknown identifiers, so we keep them here.
  std::unique_ptr<uint32_t[]> RawSectionIds;
  std::unique_ptr<Entry[]> Rows;
  mutable std::vector<Entry *> OffsetLookup;

  static StringRef getColumnHeader(DWARFSectionKind DS);

  bool parseImpl(DataExtractor IndexData);

public:
  /// Construct an empty unit index for info columns of kind \p InfoColumnKind.
  ///
  /// \param InfoColumnKind Section kind identifying the info column
  /// (e.g. DW_SECT_INFO or DW_SECT_EXT_TYPES).
  DWARFUnitIndex(DWARFSectionKind InfoColumnKind)
      : InfoColumnKind(InfoColumnKind) {}

  /// True if this index has been parsed and contains buckets.
  ///
  /// \return True if this index has been parsed and contains buckets.
  explicit operator bool() const { return Header.NumBuckets; }

  /// Parse a .debug_cu_index / .debug_tu_index table from \p IndexData.
  ///
  /// \param IndexData Raw contents of the unit index section.
  /// \return True if the index was parsed successfully.
  LLVM_ABI bool parse(DataExtractor IndexData);
  /// Print this unit index table to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  LLVM_ABI void dump(raw_ostream &OS) const;

  /// Index format version from the parsed header.
  ///
  /// \return Index format version from the parsed header.
  uint32_t getVersion() const { return Header.Version; }

  /// Return the index entry whose info contribution covers \p Offset, or nullptr.
  ///
  /// \param Offset Byte offset into the info section to look up.
  /// \return Pointer to the matching entry, or nullptr if not found.
  LLVM_ABI const Entry *getFromOffset(uint64_t Offset) const;
  /// Return the index entry whose signature hash is \p Offset, or nullptr.
  ///
  /// \param Offset Unit signature hash to look up in the index.
  /// \return Pointer to the matching entry, or nullptr if not found.
  LLVM_ABI const Entry *getFromHash(uint64_t Offset) const;

  /// Return the section kinds for each column of this unit index.
  ///
  /// \return Array of section kinds, one per index column.
  ArrayRef<DWARFSectionKind> getColumnKinds() const {
    return ArrayRef(ColumnKinds.get(), Header.NumColumns);
  }

  /// Return the hash-table rows of this unit index.
  ///
  /// \return Array reference over the hash-table rows.
  ArrayRef<Entry> getRows() const {
    return ArrayRef(Rows.get(), Header.NumBuckets);
  }

  /// Mutable view of the hash-table rows in this unit index.
  ///
  /// \return Mutable array reference over the hash-table rows.
  MutableArrayRef<Entry> getMutableRows() {
    return MutableArrayRef(Rows.get(), Header.NumBuckets);
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFUNITINDEX_H
