//==- SymbolCache.h - Cache of native symbols and ids ------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_SYMBOLCACHE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_SYMBOLCACHE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntervalMap.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/Line.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSourceFile.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Compiler.h"

#include <memory>
#include <vector>

namespace llvm {
namespace codeview {
class InlineSiteSym;
struct FileChecksumEntry;
} // namespace codeview
namespace pdb {
class IPDBSourceFile;
class NativeSession;
class PDBSymbol;
class PDBSymbolCompiland;
class DbiStream;

/// Cache of parsed native PDB symbols and stable symbol index IDs.
class SymbolCache {
  NativeSession &Session;
  DbiStream *Dbi = nullptr;

  /// Cache of all stable symbols, indexed by SymIndexId.  Just because a
  /// symbol has been parsed does not imply that it will be stable and have
  /// an Id.  Id allocation is an implementation, with the only guarantee
  /// being that once an Id is allocated, the symbol can be assumed to be
  /// cached.
  mutable std::vector<std::unique_ptr<NativeRawSymbol>> Cache;

  /// For type records from the TPI stream which have been paresd and cached,
  /// stores a mapping to SymIndexId of the cached symbol.
  mutable DenseMap<codeview::TypeIndex, SymIndexId> TypeIndexToSymbolId;

  /// For field list members which have been parsed and cached, stores a mapping
  /// from (IndexOfClass, MemberIndex) to the corresponding SymIndexId of the
  /// cached symbol.
  mutable DenseMap<std::pair<codeview::TypeIndex, uint32_t>, SymIndexId>
      FieldListMembersToSymbolId;

  /// List of SymIndexIds for each compiland, indexed by compiland index as they
  /// appear in the PDB file.
  mutable std::vector<SymIndexId> Compilands;

  /// List of source files, indexed by unique source file index.
  mutable std::vector<std::unique_ptr<NativeSourceFile>> SourceFiles;

  /// Map from string table offset to source file Id.
  mutable DenseMap<uint32_t, SymIndexId> FileNameOffsetToId;

  /// Map from global symbol offset to SymIndexId.
  mutable DenseMap<uint32_t, SymIndexId> GlobalOffsetToSymbolId;

  /// Map from virtual address range to function symbols.
  using IMapTy =
      IntervalMap<uint64_t, SymIndexId, 8, IntervalMapHalfOpenInfo<uint64_t>>;
  IMapTy::Allocator IMapAllocator;
  mutable IMapTy AddressToSymbolId;
  DenseSet<uint16_t> FuncSymCachedModIndexes;

  /// Map from segment and code offset to public symbols.
  mutable DenseMap<std::pair<uint32_t, uint32_t>, SymIndexId>
      AddressToPublicSymId;

  /// Map from module index and symbol table offset to SymIndexId.
  mutable DenseMap<std::pair<uint16_t, uint32_t>, SymIndexId>
      SymTabOffsetToSymbolId;

  struct LineTableEntry {
    uint64_t Addr;
    codeview::LineInfo Line;
    uint32_t ColumnNumber;
    uint32_t FileNameIndex;
    bool IsTerminalEntry;
  };

  std::vector<LineTableEntry> findLineTable(uint16_t Modi) const;
  mutable DenseMap<uint16_t, std::vector<LineTableEntry>> LineTable;

  SymIndexId createSymbolPlaceholder() const {
    SymIndexId Id = Cache.size();
    Cache.push_back(nullptr);
    return Id;
  }

  template <typename ConcreteSymbolT, typename CVRecordT, typename... Args>
  SymIndexId createSymbolForType(codeview::TypeIndex TI, codeview::CVType CVT,
                                 Args &&...ConstructorArgs) const {
    CVRecordT Record;
    if (auto EC =
            codeview::TypeDeserializer::deserializeAs<CVRecordT>(CVT, Record)) {
      consumeError(std::move(EC));
      return 0;
    }

    return createSymbol<ConcreteSymbolT>(
        TI, std::move(Record), std::forward<Args>(ConstructorArgs)...);
  }

  SymIndexId createSymbolForModifiedType(codeview::TypeIndex ModifierTI,
                                         codeview::CVType CVT) const;

