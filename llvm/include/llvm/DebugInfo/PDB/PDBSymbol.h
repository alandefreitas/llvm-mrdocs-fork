//===- PDBSymbol.h - base class for user-facing symbol types -----*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOL_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOL_H

#include "IPDBRawSymbol.h"
#include "PDBExtras.h"
#include "PDBTypes.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"

#define FORWARD_SYMBOL_METHOD(MethodName)                                      \
  decltype(auto) MethodName() const { return RawSymbol->MethodName(); }

#define FORWARD_CONCRETE_SYMBOL_ID_METHOD_WITH_NAME(ConcreteType, PrivateName, \
                                                    PublicName)                \
  decltype(auto) PublicName##Id() const {                                      \
    return RawSymbol->PrivateName##Id();                                       \
  }                                                                            \
  std::unique_ptr<ConcreteType> PublicName() const {                           \
    uint32_t Id = PublicName##Id();                                            \
    return getConcreteSymbolByIdHelper<ConcreteType>(Id);                      \
  }

#define FORWARD_SYMBOL_ID_METHOD_WITH_NAME(PrivateName, PublicName)            \
  FORWARD_CONCRETE_SYMBOL_ID_METHOD_WITH_NAME(PDBSymbol, PrivateName,          \
                                              PublicName)

#define FORWARD_SYMBOL_ID_METHOD(MethodName)                                   \
  FORWARD_SYMBOL_ID_METHOD_WITH_NAME(MethodName, MethodName)

namespace llvm {

class StringRef;
class raw_ostream;

namespace pdb {
class IPDBSession;
class PDBSymDumper;
class PDBSymbol;
template <typename ChildType> class ConcreteSymbolEnumerator;

#define DECLARE_PDB_SYMBOL_CONCRETE_TYPE(TagValue)                             \
private:                                                                       \
  using PDBSymbol::PDBSymbol;                                                  \
  friend class PDBSymbol;                                                      \
                                                                               \
public:                                                                        \
  static const PDB_SymType Tag = TagValue;                                     \
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

#define DECLARE_PDB_SYMBOL_CUSTOM_TYPE(Condition)                              \
private:                                                                       \
  using PDBSymbol::PDBSymbol;                                                  \
  friend class PDBSymbol;                                                      \
                                                                               \
public:                                                                        \
  static bool classof(const PDBSymbol *S) { return Condition; }

/// Base class for concrete PDB symbol types such as functions and executables.
///
/// PDBSymbol defines the base of the inheritance hierarchy for concrete symbol
/// types (e.g. functions, executables, vtables, etc).  All concrete symbol
/// types inherit from PDBSymbol and expose the exact set of methods that are
/// valid for that particular symbol type, as described in the Microsoft
/// reference "Lexical and Class Hierarchy of Symbol Types":
/// https://msdn.microsoft.com/en-us/library/370hs6k4.aspx
class LLVM_ABI PDBSymbol {
  static std::unique_ptr<PDBSymbol> createSymbol(const IPDBSession &PDBSession,
                                                 PDB_SymType Tag);

protected:
  /// Construct a symbol bound to the given PDB session.
  ///
  /// \param PDBSession Session that owns this symbol's context.
  explicit PDBSymbol(const IPDBSession &PDBSession);

  /// Move-construct a symbol, transferring ownership of the raw symbol.
  ///
  /// \param Other Symbol to move from.
  PDBSymbol(PDBSymbol &&Other);

public:
  /// Create a typed PDB symbol that takes ownership of \p RawSymbol.
  ///
  /// \param PDBSession Session that provides the symbol context.
  /// \param RawSymbol Underlying raw symbol to wrap; ownership is transferred.
  ///
  /// \returns A concrete PDBSymbol subclass matching the raw symbol's tag.
  static std::unique_ptr<PDBSymbol>
  create(const IPDBSession &PDBSession,
         std::unique_ptr<IPDBRawSymbol> RawSymbol);

  /// Create a typed PDB symbol that references an existing raw symbol.
  ///
  /// \param PDBSession Session that provides the symbol context.
  /// \param RawSymbol Underlying raw symbol to wrap; ownership is not taken.
  ///
  /// \returns A concrete PDBSymbol subclass matching the raw symbol's tag.
  static std::unique_ptr<PDBSymbol> create(const IPDBSession &PDBSession,
                                           IPDBRawSymbol &RawSymbol);

