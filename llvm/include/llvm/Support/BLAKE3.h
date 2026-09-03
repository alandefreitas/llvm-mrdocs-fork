//==- BLAKE3.h - BLAKE3 C++ wrapper for LLVM ---------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a C++ wrapper of the BLAKE3 C interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BLAKE3_H
#define LLVM_SUPPORT_BLAKE3_H

#include "llvm-c/blake3.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {

/// The constant \p LLVM_BLAKE3_OUT_LEN provides the default output length,
/// 32 bytes, which is recommended for most callers.
///
/// Outputs shorter than the default length of 32 bytes (256 bits) provide
/// less security. An N-bit BLAKE3 output is intended to provide N bits of
/// first and second preimage resistance and N/2 bits of collision
/// resistance, for any N up to 256. Longer outputs don't provide any
/// additional security.
///
/// Shorter BLAKE3 outputs are prefixes of longer ones. Explicitly
/// requesting a short output is equivalent to truncating the default-length
/// output.
template <size_t NumBytes = LLVM_BLAKE3_OUT_LEN>
using BLAKE3Result = std::array<uint8_t, NumBytes>;

/// A class that wraps the BLAKE3 algorithm.
class BLAKE3 {
public:
  /// Construct a hasher and initialize its internal state.
  BLAKE3() { init(); }

  /// Reinitialize the internal state
  void init() { llvm_blake3_hasher_init(&Hasher); }

  /// Reinitialize the internal state with the given key.
  ///
  /// \param Key Key material used to initialize keyed hashing.
  void init_keyed(ArrayRef<uint8_t> Key) {
    // TODO: maybe assert on the size of the key?
    llvm_blake3_hasher_init_keyed(&Hasher, Key.data());
  }

  /// Digest more data.
  ///
  /// \param Data Bytes to absorb into the hash state.
  void update(ArrayRef<uint8_t> Data) {
    llvm_blake3_hasher_update(&Hasher, Data.data(), Data.size());
  }

  /// Digest more data.
  ///
  /// \param Str String bytes to absorb into the hash state.
  void update(StringRef Str) {
    llvm_blake3_hasher_update(&Hasher, Str.data(), Str.size());
  }

  /// Finalize the hasher and put the result in \p Result.
  ///
  /// This doesn't modify the hasher itself, and it's possible to finalize again
  /// after adding more input.
  ///
  /// \param Result Buffer that receives the finalized hash output.
  template <size_t NumBytes = LLVM_BLAKE3_OUT_LEN>
  void final(BLAKE3Result<NumBytes> &Result) {
    llvm_blake3_hasher_finalize(&Hasher, Result.data(), Result.size());
  }

  /// Finalize the hasher and return a hash of the requested length.
  ///
  /// This doesn't modify the hasher itself, and it's possible to finalize again
  /// after adding more input.
  ///
  /// \returns The finalized hash of the requested length.
  template <size_t NumBytes = LLVM_BLAKE3_OUT_LEN>
  BLAKE3Result<NumBytes> final() {
    BLAKE3Result<NumBytes> Result;
    llvm_blake3_hasher_finalize(&Hasher, Result.data(), Result.size());
    return Result;
  }

  /// Return the current hash of the digested data since the last init().
  ///
  /// Other hash functions distinguish between \p result() and \p final(), with
  /// \p result() allowing more calls into \p update(), but there's no
  /// difference for the BLAKE3 hash function.
  ///
  /// \returns The current hash of the digested data.
  template <size_t NumBytes = LLVM_BLAKE3_OUT_LEN>
  BLAKE3Result<NumBytes> result() {
    return final<NumBytes>();
  }

  /// Returns a BLAKE3 hash for the given data.
  ///
  /// \param Data Bytes to hash in one shot.
  /// \returns The BLAKE3 hash of \p Data.
  template <size_t NumBytes = LLVM_BLAKE3_OUT_LEN>
  static BLAKE3Result<NumBytes> hash(ArrayRef<uint8_t> Data) {
    BLAKE3 Hasher;
    Hasher.update(Data);
    return Hasher.final<NumBytes>();
  }

private:
  llvm_blake3_hasher Hasher;
};

/// Like \p BLAKE3 but using a class-level template parameter for specifying the
/// hash size of the \p final() and \p result() functions.
///
/// This is useful for using BLAKE3 as the hasher type for \p HashBuilder with
/// non-default hash sizes.
template <size_t NumBytes> class TruncatedBLAKE3 : public BLAKE3 {
public:
  /// Finalize the hasher and put the result in \p Result.
  ///
  /// This doesn't modify the hasher itself, and it's possible to finalize again
  /// after adding more input.
  ///
  /// \param Result Buffer that receives the finalized hash output.
  void final(BLAKE3Result<NumBytes> &Result) { return BLAKE3::final(Result); }

  /// Finalize the hasher and return a hash of the requested length.
  ///
  /// This doesn't modify the hasher itself, and it's possible to finalize again
  /// after adding more input.
  ///
  /// \returns The finalized hash of the requested length.
  BLAKE3Result<NumBytes> final() { return BLAKE3::final<NumBytes>(); }

  /// Return the current hash of the digested data since the last init().
  ///
  /// Other hash functions distinguish between \p result() and \p final(), with
  /// \p result() allowing more calls into \p update(), but there's no
  /// difference for the BLAKE3 hash function.
  ///
  /// \returns The current hash of the digested data.
  BLAKE3Result<NumBytes> result() { return BLAKE3::result<NumBytes>(); }
};

} // namespace llvm

#endif
