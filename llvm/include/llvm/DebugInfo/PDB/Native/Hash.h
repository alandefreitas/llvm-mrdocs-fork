//===- Hash.h - PDB hash functions ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_HASH_H
#define LLVM_DEBUGINFO_PDB_NATIVE_HASH_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {
namespace pdb {

/// Compute the PDB V1 string hash used by name tables and TPI/IPI.
///
/// Corresponds to \c Hasher::lhashPbCb in PDB/include/misc.h.
///
/// \param Str The string bytes to hash.
///
/// \returns The 32-bit V1 hash of \p Str.
LLVM_ABI uint32_t hashStringV1(StringRef Str);
/// Compute the PDB V2 string hash used by the name hash table.
///
/// Corresponds to \c HasherV2::HashULONG in PDB/include/misc.h.
///
/// \param Str The string bytes to hash.
///
/// \returns The 32-bit V2 hash of \p Str.
LLVM_ABI uint32_t hashStringV2(StringRef Str);
/// Compute the PDB V8 buffer hash (CRC-32) of raw bytes.
///
/// Corresponds to \c SigForPbCb in langapi/shared/crc32.h.
///
/// \param Data The byte buffer to hash.
///
/// \returns The 32-bit CRC-32 hash of \p Data.
LLVM_ABI uint32_t hashBufferV8(ArrayRef<uint8_t> Data);

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_HASH_H
