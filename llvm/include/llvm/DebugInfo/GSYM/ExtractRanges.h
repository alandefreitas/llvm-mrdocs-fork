//===- ExtractRanges.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_EXTRACTRANGES_H
#define LLVM_DEBUGINFO_GSYM_EXTRACTRANGES_H

#include "llvm/ADT/AddressRanges.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <stdint.h>

#define HEX8(v) llvm::format_hex(v, 4)
#define HEX16(v) llvm::format_hex(v, 6)
#define HEX32(v) llvm::format_hex(v, 10)
#define HEX64(v) llvm::format_hex(v, 18)

namespace llvm {
class raw_ostream;

namespace gsym {

class FileWriter;
class GsymDataExtractor;

/// Decode an AddressRange from a binary data stream.
///
/// AddressRange objects are encoded and decoded to be relative to a base
/// address. This will be the FunctionInfo's start address if the AddressRange
/// is directly contained in a FunctionInfo, or a base address of the
/// containing parent AddressRange or AddressRanges. This allows address
/// ranges to be efficiently encoded using ULEB128 encodings as we encode the
/// offset and size of each range instead of full addresses. This also makes
/// encoded addresses easy to relocate as we just need to relocate one base
/// address.
///
/// \param Data The binary stream to read the data from.
///
/// \param BaseAddr The base address used to reconstruct absolute addresses.
///
/// \param Offset The byte offset within \a Data.
///
/// \returns The decoded address range.
LLVM_ABI AddressRange decodeRange(GsymDataExtractor &Data, uint64_t BaseAddr,
                                  uint64_t &Offset);

/// Encode an AddressRange into a binary stream relative to a base address.
///
/// The range is written as a ULEB128 offset from \a BaseAddr followed by a
/// ULEB128 size, matching the format consumed by decodeRange.
///
/// \param Range The address range to encode.
///
/// \param O The binary stream to write the data to at the current file
/// position.
///
/// \param BaseAddr The base address to subtract when encoding the range.
LLVM_ABI void encodeRange(const AddressRange &Range, FileWriter &O,
                          uint64_t BaseAddr);

/// Skip an address range object in the specified data a the specified
/// offset.
///
/// \param Data The binary stream to read the data from.
///
/// \param Offset The byte offset within \a Data.
LLVM_ABI void skipRange(GsymDataExtractor &Data, uint64_t &Offset);

/// Decode a collection of address ranges from a binary data stream.
///
/// Address ranges are decoded relative to a base address. See the
/// documentation for decodeRange for full details on the encoding.
///
/// \param Ranges The collection to populate with decoded address ranges.
///
/// \param Data The binary stream to read the data from.
///
/// \param BaseAddr The base address used to reconstruct absolute addresses.
///
/// \param Offset The byte offset within \a Data.
LLVM_ABI void decodeRanges(AddressRanges &Ranges, GsymDataExtractor &Data,
                           uint64_t BaseAddr, uint64_t &Offset);

/// Encode a collection of address ranges into a binary stream.
///
/// Address ranges are encoded relative to a base address. See the
/// documentation for encodeRange for full details on the encoding.
///
/// \param Ranges The address ranges to encode.
///
/// \param O The binary stream to write the data to at the current file
/// position.
///
/// \param BaseAddr The base address to subtract when encoding each range.
LLVM_ABI void encodeRanges(const AddressRanges &Ranges, FileWriter &O,
                           uint64_t BaseAddr);

/// Skip an address range object in the specified data a the specified
/// offset.
///
/// \param Data The binary stream to read the data from.
///
/// \param Offset The byte offset within \a Data.
///
/// \returns The number of address ranges that were skipped.
LLVM_ABI uint64_t skipRanges(GsymDataExtractor &Data, uint64_t &Offset);

} // namespace gsym

/// Stream a human-readable representation of \p R to \p OS.
///
/// \param OS Destination stream.
///
/// \param R Address range to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const AddressRange &R);

/// Stream a human-readable representation of \p AR to \p OS.
///
/// \param OS Destination stream.
///
/// \param AR Address ranges to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const AddressRanges &AR);

} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_EXTRACTRANGES_H
