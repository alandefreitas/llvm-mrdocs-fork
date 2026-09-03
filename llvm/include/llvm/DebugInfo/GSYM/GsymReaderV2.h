//===- GsymReaderV2.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_GSYMREADERV2_H
#define LLVM_DEBUGINFO_GSYM_GSYMREADERV2_H

#include "llvm/DebugInfo/GSYM/GsymReader.h"
#include "llvm/DebugInfo/GSYM/HeaderV2.h"

namespace llvm {
class MemoryBuffer;

namespace gsym {

/// GsymReaderV2 reads GSYM V2 data from a buffer.
class LLVM_ABI GsymReaderV2 : public GsymReader {
  friend class GsymReader;
  const HeaderV2 *Hdr = nullptr;
  std::unique_ptr<HeaderV2> SwappedHdr;

protected:
  /// Construct a GsymReaderV2 that owns \a Buffer with the given endianness.
  ///
  /// \param Buffer Memory buffer holding the GSYM file bytes; ownership is
  /// transferred to this object.
  /// \param Endian Byte order of the GSYM data in \a Buffer.
  GsymReaderV2(std::unique_ptr<MemoryBuffer> Buffer, llvm::endianness Endian);
  /// Parse the GSYM V2 header and populate GlobalDataSections.
  ///
  /// \returns Error on failure.
  llvm::Error parseHeaderAndGlobalDataEntries() override;

public:
  /// Move-construct a GsymReaderV2, transferring ownership of internal state.
  ///
  /// \param RHS The reader to move from.
  GsymReaderV2(GsymReaderV2 &&RHS) = default;
  /// Destroy this GsymReaderV2 and release owned resources.
  ~GsymReaderV2() override = default;

  // Header accessors
  /// Get the GSYM version for this reader.
  ///
  /// \returns The GSYM format version number.
  uint16_t getVersion() const override { return HeaderV2::getVersion(); }
  /// Get the base address of this GSYM file.
  ///
  /// \returns The base address used for address offsets in this file.
  uint64_t getBaseAddress() const override { return Hdr->BaseAddress; }
  /// Get the number of addresses in this GSYM file.
  ///
  /// \returns The number of addresses in the address table.
  uint64_t getNumAddresses() const override { return Hdr->NumAddresses; }
  /// Get the address offset byte size for this GSYM file.
  ///
  /// \returns The size in bytes of each address offset.
  uint8_t getAddressOffsetSize() const override { return Hdr->AddrOffSize; }
  /// Get the address info offset byte size for this GSYM file.
  ///
  /// \returns The size in bytes of each address info offset.
  uint8_t getAddressInfoOffsetSize() const override {
    return HeaderV2::getAddressInfoOffsetSize();
  }
  /// Get the string offset byte size for this GSYM file.
  ///
  /// \returns The size in bytes of each string table offset.
  uint8_t getStringOffsetSize() const override {
    return HeaderV2::getStringOffsetSize();
  }
  /// Get the raw UUID bytes for this GSYM file, or an empty ref if none.
  ///
  /// \returns The UUID bytes, or an empty StringRef if none are present.
  StringRef getUUID() const override {
    return getOptionalGlobalDataBytes(GlobalInfoType::UUID)
        .value_or(StringRef());
  }

  /// Bring base-class dump overloads into this scope.
  using GsymReader::dump;
  /// Dump the entire Gsym data contained in this object.
  ///
  /// \param OS The output stream to dump to.
  void dump(raw_ostream &OS) override;
};

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_GSYMREADERV2_H
