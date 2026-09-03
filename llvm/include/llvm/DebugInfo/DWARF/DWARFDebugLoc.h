//===- DWARFDebugLoc.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGLOC_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGLOC_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Errc.h"
#include <cstdint>

namespace llvm {
class DWARFUnit;
class MCRegisterInfo;
class raw_ostream;
class DWARFObject;
struct DIDumpOptions;
struct DWARFLocationExpression;
namespace object {
struct SectionedAddress;
}

/// A single location within a location list. Entries are stored in the DWARF5
/// form even if they originally come from a DWARF<=4 location list.
struct DWARFLocationEntry {
  /// The entry kind (DW_LLE_***).
  uint8_t Kind;

  /// The first value of the location entry (if applicable).
  uint64_t Value0;

  /// The second value of the location entry (if applicable).
  uint64_t Value1;

  /// The index of the section this entry is relative to (if applicable).
  uint64_t SectionIndex;

  /// The location expression itself (if applicable).
  SmallVector<uint8_t, 4> Loc;
};

/// An abstract base class for various kinds of location tables (.debug_loc,
/// .debug_loclists, and their dwo variants).
class DWARFLocationTable {
public:
  /// Construct a location table over the given section data.
  ///
  /// \param Data Extractor for the location-list section contents.
  DWARFLocationTable(DWARFDataExtractor Data) : Data(std::move(Data)) {}
  /// Destroy the location table.
  virtual ~DWARFLocationTable() = default;

  /// Visit each entry in the location list starting at \p Offset.
  ///
  /// Call the user-provided callback for each entry (including the end-of-list
  /// entry) in the location list starting at \p Offset. The callback can return
  /// false to terminate the iteration early. Returns an error if it was unable
  /// to parse the entire location list correctly. Upon successful termination
  /// \p Offset will be updated point past the end of the list.
  ///
  /// \param Offset Byte offset of the list; advanced past the end on success.
  /// \param Callback Invoked for each entry; return false to stop early.
  /// \returns Success, or an error if the location list could not be parsed.
  virtual Error visitLocationList(
      uint64_t *Offset,
      function_ref<bool(const DWARFLocationEntry &)> Callback) const = 0;

  /// Dump the location list at the given \p Offset.
  ///
  /// The function returns true iff it has successfully reched the end of the
  /// list. This means that one can attempt to parse another list after the
  /// current one (\p Offset will be updated to point past the end of the
  /// current list).
  ///
  /// \param Offset Byte offset of the list; advanced past the end on success.
  /// \param OS Output stream to write the dump to.
  /// \param BaseAddr Optional base address for resolving relative entries.
  /// \param Obj DWARF object used when formatting addresses and sections.
  /// \param U Optional unit used to resolve address-index entries.
  /// \param DumpOpts Options controlling dump formatting.
  /// \param Indent Number of spaces to indent continuation lines.
  /// \returns True if the end of the list was reached successfully.
  LLVM_ABI bool
  dumpLocationList(uint64_t *Offset, raw_ostream &OS,
                   std::optional<object::SectionedAddress> BaseAddr,
                   const DWARFObject &Obj, DWARFUnit *U, DIDumpOptions DumpOpts,
                   unsigned Indent) const;

  /// Visit each absolute location expression in the list at \p Offset.
  ///
  /// \param Offset Byte offset of the location list within the section.
  /// \param BaseAddr Optional base address for resolving relative entries.
  /// \param LookupAddr Resolves an address index to a sectioned address.
  /// \param Callback Invoked for each resolved expression or resolution error.
  /// \returns Success, or an error if the location list could not be parsed.
  LLVM_ABI Error visitAbsoluteLocationList(
      uint64_t Offset, std::optional<object::SectionedAddress> BaseAddr,
      std::function<std::optional<object::SectionedAddress>(uint32_t)>
          LookupAddr,
      function_ref<bool(Expected<DWARFLocationExpression>)> Callback) const;

  /// Return the underlying location-list section data.
  ///
  /// \returns The DWARFDataExtractor for this location-list section.
  const DWARFDataExtractor &getData() { return Data; }

protected:
  /// Extractor for the location-list section contents.
  DWARFDataExtractor Data;

  /// Dump a single raw location-list entry.
  ///
  /// \param Entry Location-list entry to dump.
  /// \param OS Output stream to write the dump to.
  /// \param Indent Number of spaces to indent the entry.
  /// \param DumpOpts Options controlling dump formatting.
  /// \param Obj DWARF object used when formatting addresses and sections.
  virtual void dumpRawEntry(const DWARFLocationEntry &Entry, raw_ostream &OS,
                            unsigned Indent, DIDumpOptions DumpOpts,
                            const DWARFObject &Obj) const = 0;
};

/// A parser for the DWARF .debug_loc section (pre-DWARF5 location lists).
class LLVM_ABI DWARFDebugLoc final : public DWARFLocationTable {
public:
  /// A list of locations that contain one variable.
  struct LocationList {
    /// The beginning offset where this location list is stored in the debug_loc
    /// section.
    uint64_t Offset;
    /// All the locations in which the variable is stored.
    SmallVector<DWARFLocationEntry, 2> Entries;
  };

private:
  using LocationLists = SmallVector<LocationList, 4>;

