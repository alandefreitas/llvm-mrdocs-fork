//===--- SipHash.h - An ABI-stable string SipHash ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An implementation of SipHash, a hash function optimized for speed on
// short inputs. Based on the SipHash reference implementation.
//
// Also provides one specific wrapper on top of SipHash-2-4-64 to compute
// compute ABI-stable ptrauth discriminators.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SIPHASH_H
#define LLVM_SUPPORT_SIPHASH_H

#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

template <typename T> class ArrayRef;
class StringRef;

/// Computes a SipHash-2-4 64-bit result.
///
/// \param In Input bytes to hash.
/// \param K 16-byte SipHash key.
/// \param Out Buffer that receives the 8-byte hash.
LLVM_ABI void getSipHash_2_4_64(ArrayRef<uint8_t> In, const uint8_t (&K)[16],
                                uint8_t (&Out)[8]);

/// Computes a SipHash-2-4 128-bit result.
///
/// \param In Input bytes to hash.
/// \param K 16-byte SipHash key.
/// \param Out Buffer that receives the 16-byte hash.
LLVM_ABI void getSipHash_2_4_128(ArrayRef<uint8_t> In, const uint8_t (&K)[16],
                                 uint8_t (&Out)[16]);

/// Compute a stable 64-bit hash of the given string.
///
/// The exact algorithm is the little-endian interpretation of the
/// non-doubled (i.e. 64-bit) result of applying a SipHash-2-4 using
/// a specific seed value which can be found in the source.
///
/// \param Str String to hash.
/// \return Stable 64-bit SipHash of \p Str.
LLVM_ABI uint64_t getStableSipHash(StringRef Str);

/// Compute a stable non-zero 16-bit hash of the given string.
///
/// The exact algorithm is the little-endian interpretation of the
/// non-doubled (i.e. 64-bit) result of applying a SipHash-2-4 using
/// a specific seed value which can be found in the source.
/// This 64-bit result is truncated to a non-zero 16-bit value.
///
/// We use a 16-bit discriminator because ARM64 can efficiently load
/// a 16-bit immediate into the high bits of a register without disturbing
/// the remainder of the value, which serves as a nice blend operation.
/// 16 bits is also sufficiently compact to not inflate a loader relocation.
/// We disallow zero to guarantee a different discriminator from the places
/// in the ABI that use a constant zero.
///
/// \param S String whose ptrauth discriminator is computed.
/// \return Non-zero 16-bit ptrauth discriminator derived from \p S.
LLVM_ABI uint16_t getPointerAuthStableSipHash(StringRef S);

} // end namespace llvm

#endif
