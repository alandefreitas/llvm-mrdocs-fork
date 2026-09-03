//===- GsymReaderV1.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_GSYMREADERV1_H
#define LLVM_DEBUGINFO_GSYM_GSYMREADERV1_H

#include "llvm/DebugInfo/GSYM/GsymReader.h"
#include "llvm/DebugInfo/GSYM/Header.h"

namespace llvm {
class MemoryBuffer;

namespace gsym {

/// GsymReaderV1 reads GSYM V1 data from a buffer.
class LLVM_ABI GsymReaderV1 : public GsymReader {
  friend class GsymReader;
  const Header *Hdr = nullptr;
  std::unique_ptr<Header> SwappedHdr;

protected:
  /// Construct a GSYM V1 reader that owns \p Buffer with the given endianness.
  ///
  /// \param Buffer The memory buffer containing GSYM V1 data.
  /// \param Endian The byte order of the GSYM data in \p Buffer.
  GsymReaderV1(std::unique_ptr<MemoryBuffer> Buffer, llvm::endianness Endian);
  /// Parse the V1 header and populate GlobalDataSections.
  ///
  /// \returns Error on failure.
  llvm::Error parseHeaderAndGlobalDataEntries() override;

public:
  /// Move-construct a GSYM V1 reader from \p RHS.
  ///
  /// \param RHS The reader to move from.
  GsymReaderV1(GsymReaderV1 &&RHS) = default;
  /// Destroy this GSYM V1 reader.
  ~GsymReaderV1() override = default;

  // Header accessors
  /// Get the GSYM version for this reader.
  ///
  /// \returns The GSYM format version number.
  uint16_t getVersion() const override { return Header::getVersion(); }
  /// Get the base address of this GSYM file.
  ///
  /// \returns The base address used to resolve address offsets.
  uint64_t getBaseAddress() const override { return Hdr->BaseAddress; }
  /// Get the number of addresses in this GSYM file.
  ///
  /// \returns The number of addresses in the address table.
  uint64_t getNumAddresses() const override { return Hdr->NumAddresses; }
  /// Get the address offset byte size for this GSYM file.
  ///
  /// \returns The size in bytes of each address offset entry.
  uint8_t getAddressOffsetSize() const override { return Hdr->AddrOffSize; }
  /// Get the address info offset byte size for this GSYM file.
  ///
  /// \returns The size in bytes of each address info offset entry.
  uint8_t getAddressInfoOffsetSize() const override {
    return Header::getAddressInfoOffsetSize();
  }
  /// Get the string offset byte size for this GSYM file.
  ///
  /// \returns The size in bytes of each string table offset entry.
  uint8_t getStringOffsetSize() const override {
    return Header::getStringOffsetSize();
  }
  /// Get the raw UUID bytes for this GSYM file from the V1 header.
  ///
  /// \returns A StringRef spanning the UUID bytes in the header.
  StringRef getUUID() const override {
    return StringRef(reinterpret_cast<const char *>(Hdr->UUID), Hdr->UUIDSize);
  }

  /// Bring base-class dump overloads into scope.
  using GsymReader::dump;
  /// Dump the entire GSYM data contained in this object.
  ///
  /// \param OS The output stream to dump to.
  void dump(raw_ostream &OS) override;
};

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_GSYMREADERV1_H
