//===- llvm/IR/DbgVariableFragmentInfo.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helper struct to describe a fragment of a debug variable.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_IR_DBGVARIABLEFRAGMENTINFO_H
#define LLVM_IR_DBGVARIABLEFRAGMENTINFO_H

#include <cstdint>

namespace llvm {
/// Describes a fragment of a debug variable as a bit size and offset.
struct DbgVariableFragmentInfo {
  /// Construct an empty fragment with zero size and offset.
  DbgVariableFragmentInfo() = default;
  /// Construct a fragment with the given size and offset in bits.
  /// \param SizeInBits Size of the fragment in bits.
  /// \param OffsetInBits Bit offset of the fragment within the variable.
  DbgVariableFragmentInfo(uint64_t SizeInBits, uint64_t OffsetInBits)
      : SizeInBits(SizeInBits), OffsetInBits(OffsetInBits) {}
  /// Size of this fragment in bits.
  uint64_t SizeInBits;
  /// Bit offset of this fragment within the variable.
  uint64_t OffsetInBits;
  /// Return the index of the first bit of the fragment.
  /// \return The bit index of the start of the fragment.
  uint64_t startInBits() const { return OffsetInBits; }
  /// Return the index of the bit after the end of the fragment, e.g. for
  /// fragment offset=16 and size=32 return their sum, 48.
  /// \return The bit index immediately after the last bit of the fragment.
  uint64_t endInBits() const { return OffsetInBits + SizeInBits; }

  /// Returns a zero-sized fragment if A and B don't intersect.
  /// \param A First fragment.
  /// \param B Second fragment.
  /// \return The overlapping fragment of \p A and \p B, or a zero-sized
  /// fragment if they do not intersect.
  static DbgVariableFragmentInfo intersect(DbgVariableFragmentInfo A,
                                           DbgVariableFragmentInfo B) {
    // Don't use std::max or min to avoid including <algorithm>.
    uint64_t StartInBits =
        A.OffsetInBits > B.OffsetInBits ? A.OffsetInBits : B.OffsetInBits;
    uint64_t EndInBits =
        A.endInBits() < B.endInBits() ? A.endInBits() : B.endInBits();
    if (EndInBits <= StartInBits)
      return {0, 0};
    return DbgVariableFragmentInfo(EndInBits - StartInBits, StartInBits);
  }
};
} // end namespace llvm

#endif // LLVM_IR_DBGVARIABLEFRAGMENTINFO_H
