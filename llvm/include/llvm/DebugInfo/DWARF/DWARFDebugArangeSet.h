//===- DWARFDebugArangeSet.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGARANGESET_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGARANGESET_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace llvm {

class raw_ostream;
class DWARFDataExtractor;

/// A single address range set from the .debug_aranges section.
///
/// Corresponds to one arange set as defined by the DWARF specification: a
/// header describing the referenced compilation unit, followed by a list of
/// address range descriptors.
class DWARFDebugArangeSet {
public:
  /// Header fields for one .debug_aranges address range set.
  struct Header {
    /// The total length of the entries for that set, not including the length
    /// field itself.
    uint64_t Length;
    /// The DWARF format of the set.
    dwarf::DwarfFormat Format;
    /// The offset from the beginning of the .debug_info section of the
    /// compilation unit entry referenced by the table.
    uint64_t CuOffset;
    /// The DWARF version number.
    uint16_t Version;
    /// The size in bytes of an address on the target architecture. For segmented
    /// addressing, this is the size of the offset portion of the address.
    uint8_t AddrSize;
    /// The size in bytes of a segment descriptor on the target architecture.
    /// If the target system uses a flat address space, this value is 0.
    uint8_t SegSize;
  };

  /// One address range descriptor within an arange set.
  struct Descriptor {
    /// Starting address of the range covered by this descriptor.
    uint64_t Address;
    /// Length in bytes of the range covered by this descriptor.
    uint64_t Length;

    /// Return the exclusive end address of this range (Address + Length).
    ///
    /// \returns Address plus Length.
    uint64_t getEndAddress() const { return Address + Length; }
    /// Print this descriptor to \p OS using \p AddressSize-byte addresses.
    ///
    /// \param OS Output stream to write to.
    /// \param AddressSize Size in bytes used when formatting addresses.
    LLVM_ABI void dump(raw_ostream &OS, uint32_t AddressSize) const;
  };

private:
  using DescriptorColl = std::vector<Descriptor>;
  using desc_iterator_range = iterator_range<DescriptorColl::const_iterator>;

  uint64_t Offset;
  Header HeaderData;
  DescriptorColl ArangeDescriptors;

public:
  /// Construct an empty address range set.
  DWARFDebugArangeSet() { clear(); }

  /// Reset this set to an empty state.
  LLVM_ABI void clear();
  /// Extract one address range set from \p data starting at \p offset_ptr.
  ///
  /// \param data .debug_aranges section contents to parse from.
  /// \param offset_ptr Byte offset into \p data; advanced past the parsed set.
  /// \param WarningHandler Optional callback invoked for non-fatal warnings.
  ///
  /// \returns Success, or an error describing why extraction failed.
  LLVM_ABI Error extract(DWARFDataExtractor data, uint64_t *offset_ptr,
                         function_ref<void(Error)> WarningHandler = nullptr);
  /// Dump this address range set to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  LLVM_ABI void dump(raw_ostream &OS) const;

  /// Return the .debug_info offset of the referenced compilation unit DIE.
  ///
  /// \returns The CuOffset field from this set's header.
  uint64_t getCompileUnitDIEOffset() const { return HeaderData.CuOffset; }

  /// Return the parsed header for this address range set.
  ///
  /// \returns A const reference to the Header for this set.
  const Header &getHeader() const { return HeaderData; }

  /// Return an iterator range over the address range descriptors in this set.
  ///
  /// \returns A const iterator range over the descriptors in this set.
  desc_iterator_range descriptors() const {
    return desc_iterator_range(ArangeDescriptors.begin(),
                               ArangeDescriptors.end());
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGARANGESET_H
