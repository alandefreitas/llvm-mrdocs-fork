//====- SHA256.cpp - SHA256 implementation ---*- C++ -* ======//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/*
 *  The SHA-256 Secure Hash Standard was published by NIST in 2002.
 *
 *  http://csrc.nist.gov/publications/fips/fips180-2/fips180-2.pdf
 *
 *   The implementation is based on nacl's sha256 implementation [0] and LLVM's
 *  pre-exsiting SHA1 code [1].
 *
 *   [0] https://hyperelliptic.org/nacl/nacl-20110221.tar.bz2 (public domain
 *       code)
 *   [1] llvm/lib/Support/SHA1.{h,cpp}
 */
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SHA256_H
#define LLVM_SUPPORT_SHA256_H

#include "llvm/Support/Compiler.h"
#include <array>
#include <cstdint>

namespace llvm {

template <typename T> class ArrayRef;
class StringRef;

/// Incremental SHA-256 message-digest computation.
class SHA256 {
public:
  /// Construct a SHA256 hasher with initial state.
  explicit SHA256() { init(); }

  /// Reinitialize the internal state
  LLVM_ABI void init();

  /// Digest more data.
  ///
  /// \param Data Bytes to absorb into the hash state.
  LLVM_ABI void update(ArrayRef<uint8_t> Data);

  /// Digest more data.
  ///
  /// \param Str String bytes to absorb into the hash state.
  LLVM_ABI void update(StringRef Str);

  /// Return the raw 256-bit SHA256 digest and finalize the state.
  ///
  /// This returns the hash for data digested since the last call to init().
  /// This call will add data to the internal state and as such is not suited
  /// for getting an intermediate result (see result()).
  ///
  /// \return The finalized 32-byte SHA-256 digest.
  LLVM_ABI std::array<uint8_t, 32> final();

  /// Return the current raw 256-bit SHA256 digest without finalizing state.
  ///
  /// This is suitable for getting the SHA256 at any time without invalidating
  /// the internal state so that more calls can be made into update.
  ///
  /// \return The current 32-byte SHA-256 digest.
  LLVM_ABI std::array<uint8_t, 32> result();

  /// Returns a raw 256-bit SHA256 hash for the given data.
  ///
  /// \param Data Bytes to hash in a single shot.
  /// \return The 32-byte SHA-256 digest of \p Data.
  LLVM_ABI static std::array<uint8_t, 32> hash(ArrayRef<uint8_t> Data);

private:
  /// Define some constants.
  /// "static constexpr" would be cleaner but MSVC does not support it yet.
  enum { BLOCK_LENGTH = 64 };
  enum { HASH_LENGTH = 32 };

  // Internal State
  struct {
    union {
      uint8_t C[BLOCK_LENGTH];
      uint32_t L[BLOCK_LENGTH / 4];
    } Buffer;
    uint32_t State[HASH_LENGTH / 4];
    uint32_t ByteCount;
    uint8_t BufferOffset;
  } InternalState;

  // Helper
  void writebyte(uint8_t data);
  void hashBlock();
  void addUncounted(uint8_t data);
  void pad();

  void final(std::array<uint32_t, HASH_LENGTH / 4> &HashResult);
};

} // namespace llvm

#endif // LLVM_SUPPORT_SHA256_H
