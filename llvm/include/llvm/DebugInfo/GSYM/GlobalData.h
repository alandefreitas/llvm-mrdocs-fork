//===- GlobalData.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_GLOBALDATA_H
#define LLVM_DEBUGINFO_GSYM_GLOBALDATA_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

namespace gsym {
class FileWriter;
class GsymDataExtractor;

/// Extensible tags for global data sections in a GSYM file.
///
/// GlobalInfoType allows GSYM files to encode global information within a GSYM
/// file in a way that is extensible for future versions of GSYM. It is
/// designed to contain information needed by the GSYM encoding, along with any
/// common data that FunctionInfo InfoType entries might require.
enum class GlobalInfoType : uint32_t {
  /// Sentinel that terminates a list of GlobalData entries.
  EndOfList = 0u,
  /// The address offsets table of function addresses relative to the base.
  ///
  /// It's a list of function addresses subtracted by the base address, hence
  /// "offset".
  ///
  /// This table and the address info offsets table (see below) have the same
  /// number of items. The items are 1-1 mapped.
  ///
  /// Given an address, this table is used to do a binary search to find the
  /// index into the address info offsets table, where the location of the
  /// FunctionInfo for the same function can be found in the GSYM.
  AddrOffsets = 1u,
  /// Offsets to FunctionInfo entries for each address in AddrOffsets.
  ///
  /// Each entry is an offset relative to a version-dependent reference
  /// position in the GSYM data where the FunctionInfo for the corresponding
  /// function can be found.
  ///
  /// In version 1, the reference position is the start of the GSYM data.
  /// In version 2 and later, the reference position is the start of the
  /// FunctionInfo section.
  AddrInfoOffsets = 2u,
  /// The string table containing all strings referenced by the GSYM.
  ///
  /// The exact storage of the strings is determined by
  /// HeaderV2::StrTableEncoding.
  StringTable = 3u,
  /// The file table listing source files referred to by FunctionInfo objects.
  FileTable = 4u,
  /// A list of FunctionInfo objects, terminated by EndOfList.
  FunctionInfo = 5u,
  /// Optional UUID identifying the binary this GSYM describes.
  UUID = 6u,
};

/// Describes a typed data section within a GSYM file.
///
/// GlobalData describes a section of data in a GSYM file by its type, file
/// offset, and size. This is used to support 64-bit GSYM files where data
/// sections can be located at arbitrary file offsets.
struct GlobalData {
  /// The kind of data stored in this section.
  GlobalInfoType Type;
  /// Byte offset of this section from the start of the GSYM file.
  uint64_t FileOffset;
  /// Size in bytes of this section.
  uint64_t FileSize;

  /// Encode this GlobalData entry into a FileWriter stream.
  ///
  /// \param O The binary stream to write the data to.
  LLVM_ABI void encode(FileWriter &O) const;

  /// Decode a GlobalData entry from a binary data stream.
  ///
  /// \param GsymData The binary stream to read from.
  /// \param Offset The offset to start reading from. Updated on success.
  /// \returns A GlobalData entry or an error.
  LLVM_ABI static llvm::Expected<GlobalData> decode(GsymDataExtractor &GsymData,
                                                    uint64_t &Offset);
};

/// Return a human-readable name for a GlobalInfoType value.
///
/// \param Type The global info type to name.
/// \returns A string describing \p Type.
LLVM_ABI StringRef getNameForGlobalInfoType(GlobalInfoType Type);

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_GLOBALDATA_H
