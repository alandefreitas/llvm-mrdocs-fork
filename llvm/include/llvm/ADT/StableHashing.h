//===- llvm/ADT/StableHashing.h - Utilities for stable hashing * C++ *-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides types and functions for computing and combining stable
// hashes. Stable hashes can be useful for hashing across different modules,
// processes, machines, or compiler runs for a specific compiler version. It
// currently employs the xxh3_64bits hashing algorithm. Be aware that this
// implementation may be adjusted or updated as improvements to the compiler are
// made.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STABLEHASHING_H
#define LLVM_ADT_STABLEHASHING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/xxhash.h"

namespace llvm {

/// An opaque object representing a stable hash code. It can be serialized,
/// deserialized, and is stable across processes and executions.
using stable_hash = uint64_t;

/// Combine the hashes in \p Buffer into a single stable hash.
/// @param Buffer Sequence of stable hashes to mix together.
inline stable_hash stable_hash_combine(ArrayRef<stable_hash> Buffer) {
  return xxh3_64bits(reinterpret_cast<const uint8_t *>(Buffer.data()),
                     Buffer.size() * sizeof(stable_hash));
}

/// Combine two stable hashes into one.
/// @param A First hash.
/// @param B Second hash.
inline stable_hash stable_hash_combine(stable_hash A, stable_hash B) {
  stable_hash Hashes[2] = {
      support::endian::byte_swap(A, llvm::endianness::little),
      support::endian::byte_swap(B, llvm::endianness::little),
  };
  return stable_hash_combine(Hashes);
}

/// Combine three stable hashes into one.
/// @param A First hash.
/// @param B Second hash.
/// @param C Third hash.
inline stable_hash stable_hash_combine(stable_hash A, stable_hash B,
                                       stable_hash C) {
  stable_hash Hashes[3] = {
      support::endian::byte_swap(A, llvm::endianness::little),
      support::endian::byte_swap(B, llvm::endianness::little),
      support::endian::byte_swap(C, llvm::endianness::little),
  };
  return stable_hash_combine(Hashes);
}

/// Combine four stable hashes into one.
/// @param A First hash.
/// @param B Second hash.
/// @param C Third hash.
/// @param D Fourth hash.
inline stable_hash stable_hash_combine(stable_hash A, stable_hash B,
                                       stable_hash C, stable_hash D) {
  stable_hash Hashes[4] = {
      support::endian::byte_swap(A, llvm::endianness::little),
      support::endian::byte_swap(B, llvm::endianness::little),
      support::endian::byte_swap(C, llvm::endianness::little),
      support::endian::byte_swap(D, llvm::endianness::little),
  };
  return stable_hash_combine(Hashes);
}

/// Strip LLVM-introduced suffixes so the name is stable across builds.
///
/// Removes suffixes such as \c .llvm.* and \c .__uniq.*, and prefers the
/// portion after \c .content. when present.
/// @param Name Possibly decorated symbol or object name.
inline StringRef get_stable_name(StringRef Name) {
  // Return the part after ".content." that represents contents.
  StringRef S0 = Name.rsplit(".content.").second;
  if (!S0.empty())
    return S0;

  // Ignore these suffixes.
  StringRef P1 = Name.rsplit(".llvm.").first;
  return P1.rsplit(".__uniq.").first;
}

/// Hash \p Name after normalizing it with get_stable_name().
///
/// Names that differ only in LLVM uniqueness suffixes produce the same hash.
/// @param Name Possibly decorated symbol or object name.
inline stable_hash stable_hash_name(StringRef Name) {
  return xxh3_64bits(get_stable_name(Name));
}

} // namespace llvm

#endif
