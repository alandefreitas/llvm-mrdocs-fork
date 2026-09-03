//===- DWARFAddressRange.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFADDRESSRANGE_H
#define LLVM_DEBUGINFO_DWARF_DWARFADDRESSRANGE_H

#include "llvm/DebugInfo/DIContext.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <tuple>
#include <vector>

namespace llvm {

class raw_ostream;
class DWARFObject;

/// An absolute address range [LowPC, HighPC) within a section.
struct DWARFAddressRange {
  /// Inclusive start address of the range (addresses in [LowPC, HighPC)).
  uint64_t LowPC;
  /// Exclusive end address of the range (addresses in [LowPC, HighPC)).
  uint64_t HighPC;
  /// Section index for LowPC/HighPC, or UndefSection if absolute/unknown.
  uint64_t SectionIndex;

  /// Default-construct an address range (members left uninitialized).
  DWARFAddressRange() = default;

  /// Used for unit testing.
  ///
  /// \param LowPC Inclusive start address of the range.
  /// \param HighPC Exclusive end address of the range.
  /// \param SectionIndex Section index for LowPC/HighPC, or UndefSection.
  DWARFAddressRange(
      uint64_t LowPC, uint64_t HighPC,
      uint64_t SectionIndex = object::SectionedAddress::UndefSection)
      : LowPC(LowPC), HighPC(HighPC), SectionIndex(SectionIndex) {}

  /// Returns true if LowPC is smaller or equal to HighPC. This accounts for
  /// dead-stripped ranges.
  ///
  /// \returns true if LowPC is less than or equal to HighPC.
  bool valid() const { return LowPC <= HighPC; }

  /// Returns true if [LowPC, HighPC) intersects with [RHS.LowPC, RHS.HighPC).
  ///
  /// \param RHS Address range to test for intersection with this range.
  ///
  /// \returns true if this range intersects with \p RHS.
  bool intersects(const DWARFAddressRange &RHS) const {
    assert(valid() && RHS.valid());
    if (SectionIndex != RHS.SectionIndex)
      return false;
    // Empty ranges can't intersect.
    if (LowPC == HighPC || RHS.LowPC == RHS.HighPC)
      return false;
    return LowPC < RHS.HighPC && RHS.LowPC < HighPC;
  }

  /// Union two address ranges if they intersect.
  ///
  /// This function will union two address ranges if they intersect by
  /// modifying this range to be the union of both ranges. If the two ranges
  /// don't intersect this range will be left alone.
  ///
  /// \param RHS Another address range to combine with.
  ///
  /// \returns false if the ranges don't intersect, true if they do and the
  /// ranges were combined.
  bool merge(const DWARFAddressRange &RHS) {
    if (!intersects(RHS))
      return false;
    LowPC = std::min<uint64_t>(LowPC, RHS.LowPC);
    HighPC = std::max<uint64_t>(HighPC, RHS.HighPC);
    return true;
  }

  /// Print this address range to \p OS using \p AddressSize-byte addresses.
  ///
  /// \param OS Output stream to write to.
  /// \param AddressSize Size in bytes used when formatting addresses.
  /// \param DumpOpts Options controlling dump formatting.
  /// \param Obj Optional DWARF object used to resolve section names.
  LLVM_ABI void dump(raw_ostream &OS, uint32_t AddressSize,
                     DIDumpOptions DumpOpts = {},
                     const DWARFObject *Obj = nullptr) const;
};

/// Order by section index, then LowPC, then HighPC.
///
/// \param LHS First address range to compare.
/// \param RHS Second address range to compare.
///
/// \returns true if \p LHS sorts before \p RHS.
inline bool operator<(const DWARFAddressRange &LHS,
                      const DWARFAddressRange &RHS) {
  return std::tie(LHS.SectionIndex, LHS.LowPC, LHS.HighPC) < std::tie(RHS.SectionIndex, RHS.LowPC, RHS.HighPC);
}

/// True if both ranges have the same section index, LowPC, and HighPC.
///
/// \param LHS First address range to compare.
/// \param RHS Second address range to compare.
///
/// \returns true if \p LHS and \p RHS are equal.
inline bool operator==(const DWARFAddressRange &LHS,
                       const DWARFAddressRange &RHS) {
  return std::tie(LHS.SectionIndex, LHS.LowPC, LHS.HighPC) == std::tie(RHS.SectionIndex, RHS.LowPC, RHS.HighPC);
}

/// Print address range \p R to \p OS (8-byte addresses by default).
///
/// \param OS Output stream to write to.
/// \param R Address range to print.
///
/// \returns \p OS after printing \p R.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const DWARFAddressRange &R);

/// DWARFAddressRangesVector - represents a set of absolute address ranges.
using DWARFAddressRangesVector = std::vector<DWARFAddressRange>;

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFADDRESSRANGE_H
