//===- ModuleDebugStream.h - PDB Module Info Stream Access ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_MODULEDEBUGSTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_MODULEDEBUGSTREAM_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptor.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>

namespace llvm {
class BinaryStreamReader;
namespace codeview {
class DebugChecksumsSubsectionRef;
}
namespace msf {
class MappedBlockStream;
}
namespace pdb {

/// Provides read access to one module's debug info stream in a PDB.
class ModuleDebugStreamRef {
  using DebugSubsectionIterator = codeview::DebugSubsectionArray::Iterator;

public:
  /// Construct a module debug stream reader for \p Module over \p Stream.
  ///
  /// \param Module DBI module descriptor describing stream layout sizes.
  /// \param Stream Owning mapped MSF stream for this module's debug info.
  LLVM_ABI ModuleDebugStreamRef(const DbiModuleDescriptor &Module,
                                std::unique_ptr<msf::MappedBlockStream> Stream);
  /// Move-construct a module debug stream reader from \p Other.
  ///
  /// \param Other The module debug stream reader to move from.
  ModuleDebugStreamRef(ModuleDebugStreamRef &&Other) = default;
  /// Copy-construct a module debug stream reader from \p Other.
  ///
  /// \param Other The module debug stream reader to copy.
  ModuleDebugStreamRef(const ModuleDebugStreamRef &Other) = default;
  /// Destroy the module debug stream reader.
  LLVM_ABI ~ModuleDebugStreamRef();

  /// Reload and reparse the module debug stream from the underlying MSF stream.
  ///
  /// \returns An Error on failure, or success if the stream was reloaded.
  LLVM_ABI Error reload();

  /// Return the signature field at the start of the symbols substream.
  ///
  /// \returns The signature value from the symbols substream header.
  uint32_t signature() const { return Signature; }

  /// Iterate the CodeView symbol records in this module's symbols substream.
  ///
  /// \param HadError Optional out-parameter set to true if iteration encounters
  ///     a corrupt or truncated symbol record.
  ///
  /// \returns An iterator range over CVSymbol records.
  LLVM_ABI iterator_range<codeview::CVSymbolArray::Iterator>
  symbols(bool *HadError) const;

  /// Return the parsed CodeView symbol array for this module.
  ///
  /// \returns A reference to the CVSymbolArray for this module.
  const codeview::CVSymbolArray &getSymbolArray() const { return SymbolArray; }
  /// Return the subset of the symbol array covered by the scope at
  /// \p ScopeBegin.
  ///
  /// \param ScopeBegin Byte offset of the opening scope symbol in the symbols
  ///     substream.
  ///
  /// \returns A CVSymbolArray limited to symbols inside that scope.
  LLVM_ABI const codeview::CVSymbolArray
  getSymbolArrayForScope(uint32_t ScopeBegin) const;

  /// Return a reference to the raw symbols substream.
  ///
  /// \returns A BinarySubstreamRef covering the symbols substream.
  LLVM_ABI BinarySubstreamRef getSymbolsSubstream() const;
  /// Return a reference to the raw C11 line-number substream.
  ///
  /// \returns A BinarySubstreamRef covering the C11 lines substream.
  LLVM_ABI BinarySubstreamRef getC11LinesSubstream() const;
  /// Return a reference to the raw C13 line-number / debug-subsection
  /// substream.
  ///
  /// \returns A BinarySubstreamRef covering the C13 lines substream.
  LLVM_ABI BinarySubstreamRef getC13LinesSubstream() const;
  /// Return a reference to the raw global-refs substream.
  ///
  /// \returns A BinarySubstreamRef covering the global-refs substream.
  LLVM_ABI BinarySubstreamRef getGlobalRefsSubstream() const;

  /// Move-assignment is deleted; module debug streams are not reassignable.
  ///
  /// \param Other Unused; this operator is deleted.
  ModuleDebugStreamRef &operator=(ModuleDebugStreamRef &&Other) = delete;

  /// Read the CodeView symbol record at \p Offset in the symbols substream.
  ///
  /// \param Offset Byte offset of the symbol record within the symbols
  ///     substream.
  ///
  /// \returns The CVSymbol at that offset.
  LLVM_ABI codeview::CVSymbol readSymbolAtOffset(uint32_t Offset) const;

  /// Iterate the debug subsections parsed from the C13 lines substream.
  ///
  /// \returns An iterator range over DebugSubsectionRecord entries.
  LLVM_ABI iterator_range<DebugSubsectionIterator> subsections() const;
  /// Return the parsed debug subsection array from the C13 lines substream.
  ///
  /// \returns The DebugSubsectionArray parsed from the C13 lines substream.
  codeview::DebugSubsectionArray getSubsectionsArray() const {
    return Subsections;
  }

  /// Return true if this module has a non-empty C13 debug-subsection
  /// substream.
  ///
  /// \returns True if the C13 debug-subsection substream is non-empty.
  LLVM_ABI bool hasDebugSubsections() const;

  /// Commit pending writes to the underlying stream.
  ///
  /// \returns An Error on failure, or success if there was nothing to commit.
  LLVM_ABI Error commit();

  /// Find the file-checksums debug subsection, if present.
  ///
  /// \returns A DebugChecksumsSubsectionRef on success (possibly empty if no
  ///     checksums subsection exists), or an Error if a checksums subsection
  ///     failed to initialize.
  LLVM_ABI Expected<codeview::DebugChecksumsSubsectionRef>
  findChecksumsSubsection() const;

private:
  Error reloadSerialize(BinaryStreamReader &Reader);

  DbiModuleDescriptor Mod;

  uint32_t Signature;

  std::shared_ptr<msf::MappedBlockStream> Stream;

  codeview::CVSymbolArray SymbolArray;

  BinarySubstreamRef SymbolsSubstream;
  BinarySubstreamRef C11LinesSubstream;
  BinarySubstreamRef C13LinesSubstream;
  BinarySubstreamRef GlobalRefsSubstream;

  codeview::DebugSubsectionArray Subsections;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_MODULEDEBUGSTREAM_H
