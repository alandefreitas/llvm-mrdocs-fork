//===- AddressRanges.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ADDRESSRANGES_H
#define LLVM_ADT_ADDRESSRANGES_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <optional>
#include <stdint.h>

namespace llvm {

/// A class that represents an address range. The range is specified using
/// a start and an end address: [Start, End).
class AddressRange {
public:
  /// Construct an empty range [0, 0).
  AddressRange() = default;
  /// Construct the half-open range [\p S, \p E).
  /// @param S Inclusive start address.
  /// @param E Exclusive end address; must be >= \p S.
  AddressRange(uint64_t S, uint64_t E) : Start(S), End(E) {
    assert(Start <= End);
  }
  /// Return the inclusive start address.
  uint64_t start() const { return Start; }
  /// Return the exclusive end address.
  uint64_t end() const { return End; }
  /// Return the number of addresses in the range.
  uint64_t size() const { return End - Start; }
  /// Return true if the range contains no addresses.
  uint64_t empty() const { return size() == 0; }
  /// Return true if \p Addr lies in [start(), end()).
  /// @param Addr Address to test.
  bool contains(uint64_t Addr) const { return Start <= Addr && Addr < End; }
  /// Return true if this range fully contains \p R.
  /// @param R Range that must lie entirely inside this one.
  bool contains(const AddressRange &R) const {
    return Start <= R.Start && R.End <= End;
  }
  /// Return true if this range and \p R share any address.
  /// @param R Range to test for overlap.
  bool intersects(const AddressRange &R) const {
    return Start < R.End && R.Start < End;
  }
  /// Return true if both ranges have the same start and end.
  /// @param R Range to compare against.
  bool operator==(const AddressRange &R) const {
    return Start == R.Start && End == R.End;
  }
  /// Return true if the ranges differ in start or end.
  /// @param R Range to compare against.
  bool operator!=(const AddressRange &R) const { return !(*this == R); }
  /// Order ranges by (start, end) lexicographically.
  /// @param R Range to compare against.
  bool operator<(const AddressRange &R) const {
    return std::make_pair(Start, End) < std::make_pair(R.Start, R.End);
  }

private:
  uint64_t Start = 0;
  uint64_t End = 0;
};

/// The AddressRangesBase class presents the base functionality for the
/// normalized address ranges collection. This class keeps a sorted vector
/// of AddressRange-like objects and can perform searches efficiently.
/// The address ranges are always sorted and never contain any invalid,
/// empty or intersected address ranges.

template <typename T> class AddressRangesBase {
protected:
  /// Sorted vector of address-range-like entries.
  using Collection = SmallVector<T>;
  /// Storage for the normalized, non-overlapping ranges.
  Collection Ranges;

public:
  /// Remove all ranges from the collection.
  void clear() { Ranges.clear(); }
  /// Return true if the collection holds no ranges.
  bool empty() const { return Ranges.empty(); }
  /// Return true if some stored range contains \p Addr.
  /// @param Addr Address to look up.
  bool contains(uint64_t Addr) const {
    return find(Addr, Addr + 1) != Ranges.end();
  }
  /// Return true if some stored range fully contains \p Range.
  /// @param Range Address range to look up.
  bool contains(AddressRange Range) const {
    return find(Range.start(), Range.end()) != Ranges.end();
  }
  /// Reserve capacity for at least \p Capacity range entries.
  /// @param Capacity Minimum number of entries to reserve.
  void reserve(size_t Capacity) { Ranges.reserve(Capacity); }
  /// Return the number of stored ranges.
  size_t size() const { return Ranges.size(); }

  /// Return the stored entry that contains \p Addr, if any.
  /// @param Addr Address to look up.
  std::optional<T> getRangeThatContains(uint64_t Addr) const {
    typename Collection::const_iterator It = find(Addr, Addr + 1);
    if (It == Ranges.end())
      return std::nullopt;

    return *It;
  }

  /// Return an iterator to the first stored range.
  typename Collection::const_iterator begin() const { return Ranges.begin(); }
  /// Return an iterator past the last stored range.
  typename Collection::const_iterator end() const { return Ranges.end(); }

  /// Return the range entry at index \p I.
  /// @param I Zero-based index into the sorted collection.
  const T &operator[](size_t I) const {
    assert(I < Ranges.size());
    return Ranges[I];
  }

  /// Return true if both collections store the same ranges in order.
  /// @param RHS Collection to compare against.
  bool operator==(const AddressRangesBase &RHS) const {
    return Ranges == RHS.Ranges;
  }

protected:
  /// Find a stored range that fully contains [\p Start, \p End).
  /// @param Start Inclusive start of the query range.
  /// @param End Exclusive end of the query range.
  /// @return Iterator to a containing entry, or end() if none.
  typename Collection::const_iterator find(uint64_t Start, uint64_t End) const {
    if (Start >= End)
      return Ranges.end();

    auto It = llvm::partition_point(
        Ranges, [=](const T &R) { return AddressRange(R).start() <= Start; });

    if (It == Ranges.begin())
      return Ranges.end();

    --It;
    if (End > AddressRange(*It).end())
      return Ranges.end();

    return It;
  }
};

/// The AddressRanges class helps normalize address range collections.
/// This class keeps a sorted vector of AddressRange objects and can perform
/// insertions and searches efficiently. Intersecting([100,200), [150,300))
/// and adjacent([100,200), [200,300)) address ranges are combined during
/// insertion.
class AddressRanges : public AddressRangesBase<AddressRange> {
public:
  /// Insert \p Range, merging with any intersecting or adjacent ranges.
  /// @param Range Address range to insert; empty ranges are ignored.
  /// @return Iterator to the (possibly merged) stored range, or end() if empty.
  Collection::const_iterator insert(AddressRange Range) {
    if (Range.empty())
      return Ranges.end();

    auto It = upper_bound(Ranges, Range);
    auto It2 = It;
    while (It2 != Ranges.end() && It2->start() <= Range.end())
      ++It2;
    if (It != It2) {
      Range = {Range.start(), std::max(Range.end(), std::prev(It2)->end())};
      It = Ranges.erase(It, It2);
    }
    if (It != Ranges.begin() && Range.start() <= std::prev(It)->end()) {
      --It;
      *It = {It->start(), std::max(It->end(), Range.end())};
      return It;
    }

    return Ranges.insert(It, Range);
  }
};

/// An address range paired with an associated integer value.
class AddressRangeValuePair {
public:
  /// Convert to the address range portion of this pair.
  explicit operator AddressRange() const { return Range; }

