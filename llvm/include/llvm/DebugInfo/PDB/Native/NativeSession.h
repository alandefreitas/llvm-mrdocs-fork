//===- NativeSession.h - Native implementation of IPDBSession ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVESESSION_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVESESSION_H

#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/Native/SymbolCache.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
class MemoryBuffer;
namespace pdb {
class PDBFile;
class NativeExeSymbol;
class IPDBSourceFile;
class ModuleDebugStreamRef;
class PDBSymbol;
class PDBSymbolCompiland;
class PDBSymbolExe;
template <typename ChildType> class IPDBEnumChildren;

/// NativeSession is an IPDBSession backed by LLVM's native PDB reader.
class LLVM_ABI NativeSession : public IPDBSession {
  struct PdbSearchOptions {
    StringRef ExePath;
    // FIXME: Add other PDB search options (_NT_SYMBOL_PATH, symsrv)
  };

public:
  /// Construct a native session from an already-parsed PDB file.
  ///
  /// \param PdbFile The parsed PDB file to own for the lifetime of the session.
  /// \param Allocator The bump allocator used while parsing the PDB.
  NativeSession(std::unique_ptr<PDBFile> PdbFile,
                std::unique_ptr<BumpPtrAllocator> Allocator);

  /// Destroy the native session.
  ~NativeSession() override;

  /// Create a native session from an in-memory PDB buffer.
  ///
  /// \param MB The memory buffer containing PDB bytes; ownership is transferred.
  /// \param Session On success, set to the newly created session.
  ///
  /// \returns Success, or an error if the PDB could not be parsed.
  static Error createFromPdb(std::unique_ptr<MemoryBuffer> MB,
                             std::unique_ptr<IPDBSession> &Session);

  /// Create a native session by loading a PDB from a filesystem path.
  ///
  /// \param PdbPath Path to the PDB file on disk.
  /// \param Session On success, set to the newly created session.
  ///
  /// \returns Success, or an error if the file could not be opened or parsed.
  static Error createFromPdbPath(StringRef PdbPath,
                                 std::unique_ptr<IPDBSession> &Session);

  /// Create a native session by locating the PDB for an executable.
  ///
  /// \param Path Path to the executable whose PDB should be loaded.
  /// \param Session On success, set to the newly created session.
  ///
  /// \returns Success, or an error if the PDB could not be found or parsed.
  static Error createFromExe(StringRef Path,
                             std::unique_ptr<IPDBSession> &Session);

  /// Search for a PDB path matching the given options.
  ///
  /// \param Opts Search options describing the executable and lookup rules.
  ///
  /// \returns The discovered PDB path, or an error if none was found.
  static Expected<std::string> searchForPdb(const PdbSearchOptions &Opts);

  /// Return the preferred load address of the executable image.
  ///
  /// \returns The preferred load address currently set for the image.
  uint64_t getLoadAddress() const override;

  /// Set the preferred load address of the executable image.
  ///
  /// \param Address The new preferred load address.
  ///
  /// \returns True if the load address was updated successfully.
  bool setLoadAddress(uint64_t Address) override;

  /// Return the global EXE scope symbol for this session.
  ///
  /// \returns The EXE scope symbol representing the global PDB scope.
  std::unique_ptr<PDBSymbolExe> getGlobalScope() override;

  /// Look up a symbol by its symbol index ID.
  ///
  /// \param SymbolId The symbol index ID to resolve.
  ///
  /// \returns The matching symbol, or nullptr if none exists.
  std::unique_ptr<PDBSymbol> getSymbolById(SymIndexId SymbolId) const override;

  /// Convert a virtual address to a section and offset.
  ///
  /// \param VA The absolute virtual address to convert.
  /// \param Section On success, set to the section index.
  /// \param Offset On success, set to the offset within the section.
  ///
  /// \returns True if the conversion succeeded.
  bool addressForVA(uint64_t VA, uint32_t &Section,
                    uint32_t &Offset) const override;

  /// Convert a relative virtual address to a section and offset.
  ///
  /// \param RVA The relative virtual address to convert.
  /// \param Section On success, set to the section index.
  /// \param Offset On success, set to the offset within the section.
  ///
  /// \returns True if the conversion succeeded.
  bool addressForRVA(uint32_t RVA, uint32_t &Section,
                     uint32_t &Offset) const override;

  /// Find a symbol of the given type at an absolute virtual address.
  ///
  /// \param Address The absolute virtual address to search.
  /// \param Type The symbol type tag to match, or a wildcard.
  ///
  /// \returns The matching symbol, or nullptr if none is found.
  std::unique_ptr<PDBSymbol> findSymbolByAddress(uint64_t Address,
                                                 PDB_SymType Type) override;