  /// Create a symbol and cast it to the concrete type \c ConcreteT.
  ///
  /// \param PDBSession Session that provides the symbol context.
  /// \param RawSymbol Underlying raw symbol to wrap; ownership is transferred.
  ///
  /// \returns The symbol as \c ConcreteT, or nullptr if the tag does not match.
  template <typename ConcreteT>
  static std::unique_ptr<ConcreteT>
  createAs(const IPDBSession &PDBSession,
           std::unique_ptr<IPDBRawSymbol> RawSymbol) {
    std::unique_ptr<PDBSymbol> S = create(PDBSession, std::move(RawSymbol));
    return unique_dyn_cast_or_null<ConcreteT>(std::move(S));
  }

  /// Create a symbol and cast it to the concrete type \c ConcreteT.
  ///
  /// \param PDBSession Session that provides the symbol context.
  /// \param RawSymbol Underlying raw symbol to wrap; ownership is not taken.
  ///
  /// \returns The symbol as \c ConcreteT, or nullptr if the tag does not match.
  template <typename ConcreteT>
  static std::unique_ptr<ConcreteT> createAs(const IPDBSession &PDBSession,
                                             IPDBRawSymbol &RawSymbol) {
    std::unique_ptr<PDBSymbol> S = create(PDBSession, RawSymbol);
    return unique_dyn_cast_or_null<ConcreteT>(std::move(S));
  }

  /// Destroy the symbol.
  virtual ~PDBSymbol();

  /// Dump this symbol's contents using the given dumper.
  ///
  /// By default this will just call dump() on the underlying RawSymbol, which
  /// allows us to discover unknown properties, but individual implementations
  /// of PDBSymbol may override the behavior to only dump known fields.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  virtual void dump(PDBSymDumper &Dumper) const = 0;

  /// For certain PDBSymbolTypes, dumps additional information for the type that
  /// normally goes on the right side of the symbol.
  ///
  /// \param Dumper Visitor used to format and emit the right-hand side.
  virtual void dumpRight(PDBSymDumper &Dumper) const {}

  /// Dump this symbol with the default property formatting.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowFlags Bitmask of symbol-id fields to print.
  /// \param RecurseFlags Bitmask of symbol-id fields to expand recursively.
  void defaultDump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowFlags,
                   PdbSymbolIdField RecurseFlags) const;

  /// Dump this symbol's properties to the standard debug stream.
  void dumpProperties() const;

  /// Dump statistics about this symbol's child symbols.
  void dumpChildStats() const;

  /// Return the DIA symbol tag for this symbol.
  ///
  /// \returns The PDB_SymType tag classifying this symbol.
  PDB_SymType getSymTag() const;

  /// Return the unique symbol index ID for this symbol.
  ///
  /// \returns The symbol's unique index ID within the PDB.
  uint32_t getSymIndexId() const;

  /// Return the first child of concrete type \c T, or nullptr if none exist.
  ///
  /// \returns The first matching child, or nullptr if none exist.
  template <typename T> std::unique_ptr<T> findOneChild() const {
    auto Enumerator(findAllChildren<T>());
    if (!Enumerator)
      return nullptr;
    return Enumerator->getNext();
  }

  /// Enumerate all children whose tag matches concrete type \c T.
  ///
  /// \returns A typed enumerator over matching children, or nullptr.
  template <typename T>
  std::unique_ptr<ConcreteSymbolEnumerator<T>> findAllChildren() const {
    auto BaseIter = RawSymbol->findChildren(T::Tag);
    if (!BaseIter)
      return nullptr;
    return std::make_unique<ConcreteSymbolEnumerator<T>>(std::move(BaseIter));
  }

  /// Enumerate all children with the given symbol tag.
  ///
  /// \param Type Symbol tag of children to enumerate.
  ///
  /// \returns An enumerator over matching child symbols.
  std::unique_ptr<IPDBEnumSymbols> findAllChildren(PDB_SymType Type) const;

  /// Enumerate all child symbols regardless of tag.
  ///
  /// \returns An enumerator over all child symbols.
  std::unique_ptr<IPDBEnumSymbols> findAllChildren() const;

