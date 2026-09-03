//===- DbiModuleDescriptorBuilder.h - PDB module information ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULEDESCRIPTORBUILDER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULEDESCRIPTORBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
class BinaryStreamWriter;
namespace codeview {
class DebugSubsection;
}

namespace msf {
class MSFBuilder;
struct MSFLayout;
}
namespace pdb {

/// Holds merged or unmerged CodeView symbol bytes for a module.
///
/// Merged symbols can be written to the output file as is, but unmerged
/// symbols must be rewritten first. In either case, the size must be known up
/// front.
struct SymbolListWrapper {
  /// Construct a wrapper around already-merged symbol bytes in \p Syms.
  ///
  /// \param Syms Pre-merged symbol record bytes that can be written as-is.
  explicit SymbolListWrapper(ArrayRef<uint8_t> Syms)
      : SymPtr(const_cast<uint8_t *>(Syms.data())), SymSize(Syms.size()),
        NeedsToBeMerged(false) {}
  /// Construct a wrapper around unmerged symbol bytes that must be rewritten.
  ///
  /// \param SymSrc Opaque pointer to the unmerged symbol source.
  /// \param Length Size in bytes of the unmerged symbol data.
  explicit SymbolListWrapper(void *SymSrc, uint32_t Length)
      : SymPtr(SymSrc), SymSize(Length), NeedsToBeMerged(true) {}

  /// Return the wrapped symbol bytes as an \c ArrayRef.
  ///
  /// \returns The symbol bytes as an \c ArrayRef over \c SymPtr.
  ArrayRef<uint8_t> asArray() const {
    return ArrayRef<uint8_t>(static_cast<const uint8_t *>(SymPtr), SymSize);
  }

  /// Return the size in bytes of the wrapped symbol data.
  ///
  /// \returns The size in bytes of the data pointed to by \c SymPtr.
  uint32_t size() const { return SymSize; }

