//===- DWARFDebugRnglists.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGRNGLISTS_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGRNGLISTS_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include "llvm/DebugInfo/DWARF/DWARFListTable.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

class Error;
class raw_ostream;
class DWARFUnit;
class DWARFDataExtractor;
struct DIDumpOptions;
namespace object {
struct SectionedAddress;
}

/// A class representing a single range list entry.
struct RangeListEntry : public DWARFListEntryBase {
  /// First value of this range list entry.
  ///
  /// Most encodings represent a range with a start and end address or a start
  /// address and a length. Others are single-value base addresses or
  /// end-of-list with no values. Unused values are semantically undefined but
  /// initialized to 0.
  uint64_t Value0;
  /// Second value of this range list entry.
  uint64_t Value1;

  /// Extract one range list entry from \p Data at \p OffsetPtr.
  ///
  /// \param Data Section data to parse the entry from.
  /// \param OffsetPtr Byte offset into \p Data; advanced past the parsed entry.
  ///
  /// \returns Success, or an error describing why extraction failed.
  LLVM_ABI Error extract(DWARFDataExtractor Data, uint64_t *OffsetPtr);
  /// Dump this range list entry to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  /// \param AddrSize Size in bytes used when formatting addresses.
  /// \param MaxEncodingStringLength Width used to pad the encoding name in
  ///        verbose mode.
  /// \param CurrentBase Running base address updated by base-address entries.
  /// \param DumpOpts Options controlling what and how to dump.
  /// \param LookupPooledAddress Resolves DW_AT_addr_base pooled address
  ///        indices.
  LLVM_ABI void
  dump(raw_ostream &OS, uint8_t AddrSize, uint8_t MaxEncodingStringLength,
       uint64_t &CurrentBase, DIDumpOptions DumpOpts,
       llvm::function_ref<std::optional<object::SectionedAddress>(uint32_t)>
           LookupPooledAddress) const;
  /// Return true if this entry is the end-of-list sentinel.
  ///
  /// \returns True if this is a DW_RLE_end_of_list entry.
  bool isSentinel() const { return EntryKind == dwarf::DW_RLE_end_of_list; }
};

/// A class representing a single rangelist.
class DWARFDebugRnglist : public DWARFListType<RangeListEntry> {
public:
  /// Build a DWARFAddressRangesVector from a rangelist.
  ///
  /// \param BaseAddr Initial base address for relative range entries, if known.
  /// \param AddressByteSize Size in bytes of an address on the target.
  /// \param LookupPooledAddress Resolves DW_AT_addr_base pooled address
  ///        indices.
  ///
  /// \returns Absolute address ranges resolved from this rangelist.
  LLVM_ABI DWARFAddressRangesVector getAbsoluteRanges(
      std::optional<object::SectionedAddress> BaseAddr, uint8_t AddressByteSize,
      function_ref<std::optional<object::SectionedAddress>(uint32_t)>
          LookupPooledAddress) const;

  /// Build a DWARFAddressRangesVector from a rangelist.
  ///
  /// \param BaseAddr Initial base address for relative range entries, if known.
  /// \param U DWARF unit providing address size and pooled address lookup.
  ///
  /// \returns Absolute address ranges resolved from this rangelist.
  LLVM_ABI DWARFAddressRangesVector getAbsoluteRanges(
      std::optional<object::SectionedAddress> BaseAddr, DWARFUnit &U) const;
};

/// A class representing a range list table from the .debug_rnglists section.
class DWARFDebugRnglistTable : public DWARFListTableBase<DWARFDebugRnglist> {
public:
  /// Construct a range list table for the .debug_rnglists section.
  DWARFDebugRnglistTable()
      : DWARFListTableBase(/* SectionName    = */ ".debug_rnglists",
                           /* HeaderString   = */ "ranges:",
                           /* ListTypeString = */ "range") {}
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGRNGLISTS_H