  SymIndexId createSimpleType(codeview::TypeIndex TI,
                              codeview::ModifierOptions Mods) const;

  std::unique_ptr<PDBSymbol> findFunctionSymbolByVA(uint64_t VA);
  std::unique_ptr<PDBSymbol> findPublicSymbolBySectOffset(uint32_t Sect,
                                                          uint32_t Offset);

public:
  /// Construct a symbol cache bound to \p Session and optional DBI stream.
  ///
  /// \param Session The native PDB session that owns this cache.
  /// \param Dbi The DBI stream providing module/compiland metadata, or null.
  LLVM_ABI SymbolCache(NativeSession &Session, DbiStream *Dbi);

  /// Create a concrete native symbol, cache it, and return its symbol index ID.
  ///
  /// \param ConstructorArgs Arguments forwarded to the concrete symbol constructor.
  ///
  /// \returns The newly allocated stable symbol index ID.
  template <typename ConcreteSymbolT, typename... Args>
  SymIndexId createSymbol(Args &&...ConstructorArgs) const {
    SymIndexId Id = Cache.size();

    // Initial construction must not access the cache, since it must be done
    // atomically.
    auto Result = std::make_unique<ConcreteSymbolT>(
        Session, Id, std::forward<Args>(ConstructorArgs)...);
    Result->SymbolId = Id;

    NativeRawSymbol *NRS = static_cast<NativeRawSymbol *>(Result.get());
    Cache.push_back(std::move(Result));

    // After the item is in the cache, we can do further initialization which
    // is then allowed to access the cache.
    NRS->initialize();
    return Id;
  }

  /// Create an enumerator over TPI types matching a single leaf kind.
  ///
  /// \param Kind The CodeView type leaf kind to enumerate.
  ///
  /// \returns An enumerator over matching type symbols, or nullptr on failure.
  LLVM_ABI std::unique_ptr<IPDBEnumSymbols>
  createTypeEnumerator(codeview::TypeLeafKind Kind);

  /// Create an enumerator over TPI types matching any of the given leaf kinds.
  ///
  /// \param Kinds The CodeView type leaf kinds to enumerate.
  ///
  /// \returns An enumerator over matching type symbols, or nullptr on failure.
  LLVM_ABI std::unique_ptr<IPDBEnumSymbols>
  createTypeEnumerator(std::vector<codeview::TypeLeafKind> Kinds);

  /// Create an enumerator over global symbols of the given kind.
  ///
  /// \param Kind The CodeView symbol kind to enumerate from the globals stream.
  ///
  /// \returns An enumerator over matching global symbols.
  LLVM_ABI std::unique_ptr<IPDBEnumSymbols>
  createGlobalsEnumerator(codeview::SymbolKind Kind);

  /// Look up or lazily create the cached symbol for a TPI type index.
  ///
  /// \param TI The CodeView type index to resolve.
  ///
  /// \returns The stable symbol index ID, or 0 if the type cannot be created.
  LLVM_ABI SymIndexId findSymbolByTypeIndex(codeview::TypeIndex TI) const;

  /// Look up or create a cached field-list member symbol.
  ///
  /// \param FieldListTI Type index of the field list that owns the member.
  /// \param Index Zero-based index of the member within the field list.
  /// \param ConstructorArgs Arguments forwarded when creating a new member symbol.
  ///
  /// \returns The stable symbol index ID for the field-list member.
  template <typename ConcreteSymbolT, typename... Args>
  SymIndexId getOrCreateFieldListMember(codeview::TypeIndex FieldListTI,
                                        uint32_t Index,
                                        Args &&... ConstructorArgs) {
    SymIndexId SymId = Cache.size();
    std::pair<codeview::TypeIndex, uint32_t> Key{FieldListTI, Index};
    auto Result = FieldListMembersToSymbolId.try_emplace(Key, SymId);
    if (Result.second)
      SymId =
          createSymbol<ConcreteSymbolT>(std::forward<Args>(ConstructorArgs)...);
    else
      SymId = Result.first->second;
    return SymId;
  }