  /// Find a symbol of the given type at a relative virtual address.
  ///
  /// \param RVA The relative virtual address to search.
  /// \param Type The symbol type tag to match, or a wildcard.
  ///
  /// \returns The matching symbol, or nullptr if none is found.
  std::unique_ptr<PDBSymbol> findSymbolByRVA(uint32_t RVA,
                                             PDB_SymType Type) override;

  /// Find a symbol of the given type at a section and offset.
  ///
  /// \param Sect The section index.
  /// \param Offset The offset within the section.
  /// \param Type The symbol type tag to match, or a wildcard.
  ///
  /// \returns The matching symbol, or nullptr if none is found.
  std::unique_ptr<PDBSymbol> findSymbolBySectOffset(uint32_t Sect,
                                                    uint32_t Offset,
                                                    PDB_SymType Type) override;

  /// Enumerate line numbers for a source file within a compiland.
  ///
  /// \param Compiland The compiland whose line info to search.
  /// \param File The source file whose lines to enumerate.
  ///
  /// \returns An enumerator over matching line numbers, or nullptr.
  std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbers(const PDBSymbolCompiland &Compiland,
                  const IPDBSourceFile &File) const override;

  /// Enumerate line numbers covering an absolute virtual address range.
  ///
  /// \param Address The starting absolute virtual address.
  /// \param Length The length in bytes of the address range.
  ///
  /// \returns An enumerator over matching line numbers, or nullptr.
  std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersByAddress(uint64_t Address, uint32_t Length) const override;

  /// Enumerate line numbers covering a relative virtual address range.
  ///
  /// \param RVA The starting relative virtual address.
  /// \param Length The length in bytes of the address range.
  ///
  /// \returns An enumerator over matching line numbers, or nullptr.
  std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersByRVA(uint32_t RVA, uint32_t Length) const override;

  /// Enumerate line numbers covering a section and offset range.
  ///
  /// \param Section The section index.
  /// \param Offset The starting offset within the section.
  /// \param Length The length in bytes of the address range.
  ///
  /// \returns An enumerator over matching line numbers, or nullptr.
  std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersBySectOffset(uint32_t Section, uint32_t Offset,
                              uint32_t Length) const override;

  /// Find source files matching a name pattern, optionally within a compiland.
  ///
  /// \param Compiland The compiland to restrict the search to, or nullptr.
  /// \param Pattern The source file name or pattern to match.
  /// \param Flags Name-search options controlling the match.
  ///
  /// \returns An enumerator over matching source files, or nullptr.
  std::unique_ptr<IPDBEnumSourceFiles>
  findSourceFiles(const PDBSymbolCompiland *Compiland, llvm::StringRef Pattern,
                  PDB_NameSearchFlags Flags) const override;

  /// Find the first source file matching a name pattern.
  ///
  /// \param Compiland The compiland to restrict the search to, or nullptr.
  /// \param Pattern The source file name or pattern to match.
  /// \param Flags Name-search options controlling the match.
  ///
  /// \returns The first matching source file, or nullptr if none is found.
  std::unique_ptr<IPDBSourceFile>
  findOneSourceFile(const PDBSymbolCompiland *Compiland,
                    llvm::StringRef Pattern,
                    PDB_NameSearchFlags Flags) const override;

  /// Find compilands that reference a source file matching a name pattern.
  ///
  /// \param Pattern The source file name or pattern to match.
  /// \param Flags Name-search options controlling the match.
  ///
  /// \returns An enumerator over matching compilands, or nullptr.
  std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>
  findCompilandsForSourceFile(llvm::StringRef Pattern,
                              PDB_NameSearchFlags Flags) const override;

  /// Find the first compiland that references a source file matching a pattern.
  ///
  /// \param Pattern The source file name or pattern to match.
  /// \param Flags Name-search options controlling the match.
  ///
  /// \returns The first matching compiland, or nullptr if none is found.
  std::unique_ptr<PDBSymbolCompiland>
  findOneCompilandForSourceFile(llvm::StringRef Pattern,
                                PDB_NameSearchFlags Flags) const override;

  /// Enumerate every source file recorded in the debug data source.
  ///
  /// \returns An enumerator over all source files, or nullptr.
  std::unique_ptr<IPDBEnumSourceFiles> getAllSourceFiles() const override;

  /// Enumerate source files associated with a specific compiland.
  ///
  /// \param Compiland The compiland whose source files to enumerate.
  ///
  /// \returns An enumerator over the compiland's source files, or nullptr.
  std::unique_ptr<IPDBEnumSourceFiles> getSourceFilesForCompiland(
      const PDBSymbolCompiland &Compiland) const override;

