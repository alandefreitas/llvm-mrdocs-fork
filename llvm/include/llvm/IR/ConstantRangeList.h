//===- ConstantRangeList.h - A list of constant ranges ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Represent a list of signed ConstantRange and do NOT support wrap around the
// end of the numeric range. Ranges in the list are ordered and not overlapping.
// Ranges should have the same bitwidth. Each range's lower should be less than
// its upper.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CONSTANTRANGELIST_H
#define LLVM_IR_CONSTANTRANGELIST_H

#include "llvm/ADT/APInt.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include <cstddef>
#include <cstdint>

namespace llvm {

class raw_ostream;

/// This class represents a list of constant ranges.
class [[nodiscard]] ConstantRangeList {
  SmallVector<ConstantRange, 2> Ranges;

public:
  /// Construct an empty constant range list.
  ConstantRangeList() = default;
  /// Construct a constant range list from ordered, non-overlapping ranges.
  /// \param RangesRef Ranges to copy; must be ordered and non-overlapping.
  ConstantRangeList(ArrayRef<ConstantRange> RangesRef) {
    assert(isOrderedRanges(RangesRef));
    for (const ConstantRange &R : RangesRef) {
      assert(empty() || R.getBitWidth() == getBitWidth());
      Ranges.push_back(R);
    }
  }

  /// Return true if the ranges are non-overlapping and increasing.
  /// \param RangesRef Ranges to check for ordered, non-overlapping layout.
  /// \return True if \p RangesRef is ordered and non-overlapping.
  LLVM_ABI static bool isOrderedRanges(ArrayRef<ConstantRange> RangesRef);
  /// Build a ConstantRangeList from \p RangesRef if the ranges are ordered.
  /// \param RangesRef Candidate ranges; returns nullopt if not ordered.
  /// \return A ConstantRangeList if ordered, otherwise nullopt.
  LLVM_ABI static std::optional<ConstantRangeList>
  getConstantRangeList(ArrayRef<ConstantRange> RangesRef);

  /// Return a read-only view of the underlying ranges.
  /// \return An ArrayRef of the ranges in this list.
  ArrayRef<ConstantRange> rangesRef() const { return Ranges; }
  /// Return an iterator to the first range.
  /// \return An iterator to the first range.
  SmallVectorImpl<ConstantRange>::iterator begin() { return Ranges.begin(); }
  /// Return an iterator past the last range.
  /// \return An iterator past the last range.
  SmallVectorImpl<ConstantRange>::iterator end() { return Ranges.end(); }
  /// Return a const iterator to the first range.
  /// \return A const iterator to the first range.
  SmallVectorImpl<ConstantRange>::const_iterator begin() const {
    return Ranges.begin();
  }
  /// Return a const iterator past the last range.
  /// \return A const iterator past the last range.
  SmallVectorImpl<ConstantRange>::const_iterator end() const {
    return Ranges.end();
  }
  /// Return the range at index \p i.
  /// \param i Zero-based index into the range list.
  /// \return The ConstantRange at index \p i.
  ConstantRange getRange(unsigned i) const { return Ranges[i]; }

  /// Return true if this list contains no members.
  /// \return True if this list contains no members.
  bool empty() const { return Ranges.empty(); }

  /// Get the bit width of this ConstantRangeList. It is invalid to call this
  /// with an empty range.
  /// \return The bit width of the ranges in this list.
  uint32_t getBitWidth() const { return Ranges.front().getBitWidth(); }

  /// Return the number of ranges in this ConstantRangeList.
  /// \return The number of ranges in this list.
  size_t size() const { return Ranges.size(); }

  /// Insert a new range to Ranges and keep the list ordered.
  /// \param NewRange Range to insert; may merge with overlapping neighbors.
  LLVM_ABI void insert(const ConstantRange &NewRange);
  /// Insert the signed 64-bit half-open range [\p Lower, \p Upper).
  /// \param Lower Inclusive signed lower bound.
  /// \param Upper Exclusive signed upper bound.
  void insert(int64_t Lower, int64_t Upper) {
    insert(ConstantRange(APInt(64, Lower, /*isSigned=*/true),
                         APInt(64, Upper, /*isSigned=*/true)));
  }

  /// Remove the values in \p SubRange from every range in this list.
  /// \param SubRange Signed range whose members are subtracted from this list.
  LLVM_ABI void subtract(const ConstantRange &SubRange);

  /// Return the range list that results from the union of this
  /// ConstantRangeList with another ConstantRangeList, "CRL".
  /// \param CRL The other constant range list to union with.
  /// \return The union of this list and \p CRL.
  LLVM_ABI ConstantRangeList unionWith(const ConstantRangeList &CRL) const;

  /// Return the range list that results from the intersection of this
  /// ConstantRangeList with another ConstantRangeList, "CRL".
  /// \param CRL The other constant range list to intersect with.
  /// \return The intersection of this list and \p CRL.
  LLVM_ABI ConstantRangeList intersectWith(const ConstantRangeList &CRL) const;

  /// Return true if this range list is equal to another range list.
  /// \param CRL The other constant range list to compare against.
  /// \return True if this range list equals \p CRL.
  bool operator==(const ConstantRangeList &CRL) const {
    return Ranges == CRL.Ranges;
  }
  /// Return true if this range list is not equal to another range list.
  /// \param CRL The other constant range list to compare against.
  /// \return True if this range list is not equal to \p CRL.
  bool operator!=(const ConstantRangeList &CRL) const {
    return !operator==(CRL);
  }

  /// Print out the ranges to a stream.
  /// \param OS The stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump the ranges to stderr for debugging.
  void dump() const;
#endif
};

} // end namespace llvm

#endif // LLVM_IR_CONSTANTRANGELIST_H
