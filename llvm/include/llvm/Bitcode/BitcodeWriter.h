//===- llvm/Bitcode/BitcodeWriter.h - Bitcode writers -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines interfaces to write LLVM bitcode files/streams.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITCODEWRITER_H
#define LLVM_BITCODE_BITCODEWRITER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <memory>
#include <vector>

namespace llvm {

class BitstreamWriter;
class Module;
class raw_ostream;

/// Writer that serializes LLVM IR modules and ThinLTO indexes to bitcode.
class BitcodeWriter {
  std::unique_ptr<BitstreamWriter> Stream;

  StringTableBuilder StrtabBuilder{StringTableBuilder::RAW};

  // Owns any strings created by the irsymtab writer until we create the
  // string table.
  BumpPtrAllocator Alloc;

  bool WroteStrtab = false, WroteSymtab = false;

  void writeBlob(unsigned Block, unsigned Record, StringRef Blob);

  std::vector<Module *> Mods;

public:
  /// Create a BitcodeWriter that writes to Buffer.
  ///
  /// \param Buffer Destination that receives the encoded bitcode bytes.
  LLVM_ABI BitcodeWriter(SmallVectorImpl<char> &Buffer);
  /// Create a BitcodeWriter that writes to \p FS.
  ///
  /// \param FS Stream that receives the encoded bitcode bytes.
  LLVM_ABI BitcodeWriter(raw_ostream &FS);

  /// Destroy this bitcode writer.
  LLVM_ABI ~BitcodeWriter();

  /// Attempt to write a symbol table to the bitcode file. This must be called
  /// at most once after all modules have been written.
  ///
  /// A reader does not require a symbol table to interpret a bitcode file;
  /// the symbol table is needed only to improve link-time performance. So
  /// this function may decide not to write a symbol table. It may so decide
  /// if, for example, the target is unregistered or the IR is malformed.
  LLVM_ABI void writeSymtab();

  /// Write the bitcode file's string table. This must be called exactly once
  /// after all modules and the optional symbol table have been written.
  LLVM_ABI void writeStrtab();

  /// Copy the string table for another module into this bitcode file. This
  /// should be called after copying the module itself into the bitcode file.
  ///
  /// \param Strtab Serialized string-table blob to copy into this file.
  LLVM_ABI void copyStrtab(StringRef Strtab);

  /// Write the specified module to the buffer specified at construction time.
  ///
  /// \param M The module to serialize.
  /// \param ShouldPreserveUseListOrder If true, encode the use-list order for
  ///        each \a Value in \p M so it is reconstructed exactly when \p M is
  ///        deserialized.
  /// \param Index Optional summary index included in the bitcode (currently
  ///        for ThinLTO optimization).
  /// \param GenerateHash If true, hash the module and include the hash in the
  ///        bitcode (currently for ThinLTO incremental builds).
  /// \param ModHash If non-null, when \p GenerateHash is true the resulting
  ///        hash is written here; when \p GenerateHash is false, this value is
  ///        used as the hash instead of computing one from the generated
  ///        bitcode. Can be used to produce the same module hash for a
  ///        minimized thin-link bitcode as for the full bitcode used in the
  ///        backend.
  LLVM_ABI void writeModule(const Module &M,
                            bool ShouldPreserveUseListOrder = false,
                            const ModuleSummaryIndex *Index = nullptr,
                            bool GenerateHash = false,
                            ModuleHash *ModHash = nullptr);

  /// Write a minimized thin-link bitcode file to the construction-time buffer.
  ///
  /// The thin-link bitcode file is used for the ThinLTO thin link, and it
  /// contains only the information needed for that step.
  ///
  /// \param M The module whose minimized bitcode is written.
  /// \param Index Per-module summary index written into the thin-link file.
  /// \param ModHash Module hash for ThinLTO incremental builds, generated
  ///        while writing the IR bitcode file.
  LLVM_ABI void writeThinLinkBitcode(const Module &M,
                                     const ModuleSummaryIndex &Index,
                                     const ModuleHash &ModHash);

