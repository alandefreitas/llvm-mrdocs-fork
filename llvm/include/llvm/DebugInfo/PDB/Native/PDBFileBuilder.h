//===- PDBFileBuilder.h - PDB File Creation ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_PDBFILEBUILDER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_PDBFILEBUILDER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/PDB/Native/HashTable.h"
#include "llvm/DebugInfo/PDB/Native/NamedStreamMap.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTableBuilder.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace llvm {
class WritableBinaryStream;
namespace codeview {
struct GUID;
}

namespace msf {
class MSFBuilder;
struct MSFLayout;
}
namespace pdb {
struct SrcHeaderBlockEntry;
class DbiStreamBuilder;
class InfoStreamBuilder;
class GSIStreamBuilder;
class TpiStreamBuilder;

/// Builds a complete PDB file by coordinating MSF layout and stream builders.
class PDBFileBuilder {
public:
  /// Construct a PDB file builder that allocates with \p Allocator.
  ///
  /// \param Allocator The bump allocator used for builder-owned data.
  LLVM_ABI explicit PDBFileBuilder(BumpPtrAllocator &Allocator);
  /// Destroy the PDB file builder and its owned stream builders.
  LLVM_ABI ~PDBFileBuilder();
  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is not supported.
  PDBFileBuilder(const PDBFileBuilder &Other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param Other Unused; copy assignment is not supported.
  PDBFileBuilder &operator=(const PDBFileBuilder &Other) = delete;

  /// Initialize the underlying MSF builder with the given block size.
  ///
  /// \param BlockSize The MSF block size in bytes used for the PDB file.
  ///
  /// \returns Success, or an error if MSF initialization fails.
  LLVM_ABI Error initialize(uint32_t BlockSize);

  /// Return the MSF builder that owns the PDB stream layout.
  ///
  /// \returns The MSF builder that owns the PDB stream layout.
  LLVM_ABI msf::MSFBuilder &getMsfBuilder();
  /// Return the builder for the PDB info stream.
  ///
  /// \returns The builder for the PDB info stream.
  LLVM_ABI InfoStreamBuilder &getInfoBuilder();
  /// Return the builder for the PDB DBI stream.
  ///
  /// \returns The builder for the PDB DBI stream.
  LLVM_ABI DbiStreamBuilder &getDbiBuilder();
  /// Return the builder for the PDB TPI stream.
  ///
  /// \returns The builder for the PDB TPI stream.
  LLVM_ABI TpiStreamBuilder &getTpiBuilder();
  /// Return the builder for the PDB IPI stream.
  ///
  /// \returns The builder for the PDB IPI stream.
  LLVM_ABI TpiStreamBuilder &getIpiBuilder();
  /// Return the builder for the PDB string table (`/names`) stream.
  ///
  /// \returns The builder for the PDB string table (`/names`) stream.
  LLVM_ABI PDBStringTableBuilder &getStringTableBuilder();
  /// Return the builder for the GSI (globals/publics) streams.
  ///
  /// \returns The builder for the GSI (globals/publics) streams.
  LLVM_ABI GSIStreamBuilder &getGsiBuilder();
  /// Return the optional DXContainer payload buffer, creating it if needed.
  ///
  /// \returns A reference to the unique pointer holding the DXContainer
  ///     payload buffer.
  LLVM_ABI std::unique_ptr<SmallVector<char>> &getDXContainerData();

  /// Commit the constructed PDB to \p Filename.
  ///
  /// If HashPDBContentsToGUID is true on the InfoStreamBuilder, Guid is filled
  /// with the computed PDB GUID on return.
  ///
  /// \param Filename The output path of the PDB file to write.
  /// \param Guid Optional pointer filled with the final PDB GUID when hashing
  ///     is enabled.
  ///
  /// \returns Success, or an error if layout finalization or writing fails.
  LLVM_ABI Error commit(StringRef Filename, codeview::GUID *Guid);

  /// Look up the MSF stream index of a named stream.
  ///
  /// \param Name The named stream to look up.
  ///
  /// \returns The stream index on success, or an error if \p Name is unknown.
  LLVM_ABI Expected<uint32_t> getNamedStreamIndex(StringRef Name) const;
  /// Add or replace a named stream with the given contents.
  ///
  /// \param Name The named stream to create or update.
  /// \param Data The raw bytes written into the named stream.
  ///
  /// \returns Success, or an error if the stream cannot be allocated.
  LLVM_ABI Error addNamedStream(StringRef Name, StringRef Data);
  /// Register an injected source file to be embedded in the PDB.
  ///
  /// \param Name The source file name as specified by the user.
  /// \param Buffer The file contents to store as an injected source stream.
  LLVM_ABI void addInjectedSource(StringRef Name,
                                  std::unique_ptr<MemoryBuffer> Buffer);

private:
  struct InjectedSourceDescriptor {
    // The full name of the stream that contains the contents of this injected
    // source.  This is built as a concatenation of the literal "/src/files"
    // plus the "vname".
    std::string StreamName;

    // The exact name of the file name as specified by the user.
    uint32_t NameIndex;

    // The string table index of the "vname" of the file.  As far as we
    // understand, this is the same as the name, except it is lowercased and
    // forward slashes are converted to backslashes.
    uint32_t VNameIndex;
    std::unique_ptr<MemoryBuffer> Content;
  };

  Error finalizeMsfLayout();
  Expected<uint32_t> allocateNamedStream(StringRef Name, uint32_t Size);

  void commitInjectedSources(WritableBinaryStream &MsfBuffer,
                             const msf::MSFLayout &Layout);
  void commitSrcHeaderBlock(WritableBinaryStream &MsfBuffer,
                            const msf::MSFLayout &Layout);

  BumpPtrAllocator &Allocator;

  std::unique_ptr<msf::MSFBuilder> Msf;
  std::unique_ptr<InfoStreamBuilder> Info;
  std::unique_ptr<DbiStreamBuilder> Dbi;
  std::unique_ptr<GSIStreamBuilder> Gsi;
  std::unique_ptr<TpiStreamBuilder> Tpi;
  std::unique_ptr<TpiStreamBuilder> Ipi;
  std::unique_ptr<SmallVector<char>> Dxc;

  std::unique_ptr<PDBStringTableBuilder> Strings;
  StringTableHashTraits InjectedSourceHashTraits;
  HashTable<SrcHeaderBlockEntry> InjectedSourceTable;

  SmallVector<InjectedSourceDescriptor, 2> InjectedSources;

  NamedStreamMap NamedStreams;
  DenseMap<uint32_t, std::string> NamedStreamData;
};
}
}

#endif
