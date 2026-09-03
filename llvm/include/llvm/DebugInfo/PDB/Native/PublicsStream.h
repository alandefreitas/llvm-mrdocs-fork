//===- PublicsStream.h - PDB Public Symbol Stream -------- ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_PUBLICSSTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_PUBLICSSTREAM_H

#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace msf {
class MappedBlockStream;
}
namespace codeview {
class PublicSym32;
}
namespace pdb {
struct PublicsStreamHeader;
struct SectionOffset;
class SymbolStream;

/// Provides read access to the PDB publics stream and its address maps.
class PublicsStream {
public:
  /// Construct a publics stream reader over \p Stream.
  ///
  /// \param Stream Owning mapped MSF stream for the publics stream.
  LLVM_ABI PublicsStream(std::unique_ptr<msf::MappedBlockStream> Stream);
  /// Destroy the publics stream reader.
  LLVM_ABI ~PublicsStream();
  /// Reload and reparse the publics stream from the underlying MSF stream.
  ///
  /// \returns An Error on failure, or success if the stream was reloaded.
  LLVM_ABI Error reload();

  /// Return the symbol-hash size field from the publics stream header.
  ///
  /// \returns The SymHash value from the parsed publics stream header.
  LLVM_ABI uint32_t getSymHash() const;
  /// Return the section index of the thunk table from the header.
  ///
  /// \returns The section index of the thunk table from the header.
  LLVM_ABI uint16_t getThunkTableSection() const;
  /// Return the offset of the thunk table within its section from the header.
  ///
  /// \returns The byte offset of the thunk table within its section.
  LLVM_ABI uint32_t getThunkTableOffset() const;
  /// Return the parsed publics GSI hash table.
  ///
  /// \returns A const reference to the parsed publics GSIHashTable.
  const GSIHashTable &getPublicsTable() const { return PublicsTable; }
  /// Return the address map of symbol offsets sorted by section and offset.
  ///
  /// \returns A FixedStreamArray of symbol offsets sorted by address.
  FixedStreamArray<support::ulittle32_t> getAddressMap() const {
    return AddressMap;
  }
  /// Return the thunk map of thunk target offsets from the publics stream.
  ///
  /// \returns A FixedStreamArray of thunk target offsets.
  FixedStreamArray<support::ulittle32_t> getThunkMap() const {
    return ThunkMap;
  }
  /// Return the section-offset map entries from the publics stream.
  ///
  /// \returns A FixedStreamArray of SectionOffset map entries.
  FixedStreamArray<SectionOffset> getSectionOffsets() const {
    return SectionOffsets;
  }

  /// Find a public symbol by a segment and offset.
  ///
  /// In case there is more than one symbol (for example due to ICF), the first
  /// one is returned.
  ///
  /// \param Symbols Symbol stream used to read public symbol records.
  /// \param Segment Section (segment) index of the address to look up.
  /// \param Offset Offset within the section of the address to look up.
  ///
  /// \return If a symbol was found, the symbol at the provided address is
  ///     returned as well as the index of this symbol in the address map. If
  ///     the binary was linked with ICF, there might be more symbols with the
  ///     same address after the returned one. If no symbol is found,
  ///     `std::nullopt` is returned.
  LLVM_ABI std::optional<std::pair<codeview::PublicSym32, size_t>>
  findByAddress(const SymbolStream &Symbols, uint16_t Segment,
                uint32_t Offset) const;

private:
  std::unique_ptr<msf::MappedBlockStream> Stream;
  GSIHashTable PublicsTable;
  FixedStreamArray<support::ulittle32_t> AddressMap;
  FixedStreamArray<support::ulittle32_t> ThunkMap;
  FixedStreamArray<SectionOffset> SectionOffsets;

  const PublicsStreamHeader *Header;
};
}
}

#endif
