//===- SymbolStream.cpp - PDB Symbol Stream Access --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_SYMBOLSTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_SYMBOLSTREAM_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/Support/Compiler.h"

#include "llvm/Support/Error.h"

namespace llvm {
namespace msf {
class MappedBlockStream;
}
namespace pdb {

/// Provides read access to the PDB symbol stream of CodeView records.
class SymbolStream {
public:
  /// Construct a symbol stream reader over \p Stream.
  ///
  /// \param Stream Owning mapped MSF stream for the PDB symbol stream.
  LLVM_ABI SymbolStream(std::unique_ptr<msf::MappedBlockStream> Stream);
  /// Destroy the symbol stream reader.
  LLVM_ABI ~SymbolStream();
  /// Reload and reparse the symbol stream from the underlying MSF stream.
  ///
  /// \returns An Error on failure, or success if the stream was reloaded.
  LLVM_ABI Error reload();

  /// Return the parsed CodeView symbol array for this stream.
  ///
  /// \returns A const reference to the CVSymbolArray of parsed symbol records.
  const codeview::CVSymbolArray &getSymbolArray() const {
    return SymbolRecords;
  }

  /// Read the CodeView symbol record at \p Offset in the symbol stream.
  ///
  /// \param Offset Byte offset of the symbol record within the symbol stream.
  ///
  /// \returns The CVSymbol at that offset.
  LLVM_ABI codeview::CVSymbol readRecord(uint32_t Offset) const;

  /// Iterate the CodeView symbol records in this stream.
  ///
  /// \param HadError Optional out-parameter set to true if iteration encounters
  ///     a corrupt or truncated symbol record.
  ///
  /// \returns An iterator range over CVSymbol records.
  LLVM_ABI iterator_range<codeview::CVSymbolArray::Iterator>
  getSymbols(bool *HadError) const;

  /// Commit pending writes to the underlying stream.
  ///
  /// \returns An Error on failure, or success if there was nothing to commit.
  LLVM_ABI Error commit();

private:
  codeview::CVSymbolArray SymbolRecords;
  std::unique_ptr<msf::MappedBlockStream> Stream;
};
} // namespace pdb
}

#endif
