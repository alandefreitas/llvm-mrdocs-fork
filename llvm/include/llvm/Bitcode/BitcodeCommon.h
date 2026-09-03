//===- BitcodeCommon.h - Common code for encode/decode   --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines common code to be used by BitcodeWriter and
// BitcodeReader.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITCODECOMMON_H
#define LLVM_BITCODE_BITCODECOMMON_H

#include "llvm/ADT/Bitfields.h"

namespace llvm {

/// Bitfield layout for packed flags stored in an alloca bitcode record.
///
/// We increased the number of bits needed to represent alignment to be more
/// than 5, but to preserve backward compatibility we store the upper bits
/// separately.
struct AllocaPackedValues {
  /// Bitfield element for the lower 5 bits of the encoded alignment.
  using AlignLower = Bitfield::Element<unsigned, 0, 5>;
  /// Bitfield element for the inalloca flag.
  using UsedWithInAlloca = Bitfield::Element<bool, AlignLower::NextBit, 1>;
  /// Bitfield element indicating the allocated type is stored explicitly.
  using ExplicitType = Bitfield::Element<bool, UsedWithInAlloca::NextBit, 1>;
  /// Bitfield element for the Swift error flag.
  using SwiftError = Bitfield::Element<bool, ExplicitType::NextBit, 1>;
  /// Bitfield element for the upper bits of the encoded alignment.
  using AlignUpper = Bitfield::Element<unsigned, SwiftError::NextBit, 3>;
};

} // namespace llvm

#endif // LLVM_BITCODE_BITCODECOMMON_H
