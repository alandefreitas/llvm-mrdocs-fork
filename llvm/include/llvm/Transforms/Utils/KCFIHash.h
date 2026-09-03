//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helpers for computing the 32-bit KCFI type ID from a mangled type name.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_KCFIHASH_H
#define LLVM_TRANSFORMS_UTILS_KCFIHASH_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm {

/// Hash algorithm used to compute a KCFI type ID.
enum class KCFIHashAlgorithm {
  /// 64-bit xxHash truncated to a 32-bit type ID.
  xxHash64,
  /// FNV-1a hash used as a 32-bit type ID.
  FNV1a,
};

/// Parse a KCFI hash algorithm name.
/// Returns xxHash64 if the name is not recognized.
/// \param Name Algorithm name string, such as "xxHash64" or "FNV1a".
/// \return The parsed hash algorithm, or xxHash64 if \p Name is unrecognized.
LLVM_ABI KCFIHashAlgorithm parseKCFIHashAlgorithm(StringRef Name);

/// Convert a KCFI hash algorithm enum to its string representation.
/// \param Algorithm Hash algorithm to stringify.
/// \return The string name of \p Algorithm.
LLVM_ABI StringRef stringifyKCFIHashAlgorithm(KCFIHashAlgorithm Algorithm);

/// Compute KCFI type ID from mangled type name.
/// The algorithm can be xxHash64 or FNV-1a.
/// \param MangledTypeName Mangled function type name to hash.
/// \param Algorithm Hash algorithm used to compute the type ID.
/// \return The 32-bit KCFI type ID for \p MangledTypeName.
LLVM_ABI uint32_t getKCFITypeID(StringRef MangledTypeName,
                                KCFIHashAlgorithm Algorithm);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_KCFIHASH_H