  /// The address range covered by this entry.
  AddressRange Range;
  /// Value mapped to \c Range.
  int64_t Value = 0;
};

/// Return true if \p LHS and \p RHS have the same range and value.
/// @param LHS First pair.
/// @param RHS Second pair.
inline bool operator==(const AddressRangeValuePair &LHS,
                       const AddressRangeValuePair &RHS) {
  return LHS.Range == RHS.Range && LHS.Value == RHS.Value;
}

/// AddressRangesMap class maps values to the address ranges.
/// It keeps normalized address ranges and corresponding values.
/// This class keeps a sorted vector of AddressRangeValuePair objects
/// and can perform insertions and searches efficiently.
/// Intersecting([100,200), [150,300)) ranges splitted into non-conflicting
/// parts([100,200), [200,300)). Adjacent([100,200), [200,300)) address
/// ranges are not combined during insertion.
class AddressRangesMap : public AddressRangesBase<AddressRangeValuePair> {
public:
  /// Map \p Value onto \p Range, splitting around any existing coverage.
  /// @param Range Address range to cover; empty ranges are ignored.
  /// @param Value Integer associated with the newly covered segments.
  void insert(AddressRange Range, int64_t Value) {
    if (Range.empty())
      return;

    // Search for range which is less than or equal incoming Range.
    auto It =
        llvm::partition_point(Ranges, [=](const AddressRangeValuePair &R) {
          return R.Range.start() <= Range.start();
        });

    if (It != Ranges.begin())
      It--;

    while (!Range.empty()) {
      // Inserted range does not overlap with any range.
      // Store it into the Ranges collection.
      if (It == Ranges.end() || Range.end() <= It->Range.start()) {
        Ranges.insert(It, {Range, Value});
        return;
      }

      // Inserted range partially overlaps with current range.
      // Store not overlapped part of inserted range.
      if (Range.start() < It->Range.start()) {
        It = Ranges.insert(It, {{Range.start(), It->Range.start()}, Value});
        It++;
        Range = {It->Range.start(), Range.end()};
        continue;
      }

      // Inserted range fully overlaps with current range.
      if (Range.end() <= It->Range.end())
        return;

      // Inserted range partially overlaps with current range.
      // Remove overlapped part from the inserted range.
      if (Range.start() < It->Range.end())
        Range = {It->Range.end(), Range.end()};

      It++;
    }
  }
};

} // namespace llvm

#endif // LLVM_ADT_ADDRESSRANGES_H