  /// Look up or create a cached global symbol at the given symbol-stream offset.
  ///
  /// \param Offset Byte offset of the global symbol record in the symbol stream.
  ///
  /// \returns The stable symbol index ID, or 0 if creation fails.
  LLVM_ABI SymIndexId getOrCreateGlobalSymbolByOffset(uint32_t Offset);

  /// Look up or create a cached inline-site symbol.
  ///
  /// \param Sym The CodeView inline-site symbol record.
  /// \param ParentAddr Absolute virtual address of the enclosing function.
  /// \param Modi Module index that owns the inline site.
  /// \param RecordOffset Offset of the record in the module symbol table.
  ///
  /// \returns The stable symbol index ID for the inline site.
  LLVM_ABI SymIndexId getOrCreateInlineSymbol(codeview::InlineSiteSym Sym,
                                              uint64_t ParentAddr,
                                              uint16_t Modi,
                                              uint32_t RecordOffset) const;

  /// Find a symbol of the given type at an absolute virtual address.
  ///
  /// \param VA The absolute virtual address to search.
  /// \param Type The symbol type tag to match, or a wildcard.
  ///
  /// \returns The matching symbol, or nullptr if none is found.
  LLVM_ABI std::unique_ptr<PDBSymbol> findSymbolByVA(uint64_t VA,
                                                     PDB_SymType Type);

  /// Find line numbers covering the given virtual address range.
  ///
  /// \param VA Absolute virtual address of the start of the range.
  /// \param Length Length in bytes of the address range.
  ///
  /// \returns An enumerator over matching line-number entries, or nullptr.
  LLVM_ABI std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersByVA(uint64_t VA, uint32_t Length) const;

  /// Look up or create the cached compiland symbol for a module index.
  ///
  /// \param Index Zero-based module/compiland index in the DBI stream.
  ///
  /// \returns The compiland symbol, or nullptr if the index is invalid.
  LLVM_ABI std::unique_ptr<PDBSymbolCompiland>
  getOrCreateCompiland(uint32_t Index);

  /// Return the number of compilands (modules) in the PDB.
  ///
  /// \returns The module count from the DBI stream, or 0 if DBI is unavailable.
  LLVM_ABI uint32_t getNumCompilands() const;

  /// Return a high-level PDB symbol wrapper for a cached symbol index ID.
  ///
  /// \param SymbolId The symbol index ID to resolve.
  ///
  /// \returns The matching symbol, or nullptr if the ID is reserved or invalid.
  LLVM_ABI std::unique_ptr<PDBSymbol> getSymbolById(SymIndexId SymbolId) const;

  /// Return a reference to the cached native raw symbol for \p SymbolId.
  ///
  /// \param SymbolId The symbol index ID to resolve; must refer to a cached symbol.
  ///
  /// \returns A reference to the native raw symbol in the cache.
  LLVM_ABI NativeRawSymbol &getNativeSymbolById(SymIndexId SymbolId) const;

  /// Return a typed reference to the cached native raw symbol for \p SymbolId.
  ///
  /// \param SymbolId The symbol index ID to resolve; must refer to a cached symbol.
  ///
  /// \returns A reference cast to \c ConcreteT.
  template <typename ConcreteT>
  ConcreteT &getNativeSymbolById(SymIndexId SymbolId) const {
    return static_cast<ConcreteT &>(getNativeSymbolById(SymbolId));
  }

  /// Return a source-file object for a cached source-file index ID.
  ///
  /// \param FileId The source-file index ID to resolve.
  ///
  /// \returns The matching source file, or nullptr if the ID is reserved.
  LLVM_ABI std::unique_ptr<IPDBSourceFile>
  getSourceFileById(SymIndexId FileId) const;

  /// Look up or create a cached source file from a checksum entry.
  ///
  /// \param Checksum File checksum entry describing the source file name and hash.
  ///
  /// \returns The stable source-file index ID.
  LLVM_ABI SymIndexId
  getOrCreateSourceFile(const codeview::FileChecksumEntry &Checksum) const;
};

} // namespace pdb
} // namespace llvm

#endif