  /// Write a module summary index into this bitcode file.
  ///
  /// Used when writing the combined index file for ThinLTO. When writing a
  /// subset of the index for a distributed backend, provide
  /// \p ModuleToSummariesForIndex.
  ///
  /// \param Index Combined or per-module summary index to write.
  /// \param ModuleToSummariesForIndex Optional map from module name to the
  ///        summaries to include for a distributed ThinLTO backend.
  /// \param DecSummaries Optional set of summaries whose values should be
  ///        imported as declarations (prototypes).
  LLVM_ABI void
  writeIndex(const ModuleSummaryIndex *Index,
             const ModuleToSummariesForIndexTy *ModuleToSummariesForIndex,
             const GVSummaryPtrSet *DecSummaries);
};

/// Write the specified module to the specified raw output stream.
///
/// For streams where it matters, the given stream should be in "binary"
/// mode.
///
/// \param M The module to serialize.
/// \param Out Stream that receives the encoded bitcode.
/// \param ShouldPreserveUseListOrder If true, encode the use-list order for
///        each \a Value in \p M so it is reconstructed exactly when \p M is
///        deserialized.
/// \param Index Optional summary index included in the bitcode (currently
///        for ThinLTO optimization).
/// \param GenerateHash If true, hash the module and include the hash in the
///        bitcode (currently for ThinLTO incremental builds).
/// \param ModHash If non-null, when \p GenerateHash is true the resulting
///        hash is written here; when \p GenerateHash is false, this value is
///        used as the hash instead of computing one from the generated
///        bitcode. Can be used to produce the same module hash for a
///        minimized thin-link bitcode as for the full bitcode used in the
///        backend.
LLVM_ABI void WriteBitcodeToFile(const Module &M, raw_ostream &Out,
                                 bool ShouldPreserveUseListOrder = false,
                                 const ModuleSummaryIndex *Index = nullptr,
                                 bool GenerateHash = false,
                                 ModuleHash *ModHash = nullptr);

/// Write a minimized thin-link bitcode file to \p Out.
///
/// The output is written in a new bitcode block. The thin-link bitcode file
/// is used for the ThinLTO thin link, and it contains only the information
/// needed for that step.
///
/// \param M The module whose minimized bitcode is written.
/// \param Out Stream that receives the encoded thin-link bitcode.
/// \param Index Per-module summary index written into the thin-link file.
/// \param ModHash Module hash for ThinLTO incremental builds, generated
///        while writing the IR bitcode file.
LLVM_ABI void writeThinLinkBitcodeToFile(const Module &M, raw_ostream &Out,
                                         const ModuleSummaryIndex &Index,
                                         const ModuleHash &ModHash);

/// Write a module summary index to \p Out as a new bitcode block.
///
/// Used when writing the combined index file for ThinLTO. When writing a
/// subset of the index for a distributed backend, provide
/// \p ModuleToSummariesForIndex.
///
/// \param Index Combined or per-module summary index to write.
/// \param Out Stream that receives the encoded index bitcode.
/// \param ModuleToSummariesForIndex Optional map from module name to the
///        summaries to include for a distributed ThinLTO backend.
/// \param DecSummaries Optional set of summaries whose values should be
///        imported as declarations (prototypes).
LLVM_ABI void writeIndexToFile(
    const ModuleSummaryIndex &Index, raw_ostream &Out,
    const ModuleToSummariesForIndexTy *ModuleToSummariesForIndex = nullptr,
    const GVSummaryPtrSet *DecSummaries = nullptr);

/// Embed bitcode and optional command-line data as sections in \p M.
///
/// If \p EmbedBitcode is set, save a copy of the LLVM IR as data in the
/// __LLVM,__bitcode section (.llvmbc on non-MacOS). If available, pass the
/// serialized module via \p Buf. If not, pass an empty (default-initialized)
/// MemoryBufferRef, and the serialization will be handled by this API. The
/// same behavior happens if the provided \p Buf is not bitcode (i.e. if it's
/// invalid data or even textual LLVM assembly). If \p EmbedCmdline is set, the
/// command line is also exported in the corresponding section
/// (__LLVM,__cmdline / .llvmcmd) - even if \p CmdArgs were empty.
///
/// \param M Module that receives the embedded data globals.
/// \param Buf Serialized bitcode to embed, or an empty buffer to serialize
///        \p M.
/// \param EmbedBitcode If true, embed bitcode in the module's bitcode section.
/// \param EmbedCmdline If true, embed \p CmdArgs in the command-line section.
/// \param CmdArgs Compiler command-line bytes to embed when \p EmbedCmdline
///        is true.
LLVM_ABI void embedBitcodeInModule(Module &M, MemoryBufferRef Buf,
                                   bool EmbedBitcode, bool EmbedCmdline,
                                   const std::vector<uint8_t> &CmdArgs);

} // end namespace llvm

#endif // LLVM_BITCODE_BITCODEWRITER_H
