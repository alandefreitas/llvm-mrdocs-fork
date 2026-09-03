//===- DWARFDebugRangeList.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGRANGELIST_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGRANGELIST_H

#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <vector>

namespace llvm {

class raw_ostream;
class DWARFDataExtractor;
namespace object {
struct SectionedAddress;
}

class DWARFDebugRangeList {
public:
  /// A single entry in a DWARF .debug_ranges range list.
  struct RangeListEntry {
    /// Beginning address offset of this range.
    ///
    /// This address offset has the size of an address and is relative to the
    /// applicable base address of the compilation unit referencing this range
    /// list. It marks the beginning of an address range.
    uint64_t StartAddress;
    /// Ending address offset of this range.
    ///
    /// This address offset again has the size of an address and is relative to
    /// the applicable base address of the compilation unit referencing this
    /// range list. It marks the first address past the end of the address
    /// range. The ending address must be greater than or equal to the beginning
    /// address.
    uint64_t EndAddress;
    /// A section index this range belongs to.
    uint64_t SectionIndex;

    /// The end of any given range list is marked by an end of list entry,
    /// which consists of a 0 for the beginning address offset
    /// and a 0 for the ending address offset.
    ///
    /// \returns True if this entry is an end-of-list entry.
    bool isEndOfListEntry() const {
      return (StartAddress == 0) && (EndAddress == 0);
    }

    /// Return true if this entry is a base address selection entry.
    ///
    /// A base address selection entry consists of:
    /// 1. The value of the largest representable address offset
    /// (for example, 0xffffffff when the size of an address is 32 bits).
    /// 2. An address, which defines the appropriate base address for
    /// use in interpreting the beginning and ending address offsets of
    /// subsequent entries of the location list.
    ///
    /// \param AddressSize Size in bytes of an address on the target.
    ///
    /// \returns True if this entry is a base address selection entry.
    LLVM_ABI bool isBaseAddressSelectionEntry(uint8_t AddressSize) const;
  };

private:
  /// Offset in .debug_ranges section.
  uint64_t Offset;
  uint8_t AddressSize;
  std::vector<RangeListEntry> Entries;

public:
  /// Construct an empty range list.
  DWARFDebugRangeList() { clear(); }

  /// Reset this range list to an empty state.
  LLVM_ABI void clear();
  /// Dump this range list to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  LLVM_ABI void dump(raw_ostream &OS) const;
  /// Extract a range list from \p data starting at \p offset_ptr.
  ///
  /// \param data .debug_ranges section contents to parse from.
  /// \param offset_ptr Byte offset into \p data; advanced past the parsed list.
  ///
  /// \returns Success, or an error describing why extraction failed.
  LLVM_ABI Error extract(const DWARFDataExtractor &data, uint64_t *offset_ptr);
  /// Return the entries that make up this range list.
  ///
  /// \returns The range list entries.
  const std::vector<RangeListEntry> &getEntries() { return Entries; }

  /// getAbsoluteRanges - Returns absolute address ranges defined by this range
  /// list. Has to be passed base address of the compile unit referencing this
  /// range list.
  ///
  /// \param BaseAddr Base address of the compile unit referencing this range
  /// list, or std::nullopt if none is known.
  ///
  /// \returns Absolute address ranges defined by this range list.
  LLVM_ABI DWARFAddressRangesVector
  getAbsoluteRanges(std::optional<object::SectionedAddress> BaseAddr) const;
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGRANGELIST_H