  /// A list of all the variables in the debug_loc section, each one describing
  /// the locations in which the variable is stored.
  LocationLists Locations;

public:
  /// Construct a .debug_loc parser over the given section data.
  ///
  /// \param Data Extractor for the .debug_loc section contents.
  DWARFDebugLoc(DWARFDataExtractor Data)
      : DWARFLocationTable(std::move(Data)) {}

  /// Print the location lists found within the debug_loc section.
  ///
  /// \param OS Output stream to write the dump to.
  /// \param Obj DWARF object used when formatting addresses and sections.
  /// \param DumpOpts Options controlling dump formatting.
  /// \param Offset If set, dump only the list at this offset; otherwise dump
  ///        all lists.
  void dump(raw_ostream &OS, const DWARFObject &Obj, DIDumpOptions DumpOpts,
            std::optional<uint64_t> Offset) const;

  /// Visit each entry in a .debug_loc location list starting at \p Offset.
  ///
  /// \param Offset Byte offset of the list; advanced past the end on success.
  /// \param Callback Invoked for each entry; return false to stop early.
  /// \returns Success, or an error if the location list could not be parsed.
  Error visitLocationList(
      uint64_t *Offset,
      function_ref<bool(const DWARFLocationEntry &)> Callback) const override;

protected:
  /// Dump a single raw .debug_loc location-list entry.
  ///
  /// \param Entry Location-list entry to dump.
  /// \param OS Output stream to write the dump to.
  /// \param Indent Number of spaces to indent the entry.
  /// \param DumpOpts Options controlling dump formatting.
  /// \param Obj DWARF object used when formatting addresses and sections.
  void dumpRawEntry(const DWARFLocationEntry &Entry, raw_ostream &OS,
                    unsigned Indent, DIDumpOptions DumpOpts,
                    const DWARFObject &Obj) const override;
};

/// A parser for the DWARF .debug_loclists section (DWARF5 location lists).
class LLVM_ABI DWARFDebugLoclists final : public DWARFLocationTable {
public:
  /// Construct a .debug_loclists parser over the given section data.
  ///
  /// \param Data Extractor for the .debug_loclists section contents.
  /// \param Version DWARF version that produced this loclists section.
  DWARFDebugLoclists(DWARFDataExtractor Data, uint16_t Version)
      : DWARFLocationTable(std::move(Data)), Version(Version) {}

  /// Visit each entry in a .debug_loclists location list starting at \p Offset.
  ///
  /// \param Offset Byte offset of the list; advanced past the end on success.
  /// \param Callback Invoked for each entry; return false to stop early.
  /// \returns Success, or an error if the location list could not be parsed.
  Error visitLocationList(
      uint64_t *Offset,
      function_ref<bool(const DWARFLocationEntry &)> Callback) const override;

  /// Dump all location lists within the given range.
  ///
  /// \param StartOffset Byte offset of the first list to dump.
  /// \param Size Number of bytes from \p StartOffset to dump.
  /// \param OS Output stream to write the dump to.
  /// \param Obj DWARF object used when formatting addresses and sections.
  /// \param DumpOpts Options controlling dump formatting.
  void dumpRange(uint64_t StartOffset, uint64_t Size, raw_ostream &OS,
                 const DWARFObject &Obj, DIDumpOptions DumpOpts);

protected:
  /// Dump a single raw .debug_loclists location-list entry.
  ///
  /// \param Entry Location-list entry to dump.
  /// \param OS Output stream to write the dump to.
  /// \param Indent Number of spaces to indent the entry.
  /// \param DumpOpts Options controlling dump formatting.
  /// \param Obj DWARF object used when formatting addresses and sections.
  void dumpRawEntry(const DWARFLocationEntry &Entry, raw_ostream &OS,
                    unsigned Indent, DIDumpOptions DumpOpts,
                    const DWARFObject &Obj) const override;

private:
  uint16_t Version;
};

/// Error reporting failure to resolve an indirect address in a location list.
class LLVM_ABI ResolverError : public ErrorInfo<ResolverError> {
public:
  /// ErrorInfo ID for ResolverError.
  static char ID;

  /// Construct a resolver error for the given address index and entry kind.
  ///
  /// \param Index Address index that could not be resolved.
  /// \param Kind Location-list entry encoding that referenced the index.
  ResolverError(uint32_t Index, dwarf::LoclistEntries Kind) : Index(Index), Kind(Kind) {}

  /// Write a human-readable description of this error to \p OS.
  ///
  /// \param OS Output stream to write the description to.
  void log(raw_ostream &OS) const override;
  /// Convert this resolver error to a std::error_code.
  ///
  /// \returns An error code of llvm::errc::invalid_argument.
  std::error_code convertToErrorCode() const override {
    return llvm::errc::invalid_argument;
  }

private:
  uint32_t Index;
  dwarf::LoclistEntries Kind;
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGLOC_H
