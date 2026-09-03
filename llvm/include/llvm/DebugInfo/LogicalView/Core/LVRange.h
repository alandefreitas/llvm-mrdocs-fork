//===-- LVRange.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVRange class, which is used to describe a debug
// information range.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVRANGE_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVRANGE_H

#include "llvm/ADT/IntervalTree.h"
#include "llvm/DebugInfo/LogicalView/Core/LVObject.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace logicalview {

/// Pair of lower and upper addresses describing a contiguous range.
using LVAddressRange = std::pair<LVAddress, LVAddress>;

/// One address interval associated with a logical scope.
class LVRangeEntry final {
  LVAddress Lower = 0;
  LVAddress Upper = 0;
  LVScope *Scope = nullptr;

public:
  /// Address type used for the lower and upper bounds of this entry.
  using RangeType = LVAddress;

  /// Default construction is deleted; address bounds and a scope are required.
  LVRangeEntry() = delete;
  /// Construct a range entry for \p Scope covering [\p LowerAddress, \p UpperAddress).
  /// \param LowerAddress Inclusive lower address of the range.
  /// \param UpperAddress Exclusive upper address of the range.
  /// \param Scope Logical scope associated with this address range.
  LVRangeEntry(LVAddress LowerAddress, LVAddress UpperAddress, LVScope *Scope)
      : Lower(LowerAddress), Upper(UpperAddress), Scope(Scope) {}

  /// Return the inclusive lower address of this range entry.
  /// \returns Inclusive lower address of this range entry.
  RangeType lower() const { return Lower; }
  /// Return the exclusive upper address of this range entry.
  /// \returns Exclusive upper address of this range entry.
  RangeType upper() const { return Upper; }
  /// Return the lower and upper addresses as an address-range pair.
  /// \returns Pair of lower and upper addresses for this entry.
  LVAddressRange addressRange() const {
    return LVAddressRange(lower(), upper());
  }
  /// Return the logical scope associated with this range entry.
  /// \returns Logical scope associated with this range entry.
  LVScope *scope() const { return Scope; }
};

/// List of range addresses associated with scopes; stored ascending and may overlap.
using LVRangeEntries = std::vector<LVRangeEntry>;

/// Collection of address ranges associated with logical scopes.
class LLVM_ABI LVRange final : public LVObject {
  /// Map of where a user value is live, and its location.
  using LVRangesTree = IntervalTree<LVAddress, LVScope *>;
  using LVAllocator = LVRangesTree::Allocator;

  LVAllocator Allocator;
  LVRangesTree RangesTree;
  LVRangeEntries RangeEntries;
  LVAddress Lower = MaxAddress;
  LVAddress Upper = 0;

public:
  /// Construct an empty range collection.
  LVRange() : LVObject(), RangesTree(Allocator) {}
  /// Copy construction is not allowed.
  /// \param Other Unused source range collection.
  LVRange(const LVRange &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source range collection.
  LVRange &operator=(const LVRange &Other) = delete;
  /// Destroy the range collection.
  ~LVRange() override = default;

  /// Add a range entry for \p Scope covering [\p LowerAddress, \p UpperAddress].
  /// \param Scope Logical scope associated with the address range.
  /// \param LowerAddress Inclusive lower address of the range.
  /// \param UpperAddress Exclusive upper address of the range.
  void addEntry(LVScope *Scope, LVAddress LowerAddress, LVAddress UpperAddress);
  /// Add range entries for each location range recorded on \p Scope.
  /// \param Scope Scope whose location ranges are added when not already present.
  void addEntry(LVScope *Scope);
  /// Return the deepest scope whose range contains \p Address.
  /// \param Address Address to look up in the interval tree.
  /// \returns Matching scope, or nullptr if none contains the address.
  LVScope *getEntry(LVAddress Address) const;
  /// Return the scope whose range encloses [\p LowerAddress, \p UpperAddress).
  /// \param LowerAddress Inclusive lower address of the query range.
  /// \param UpperAddress Exclusive upper address of the query range.
  /// \returns Matching scope, or nullptr if no enclosing entry is found.
  LVScope *getEntry(LVAddress LowerAddress, LVAddress UpperAddress) const;
  /// Return whether an exact [\p Low, \p High] entry is already recorded.
  /// \param Low Inclusive lower address to match.
  /// \param High Exclusive upper address to match.
  /// \returns True if an identical range entry exists.
  bool hasEntry(LVAddress Low, LVAddress High) const;
  /// Return the lowest address seen across all recorded range entries.
  /// \returns Lowest address across all recorded range entries.
  LVAddress getLower() const { return Lower; }
  /// Return the highest address seen across all recorded range entries.
  /// \returns Highest address across all recorded range entries.
  LVAddress getUpper() const { return Upper; }

  /// Return the ordered list of recorded range entries.
  /// \returns Ordered list of recorded range entries.
  const LVRangeEntries &getEntries() const { return RangeEntries; }

  /// Clear all recorded range entries and reset the overall bounds.
  void clear() {
    RangeEntries.clear();
    Lower = MaxAddress;
    Upper = 0;
  }
  /// Return whether no range entries have been recorded.
  /// \returns True if no range entries have been recorded.
  bool empty() const { return RangeEntries.empty(); }
  /// Sort the recorded range entries by lower address, then size.
  void sort();

  /// Build the interval tree used by address-based scope lookups.
  void startSearch();
  /// Finish a search session over the interval tree.
  void endSearch() {}

  /// Print the recorded range entries to \p OS.
  /// \param OS Stream that receives the printed ranges.
  /// \param Full Whether to print full range details.
  void print(raw_ostream &OS, bool Full = true) const override;
  /// Print kind-specific extra information for this range collection.
  /// \param OS Stream that receives the printed extras.
  /// \param Full Whether to print full extra details.
  void printExtra(raw_ostream &OS, bool Full = true) const override {}
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVRANGE_H
