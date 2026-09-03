//===- GsymDataExtractor.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_GSYMDATAEXTRACTOR_H
#define LLVM_DEBUGINFO_GSYM_GSYMDATAEXTRACTOR_H

#include "llvm/Support/DataExtractor.h"

namespace llvm {
namespace gsym {

/// A DataExtractor subclass that adds GSYM-specific string offset support.
///
/// GSYM files use variable-width string offsets (1-8 bytes). This subclass
/// adds getStringOffsetSize() and getStringOffset() methods to support reading
/// string offsets of the configured size.
class GsymDataExtractor : public DataExtractor {
  uint8_t StringOffsetSize;

public:
  /// Construct from raw bytes.
  ///
  /// \param Data Buffer whose bytes will be extracted.
  /// \param IsLittleEndian Whether multi-byte values are little-endian.
  /// \param StringOffsetSize Size in bytes of string table offsets (1-8).
  GsymDataExtractor(StringRef Data, bool IsLittleEndian,
                    uint8_t StringOffsetSize = 8)
      : DataExtractor(Data, IsLittleEndian),
        StringOffsetSize(StringOffsetSize) {}

  /// Construct a sub-range extractor from a parent, copying its endianness
  /// and string offset size.
  ///
  /// \param Parent Extractor whose endianness and string offset size are
  ///               copied, and whose data is sliced.
  /// \param Offset Byte offset into \p Parent's data at which the sub-range
  ///               begins.
  /// \param Length Length in bytes of the sub-range.
  GsymDataExtractor(const GsymDataExtractor &Parent, uint64_t Offset,
                    uint64_t Length)
      : DataExtractor(Parent.getData().substr(Offset, Length),
                      Parent.isLittleEndian()),
        StringOffsetSize(Parent.getStringOffsetSize()) {}

  /// Get the string offset size in bytes.
  ///
  /// \returns The size in bytes of string table offsets (1-8).
  uint8_t getStringOffsetSize() const { return StringOffsetSize; }

  /// Extract a string offset of StringOffsetSize bytes from \a *offset_ptr.
  ///
  /// \param[in,out] offset_ptr A pointer to an offset within the data that will
  ///     be advanced by StringOffsetSize bytes on success.
  /// \returns The extracted string offset, or zero on failure.
  uint64_t getStringOffset(uint64_t *offset_ptr) const {
    return getUnsigned(offset_ptr, StringOffsetSize);
  }

  /// Extract a string offset of StringOffsetSize bytes from the location given
  /// by the cursor.
  ///
  /// \param[in,out] C Cursor providing the extraction offset and sticky error
  ///     state.
  /// \returns The extracted string offset, or zero on failure.
  uint64_t getStringOffset(Cursor &C) const {
    return getUnsigned(C, StringOffsetSize);
  }
};

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_GSYMDATAEXTRACTOR_H