  /// Opaque pointer to the symbol bytes (merged or unmerged).
  void *SymPtr = nullptr;
  /// Size in bytes of the data pointed to by \c SymPtr.
  uint32_t SymSize = 0;
  /// True if the symbol bytes must be merged before writing.
  bool NeedsToBeMerged = false;
};

/// Represents a string table reference at some offset in the module symbol
/// stream.
struct StringTableFixup {
  /// Offset of the referenced string within the PDB string table.
  uint32_t StrTabOffset = 0;
  /// Offset within the module symbol stream of the uint32_t that references
  /// the string table entry.
  uint32_t SymOffsetOfReference = 0;
};

/// Builds one module's DBI descriptor and its associated module symbol stream.
class DbiModuleDescriptorBuilder {
  friend class DbiStreamBuilder;

public:
  /// Construct a module descriptor builder for \p ModuleName.
  ///
  /// \param ModuleName The module name stored in the DBI module descriptor.
  /// \param ModIndex Zero-based module index assigned in the DBI stream.
  /// \param Msf The MSF builder used to allocate the module debug info stream.
  LLVM_ABI DbiModuleDescriptorBuilder(StringRef ModuleName, uint32_t ModIndex,
                                      msf::MSFBuilder &Msf);
  /// Destroy the module descriptor builder.
  LLVM_ABI ~DbiModuleDescriptorBuilder();

  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is not supported.
  DbiModuleDescriptorBuilder(const DbiModuleDescriptorBuilder &Other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param Other Unused; copy assignment is not supported.
  DbiModuleDescriptorBuilder &
  operator=(const DbiModuleDescriptorBuilder &Other) = delete;

  /// Set the name-index of the path to the compiler PDB for this module.
  ///
  /// \param NI String-table name index of the PDB file path.
  LLVM_ABI void setPdbFilePathNI(uint32_t NI);
  /// Set the object file name associated with this module.
  ///
  /// \param Name Object file path written after the module name in the
  ///        descriptor.
  LLVM_ABI void setObjFileName(StringRef Name);

  /// Callback invoked to merge one source of unmerged symbols into the writer.
  ///
  /// \param Ctx Opaque context pointer supplied to
  ///        \c setMergeSymbolsCallback.
  /// \param Symbols Opaque pointer to the unmerged symbol source.
  /// \param Writer Stream writer positioned where merged symbols should be
  ///        written.
  ///
  /// \returns An Error on failure, or success if the symbols were merged.
  using MergeSymbolsCallback = Error (*)(void *Ctx, void *Symbols,
                                         BinaryStreamWriter &Writer);

  /// Set the callback used to merge unmerged symbol sources at commit time.
  ///
  /// \param Ctx Opaque context passed to \p Callback.
  /// \param Callback Function that rewrites unmerged symbols into \p Writer.
  void setMergeSymbolsCallback(void *Ctx, MergeSymbolsCallback Callback) {
    MergeSymsCtx = Ctx;
    MergeSymsCallback = Callback;
  }

  /// Set the string-table fixups to apply when committing the symbol stream.
  ///
  /// \param Fixups Fixups moved into this builder; previous fixups are
  ///        replaced.
  void setStringTableFixups(std::vector<StringTableFixup> &&Fixups) {
    StringTableFixups = std::move(Fixups);
  }

  /// Set the first section contribution recorded for this module.
  ///
  /// \param SC Section contribution written into the module info header.
  LLVM_ABI void setFirstSectionContrib(const SectionContrib &SC);
  /// Add a single CodeView symbol record to the module symbol stream.
  ///
  /// \param Symbol The symbol record whose bytes are appended.
  LLVM_ABI void addSymbol(codeview::CVSymbol Symbol);
  /// Add a contiguous run of already-merged symbol bytes.
  ///
  /// \param BulkSymbols Pre-merged, 4-byte-aligned symbol record bytes.
  LLVM_ABI void addSymbolsInBulk(ArrayRef<uint8_t> BulkSymbols);

  /// Add symbols of known size which will be merged (rewritten) when committing
  /// the PDB to disk.
  ///
  /// \param SymSrc Opaque pointer to the unmerged symbol source.
  /// \param SymLength Size in bytes of the unmerged symbol data; must be
  ///        4-byte aligned.
  LLVM_ABI void addUnmergedSymbols(void *SymSrc, uint32_t SymLength);

  /// Add a C13 debug subsection owned by this builder.
  ///
  /// \param Subsection Shared ownership of the subsection to serialize.
  LLVM_ABI void
  addDebugSubsection(std::shared_ptr<codeview::DebugSubsection> Subsection);

  /// Add a C13 debug subsection from an existing subsection record.
  ///
  /// \param SubsectionContents Pre-built subsection record to serialize.
  LLVM_ABI void
  addDebugSubsection(const codeview::DebugSubsectionRecord &SubsectionContents);

  /// Return the MSF stream index of this module's debug info stream.
  ///
  /// \returns The MSF stream index of this module's debug info stream.
  LLVM_ABI uint16_t getStreamIndex() const;
  /// Return the module name stored in the descriptor.
  ///
  /// \returns The module name stored in the descriptor.
  StringRef getModuleName() const { return ModuleName; }
  /// Return the object file name stored in the descriptor.
  ///
  /// \returns The object file name stored in the descriptor.
  StringRef getObjFileName() const { return ObjFileName; }

  /// Return the zero-based module index assigned to this descriptor.
  ///
  /// \returns The zero-based module index assigned to this descriptor.
  unsigned getModuleIndex() const { return Layout.Mod; }

  /// Return the list of source file paths contributing to this module.
  ///
  /// \returns The list of source file paths contributing to this module.
  ArrayRef<std::string> source_files() const { return SourceFiles; }

  /// Return the on-disk size of this module's DBI descriptor record.
  ///
  /// \returns The on-disk size in bytes of this module's DBI descriptor.
  LLVM_ABI uint32_t calculateSerializedLength() const;

  /// Return the offset within the module symbol stream of the next symbol
  /// record passed to \c addSymbol.
  ///
  /// Add four to account for the signature.
  ///
  /// \returns The byte offset where the next symbol record will be written.
  uint32_t getNextSymbolOffset() const { return SymbolByteSize + 4; }

  /// Finalize fields of the module info header before writing.
  LLVM_ABI void finalize();
  /// Allocate the module debug info stream in the MSF and record its index.
  ///
  /// \returns An Error if stream allocation fails, otherwise success.
  LLVM_ABI Error finalizeMsfLayout();

  /// Commit the DBI descriptor to the DBI stream.
  ///
  /// \param ModiWriter Writer positioned at this module's entry in the DBI
  ///        module info substream.
  ///
  /// \returns An Error on write failure, otherwise success.
  LLVM_ABI Error commit(BinaryStreamWriter &ModiWriter);

  /// Commit the accumulated symbols to the module symbol stream.
  ///
  /// Safe to call in parallel on different DbiModuleDescriptorBuilder objects.
  /// Only modifies the pre-allocated stream in question.
  ///
  /// \param MsfLayout The finalized MSF layout describing stream block maps.
  /// \param MsfBuffer Writable view of the MSF file that receives the stream.
  ///
  /// \returns An Error on write failure, otherwise success.
  LLVM_ABI Error commitSymbolStream(const msf::MSFLayout &MsfLayout,
                                    WritableBinaryStreamRef MsfBuffer);

private:
  uint32_t calculateC13DebugInfoSize() const;

  void addSourceFile(StringRef Path);
  msf::MSFBuilder &MSF;

  uint32_t SymbolByteSize = 0;
  uint32_t PdbFilePathNI = 0;
  std::string ModuleName;
  std::string ObjFileName;
  std::vector<std::string> SourceFiles;
  std::vector<SymbolListWrapper> Symbols;

  void *MergeSymsCtx = nullptr;
  MergeSymbolsCallback MergeSymsCallback = nullptr;

  std::vector<StringTableFixup> StringTableFixups;

  std::vector<codeview::DebugSubsectionRecordBuilder> C13Builders;

  ModuleInfoHeader Layout;
};

} // end namespace pdb

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULEDESCRIPTORBUILDER_H
