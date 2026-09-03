//===- IPDBSession.h - base interface for a PDB symbol context --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBSESSION_H
#define LLVM_DEBUGINFO_PDB_IPDBSESSION_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {
namespace pdb {
class PDBSymbolCompiland;
class PDBSymbolExe;

/// IPDBSession defines an interface used to provide a context for querying
/// debug information from a debug data source (for example, a PDB).
class LLVM_ABI IPDBSession {
public:
  /// Virtual destructor.
  virtual ~IPDBSession();

  /// Get the preferred load address of the executable image.
  ///
  /// \returns The preferred load address of the executable image.
  virtual uint64_t getLoadAddress() const = 0;

  /// Set the preferred load address of the executable image.
  ///
  /// \param Address The new preferred load address.
  ///
  /// \returns True if the load address was updated successfully.
  virtual bool setLoadAddress(uint64_t Address) = 0;

  /// Get the global EXE scope symbol for this session.
  ///
  /// \returns The global EXE scope symbol for this session.
  virtual std::unique_ptr<PDBSymbolExe> getGlobalScope() = 0;

  /// Look up a symbol by its symbol index ID.
  ///
  /// \param SymbolId The symbol index ID to resolve.
  ///
  /// \returns The matching symbol, or nullptr if none exists.
  virtual std::unique_ptr<PDBSymbol>
  getSymbolById(SymIndexId SymbolId) const = 0;

  /// Convert a virtual address to a section and offset.
  ///
  /// \param VA The absolute virtual address to convert.
  /// \param Section On success, set to the section index.
  /// \param Offset On success, set to the offset within the section.
  ///
  /// \returns True if the conversion succeeded.
  virtual bool addressForVA(uint64_t VA, uint32_t &Section,
                            uint32_t &Offset) const = 0;

  /// Convert a relative virtual address to a section and offset.
  ///
  /// \param RVA The relative virtual address to convert.
  /// \param Section On success, set to the section index.
  /// \param Offset On success, set to the offset within the section.
  ///
  /// \returns True if the conversion succeeded.
  virtual bool addressForRVA(uint32_t RVA, uint32_t &Section,
                             uint32_t &Offset) const = 0;

  /// Look up a symbol by ID and cast it to a concrete symbol type.
  ///
  /// \param SymbolId The symbol index ID to resolve.
  ///
  /// \returns The symbol cast to \c T, or nullptr if missing or of another type.
  template <typename T>
  std::unique_ptr<T> getConcreteSymbolById(SymIndexId SymbolId) const {
    return unique_dyn_cast_or_null<T>(getSymbolById(SymbolId));
  }

  /// Find a symbol of the given type at an absolute virtual address.
  ///
  /// \param Address The absolute virtual address to search.
  /// \param Type The symbol type tag to match, or a wildcard.
  ///
  /// \returns The matching symbol, or nullptr if none is found.
  virtual std::unique_ptr<PDBSymbol> findSymbolByAddress(uint64_t Address,
                                                         PDB_SymType Type) = 0;

  /// Find a symbol of the given type at a relative virtual address.
  ///
  /// \param RVA The relative virtual address to search.
  /// \param Type The symbol type tag to match, or a wildcard.
  ///
  /// \returns The matching symbol, or nullptr if none is found.
  virtual std::unique_ptr<PDBSymbol> findSymbolByRVA(uint32_t RVA,
                                                     PDB_SymType Type) = 0;

  /// Find a symbol of the given type at a section and offset.
  ///
  /// \param Sect The section index.
  /// \param Offset The offset within the section.
  /// \param Type The symbol type tag to match, or a wildcard.
  ///
  /// \returns The matching symbol, or nullptr if none is found.
  virtual std::unique_ptr<PDBSymbol>
  findSymbolBySectOffset(uint32_t Sect, uint32_t Offset, PDB_SymType Type) = 0;

  /// Enumerate line numbers for a source file within a compiland.
  ///
  /// \param Compiland The compiland whose line info to search.
  /// \param File The source file whose lines to enumerate.
  ///
  /// \returns An enumerator over matching line numbers, or nullptr.
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbers(const PDBSymbolCompiland &Compiland,
                  const IPDBSourceFile &File) const = 0;

  /// Enumerate line numbers covering an absolute virtual address range.
  ///
  /// \param Address The starting absolute virtual address.
  /// \param Length The length in bytes of the address range.
  ///
  /// \returns An enumerator over matching line numbers, or nullptr.
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersByAddress(uint64_t Address, uint32_t Length) const = 0;

  /// Enumerate line numbers covering a relative virtual address range.
  ///
  /// \param RVA The starting relative virtual address.
  /// \param Length The length in bytes of the address range.
  ///
  /// \returns An enumerator over matching line numbers, or nullptr.
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersByRVA(uint32_t RVA, uint32_t Length) const = 0;

  /// Enumerate line numbers covering a section and offset range.
  ///
  /// \param Section The section index.
  /// \param Offset The starting offset within the section.
  /// \param Length The length in bytes of the address range.
  ///
  /// \returns An enumerator over matching line numbers, or nullptr.
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersBySectOffset(uint32_t Section, uint32_t Offset,
                              uint32_t Length) const = 0;

  /// Find source files matching a name pattern, optionally within a compiland.
  ///
  /// \param Compiland The compiland to restrict the search to, or nullptr.
  /// \param Pattern The source file name or pattern to match.
  /// \param Flags Name-search options controlling the match.
  ///
  /// \returns An enumerator over matching source files, or nullptr.
  virtual std::unique_ptr<IPDBEnumSourceFiles>
  findSourceFiles(const PDBSymbolCompiland *Compiland, llvm::StringRef Pattern,
                  PDB_NameSearchFlags Flags) const = 0;

  /// Find the first source file matching a name pattern.
  ///
  /// \param Compiland The compiland to restrict the search to, or nullptr.
  /// \param Pattern The source file name or pattern to match.
  /// \param Flags Name-search options controlling the match.
  ///
  /// \returns The first matching source file, or nullptr if none is found.
  virtual std::unique_ptr<IPDBSourceFile>
  findOneSourceFile(const PDBSymbolCompiland *Compiland,
                    llvm::StringRef Pattern,
                    PDB_NameSearchFlags Flags) const = 0;

  /// Find compilands that reference a source file matching a name pattern.
  ///
  /// \param Pattern The source file name or pattern to match.
  /// \param Flags Name-search options controlling the match.
  ///
  /// \returns An enumerator over matching compilands, or nullptr.
  virtual std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>
  findCompilandsForSourceFile(llvm::StringRef Pattern,
                              PDB_NameSearchFlags Flags) const = 0;

  /// Find the first compiland that references a source file matching a pattern.
  ///
  /// \param Pattern The source file name or pattern to match.
  /// \param Flags Name-search options controlling the match.
  ///
  /// \returns The first matching compiland, or nullptr if none is found.
  virtual std::unique_ptr<PDBSymbolCompiland>
  findOneCompilandForSourceFile(llvm::StringRef Pattern,
                                PDB_NameSearchFlags Flags) const = 0;

  /// Enumerate every source file recorded in the debug data source.
  ///
  /// \returns An enumerator over all source files, or nullptr.
  virtual std::unique_ptr<IPDBEnumSourceFiles> getAllSourceFiles() const = 0;

  /// Enumerate source files associated with a specific compiland.
  ///
  /// \param Compiland The compiland whose source files to enumerate.
  ///
  /// \returns An enumerator over the compiland's source files, or nullptr.
  virtual std::unique_ptr<IPDBEnumSourceFiles>
  getSourceFilesForCompiland(const PDBSymbolCompiland &Compiland) const = 0;

  /// Look up a source file by its unique file ID.
  ///
  /// \param FileId The unique source file ID to resolve.
  ///
  /// \returns The matching source file, or nullptr if none exists.
  virtual std::unique_ptr<IPDBSourceFile>
  getSourceFileById(uint32_t FileId) const = 0;

  /// Enumerate the debug data streams in the PDB.
  ///
  /// \returns An enumerator over debug streams, or nullptr.
  virtual std::unique_ptr<IPDBEnumDataStreams> getDebugStreams() const = 0;

  /// Enumerate the tables available in the PDB.
  ///
  /// \returns An enumerator over PDB tables, or nullptr.
  virtual std::unique_ptr<IPDBEnumTables> getEnumTables() const = 0;

  /// Enumerate source files injected into the PDB.
  ///
  /// \returns An enumerator over injected sources, or nullptr.
  virtual std::unique_ptr<IPDBEnumInjectedSources>
  getInjectedSources() const = 0;

  /// Enumerate section contributions recorded in the PDB.
  ///
  /// \returns An enumerator over section contributions, or nullptr.
  virtual std::unique_ptr<IPDBEnumSectionContribs>
  getSectionContribs() const = 0;

  /// Enumerate frame data records for stack unwinding.
  ///
  /// \returns An enumerator over frame data records, or nullptr.
  virtual std::unique_ptr<IPDBEnumFrameData>
  getFrameData() const = 0;
};
} // namespace pdb
} // namespace llvm

#endif