  /// Find child symbols of the given type whose name matches \p Name.
  ///
  /// \param Type Symbol tag of children to enumerate.
  /// \param Name Name pattern to match.
  /// \param Flags Name-search options controlling the match.
  ///
  /// \returns An enumerator over matching child symbols.
  std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type, StringRef Name,
               PDB_NameSearchFlags Flags) const;

  /// Find child symbols of the given type at a relative virtual address.
  ///
  /// \param Type Symbol tag of children to enumerate.
  /// \param Name Name pattern to match.
  /// \param Flags Name-search options controlling the match.
  /// \param RVA Relative virtual address to search.
  ///
  /// \returns An enumerator over matching child symbols.
  std::unique_ptr<IPDBEnumSymbols> findChildrenByRVA(PDB_SymType Type,
                                                     StringRef Name,
                                                     PDB_NameSearchFlags Flags,
                                                     uint32_t RVA) const;

  /// Find inline frames covering the given absolute virtual address.
  ///
  /// \param VA Absolute virtual address to search.
  ///
  /// \returns An enumerator over matching inline frame symbols.
  std::unique_ptr<IPDBEnumSymbols> findInlineFramesByVA(uint64_t VA) const;

  /// Find inline frames covering the given relative virtual address.
  ///
  /// \param RVA Relative virtual address to search.
  ///
  /// \returns An enumerator over matching inline frame symbols.
  std::unique_ptr<IPDBEnumSymbols> findInlineFramesByRVA(uint32_t RVA) const;

  /// Find inlinee line numbers covering an absolute virtual address range.
  ///
  /// \param VA Starting absolute virtual address.
  /// \param Length Length in bytes of the address range.
  ///
  /// \returns An enumerator over matching inlinee line numbers.
  std::unique_ptr<IPDBEnumLineNumbers>
  findInlineeLinesByVA(uint64_t VA, uint32_t Length) const;

  /// Find inlinee line numbers covering a relative virtual address range.
  ///
  /// \param RVA Starting relative virtual address.
  /// \param Length Length in bytes of the address range.
  ///
  /// \returns An enumerator over matching inlinee line numbers.
  std::unique_ptr<IPDBEnumLineNumbers>
  findInlineeLinesByRVA(uint32_t RVA, uint32_t Length) const;

  /// Return the name of this symbol.
  ///
  /// \returns The symbol's name as a string.
  std::string getName() const;

  /// Return a const reference to the underlying raw symbol.
  ///
  /// \returns A const reference to the wrapped IPDBRawSymbol.
  const IPDBRawSymbol &getRawSymbol() const { return *RawSymbol; }

  /// Return a mutable reference to the underlying raw symbol.
  ///
  /// \returns A mutable reference to the wrapped IPDBRawSymbol.
  IPDBRawSymbol &getRawSymbol() { return *RawSymbol; }

  /// Return the PDB session that owns this symbol's context.
  ///
  /// \returns A const reference to the owning IPDBSession.
  const IPDBSession &getSession() const { return Session; }

  /// Collect and return child-symbol statistics for this symbol.
  ///
  /// \param Stats Map updated with per-tag child counts.
  ///
  /// \returns An enumerator over the child symbols that were counted.
  std::unique_ptr<IPDBEnumSymbols> getChildStats(TagStats &Stats) const;

protected:
  /// Look up a symbol by ID within this symbol's session.
  ///
  /// \param Id Symbol index ID to resolve.
  ///
  /// \returns The matching symbol, or nullptr if none exists.
  std::unique_ptr<PDBSymbol> getSymbolByIdHelper(uint32_t Id) const;

  /// Look up a symbol by ID and cast it to \c ConcreteType.
  ///
  /// \param Id Symbol index ID to resolve.
  ///
  /// \returns The symbol as \c ConcreteType, or nullptr if missing or mismatched.
  template <typename ConcreteType>
  std::unique_ptr<ConcreteType> getConcreteSymbolByIdHelper(uint32_t Id) const {
    return unique_dyn_cast_or_null<ConcreteType>(getSymbolByIdHelper(Id));
  }

  /// PDB session that provides the context for this symbol.
  const IPDBSession &Session;

  /// Owned underlying raw symbol, when this wrapper took ownership.
  std::unique_ptr<IPDBRawSymbol> OwnedRawSymbol;

  /// Non-owning pointer to the underlying raw symbol.
  IPDBRawSymbol *RawSymbol = nullptr;
};

} // namespace llvm
}

#endif