  /// Look up a source file by its unique file ID.
  ///
  /// \param FileId The unique source file ID to resolve.
  ///
  /// \returns The matching source file, or nullptr if none exists.
  std::unique_ptr<IPDBSourceFile>
  getSourceFileById(uint32_t FileId) const override;

  /// Enumerate the debug data streams in the PDB.
  ///
  /// \returns An enumerator over debug streams, or nullptr.
  std::unique_ptr<IPDBEnumDataStreams> getDebugStreams() const override;

  /// Enumerate the tables available in the PDB.
  ///
  /// \returns An enumerator over PDB tables, or nullptr.
  std::unique_ptr<IPDBEnumTables> getEnumTables() const override;

  /// Enumerate source files injected into the PDB.
  ///
  /// \returns An enumerator over injected sources, or nullptr.
  std::unique_ptr<IPDBEnumInjectedSources> getInjectedSources() const override;

  /// Enumerate section contributions recorded in the PDB.
  ///
  /// \returns An enumerator over section contributions, or nullptr.
  std::unique_ptr<IPDBEnumSectionContribs> getSectionContribs() const override;

  /// Enumerate frame data records for stack unwinding.
  ///
  /// \returns An enumerator over frame data records, or nullptr.
  std::unique_ptr<IPDBEnumFrameData> getFrameData() const override;

  /// Return the underlying native PDB file.
  ///
  /// \returns A reference to the owned PDB file.
  PDBFile &getPDBFile() { return *Pdb; }

  /// Return the underlying native PDB file.
  ///
  /// \returns A const reference to the owned PDB file.
  const PDBFile &getPDBFile() const { return *Pdb; }

  /// Return the native EXE scope symbol for this session.
  ///
  /// \returns A reference to the cached native global scope symbol.
  NativeExeSymbol &getNativeGlobalScope() const;

  /// Return the session's symbol cache.
  ///
  /// \returns A reference to the symbol cache for this session.
  SymbolCache &getSymbolCache() { return Cache; }

  /// Return the session's symbol cache.
  ///
  /// \returns A const reference to the symbol cache for this session.
  const SymbolCache &getSymbolCache() const { return Cache; }

  /// Convert a section and offset to a relative virtual address.
  ///
  /// \param Section The section index.
  /// \param Offset The offset within the section.
  ///
  /// \returns The corresponding RVA, or 0 if the mapping fails.
  uint32_t getRVAFromSectOffset(uint32_t Section, uint32_t Offset) const;

  /// Convert a section and offset to an absolute virtual address.
  ///
  /// \param Section The section index.
  /// \param Offset The offset within the section.
  ///
  /// \returns The corresponding VA using the current load address.
  uint64_t getVAFromSectOffset(uint32_t Section, uint32_t Offset) const;

  /// Look up the module index that contributes a virtual address.
  ///
  /// \param VA The absolute virtual address to resolve.
  /// \param ModuleIndex On success, set to the matching module index.
  ///
  /// \returns True if a module contribution covers \p VA.
  bool moduleIndexForVA(uint64_t VA, uint16_t &ModuleIndex) const;

  /// Look up the module index that contributes a section and offset.
  ///
  /// \param Sect The section index.
  /// \param Offset The offset within the section.
  /// \param ModuleIndex On success, set to the matching module index.
  ///
  /// \returns True if a module contribution covers the address.
  bool moduleIndexForSectOffset(uint32_t Sect, uint32_t Offset,
                                uint16_t &ModuleIndex) const;

  /// Open the module debug stream for a DBI module index.
  ///
  /// \param Index The zero-based module index in the DBI stream.
  ///
  /// \returns A reference to the module debug stream, or an error.
  Expected<ModuleDebugStreamRef> getModuleDebugStream(uint32_t Index) const;

#ifndef NDEBUG
  /// Assert that a virtual address range is well-formed for debug checks.
  ///
  /// \param Start The inclusive start of the address range.
  /// \param Stop The exclusive end of the address range.
  void checkSymbolRange(uint64_t Start, uint64_t Stop) const;
#endif

private:
  void initializeExeSymbol();
  void parseSectionContribs();

  std::unique_ptr<PDBFile> Pdb;
  std::unique_ptr<BumpPtrAllocator> Allocator;

  SymbolCache Cache;
  SymIndexId ExeSymbol = 0;
  uint64_t LoadAddress = 0;

  /// Map from virtual address to module index.
  using IMap =
      IntervalMap<uint64_t, uint16_t, 8, IntervalMapHalfOpenInfo<uint64_t>>;
  IMap::Allocator IMapAllocator;
  IMap AddrToModuleIndex;
};
} // namespace pdb
} // namespace llvm

#endif
