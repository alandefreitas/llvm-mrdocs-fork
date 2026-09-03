//===-- llvm/Support/CRC.h - Cyclic Redundancy Check-------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains implementations of CRC functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CRC_H
#define LLVM_SUPPORT_CRC_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {
template <typename T> class ArrayRef;

/// Compute the CRC-32 of \p Data.
///
/// \param Data Bytes to checksum.
/// \return The CRC-32 checksum of \p Data.
LLVM_ABI uint32_t crc32(ArrayRef<uint8_t> Data);

/// Compute a running CRC-32 over \p Data from a previous checksum.
///
/// \param CRC Previous CRC-32 value to continue from.
/// \param Data Bytes to fold into the checksum.
/// \return The updated CRC-32 checksum.
LLVM_ABI uint32_t crc32(uint32_t CRC, ArrayRef<uint8_t> Data);

/// Computes JamCRC checksums over a byte stream.
///
/// We will use the "Rocksoft^tm Model CRC Algorithm" to describe the properties
/// of this CRC:
///   Width  : 32
///   Poly   : 04C11DB7
///   Init   : FFFFFFFF
///   RefIn  : True
///   RefOut : True
///   XorOut : 00000000
///   Check  : 340BC6D9 (result of CRC for "123456789")
///
/// In other words, this is the same as CRC-32, except that XorOut is 0 instead
/// of FFFFFFFF.
///
/// N.B.  We permit flexibility of the "Init" value.  Some consumers of this need
///       it to be zero.
class JamCRC {
public:
  /// Construct a JamCRC hasher with the given initial CRC value.
  ///
  /// \param Init Initial CRC state (defaults to 0xFFFFFFFF).
  JamCRC(uint32_t Init = 0xFFFFFFFFU) : CRC(Init) {}

  /// Update the CRC calculation with \p Data.
  ///
  /// \param Data Bytes to fold into the running checksum.
  LLVM_ABI void update(ArrayRef<uint8_t> Data);

  /// Return the current JamCRC value.
  ///
  /// \return The checksum accumulated so far.
  uint32_t getCRC() const { return CRC; }

private:
  uint32_t CRC;
};

} // end namespace llvm

#endif
