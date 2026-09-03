//===- InjectedSourceStream.h - PDB Headerblock Stream Access ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_INJECTEDSOURCESTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_INJECTEDSOURCESTREAM_H

#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/HashTable.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {
struct SrcHeaderBlockEntry;
struct SrcHeaderBlockHeader;
class PDBStringTable;

/// Provides read access to the PDB /src/headerblock stream (injected sources).
class InjectedSourceStream {
public:
  /// Construct an injected source stream reader over \p Stream.
  ///
  /// \param Stream Owning mapped MSF stream for the /src/headerblock stream.
  LLVM_ABI InjectedSourceStream(std::unique_ptr<msf::MappedBlockStream> Stream);
  /// Reload and reparse the injected source stream from the underlying MSF
  /// stream.
  ///
  /// \param Strings PDB string table used to validate name references in each
  ///     entry.
  ///
  /// \returns An Error on failure, or success if the stream was reloaded.
  LLVM_ABI Error reload(const PDBStringTable &Strings);

  /// Const iterator over hash-table entries of \c SrcHeaderBlockEntry.
  using const_iterator = HashTable<SrcHeaderBlockEntry>::const_iterator;
  /// Return an iterator to the first injected source entry.
  ///
  /// \returns A const iterator to the first injected source entry.
  const_iterator begin() const { return InjectedSourceTable.begin(); }
  /// Return an iterator past the last injected source entry.
  ///
  /// \returns A const iterator past the last injected source entry.
  const_iterator end() const { return InjectedSourceTable.end(); }

  /// Return the number of injected source entries in the stream.
  ///
  /// \returns The number of injected source entries.
  uint32_t size() const { return InjectedSourceTable.size(); }

private:
  std::unique_ptr<msf::MappedBlockStream> Stream;

  const SrcHeaderBlockHeader* Header;
  HashTable<SrcHeaderBlockEntry> InjectedSourceTable;
};
}
} // namespace llvm

#endif
