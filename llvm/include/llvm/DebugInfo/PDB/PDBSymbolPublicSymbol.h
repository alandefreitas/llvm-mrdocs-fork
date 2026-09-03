//===- PDBSymbolPublicSymbol.h - public symbol info -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLPUBLICSYMBOL_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLPUBLICSYMBOL_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a public (linker-visible) symbol entry.
///
/// Exposes name, address, and code-related accessors for an entry from the PDB
/// publics stream.
/// https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/publicsymbol
class LLVM_ABI PDBSymbolPublicSymbol : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB public symbols.
  static const PDB_SymType Tag = PDB_SymType::PublicSymbol;

  /// Return true if \p S is a PDB public symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S has the public symbol tag.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this public symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the section-relative address offset of this symbol.
  ///
  /// \returns The section-relative address offset.
  FORWARD_SYMBOL_METHOD(getAddressOffset)
  /// Return the section index of this symbol's address.
  ///
  /// \returns The section index of this symbol's address.
  FORWARD_SYMBOL_METHOD(getAddressSection)
  /// Return true if this symbol represents code.
  ///
  /// \returns True if this symbol represents code.
  FORWARD_SYMBOL_METHOD(isCode)
  /// Return true if this symbol is a function.
  ///
  /// \returns True if this symbol is a function.
  FORWARD_SYMBOL_METHOD(isFunction)
  /// Return the length in bytes of this symbol.
  ///
  /// \returns The length in bytes of this symbol.
  FORWARD_SYMBOL_METHOD(getLength)
  /// Return the symbol id of this symbol's lexical parent.
  ///
  /// \returns The symbol id of the lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this symbol's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the location type of this symbol.
  ///
  /// \returns The location type of this symbol.
  FORWARD_SYMBOL_METHOD(getLocationType)
  /// Return true if this symbol is managed code.
  ///
  /// \returns True if this symbol is managed code.
  FORWARD_SYMBOL_METHOD(isManagedCode)
  /// Return true if this symbol is MSIL code.
  ///
  /// \returns True if this symbol is MSIL code.
  FORWARD_SYMBOL_METHOD(isMSILCode)
  /// Return the name of this symbol.
  ///
  /// \returns The name of this symbol.
  FORWARD_SYMBOL_METHOD(getName)
  /// Return the relative virtual address of this symbol.
  ///
  /// \returns The relative virtual address of this symbol.
  FORWARD_SYMBOL_METHOD(getRelativeVirtualAddress)
  /// Return the virtual address of this symbol.
  ///
  /// \returns The virtual address of this symbol.
  FORWARD_SYMBOL_METHOD(getVirtualAddress)
  /// Return the undecorated (demangled) name of this symbol.
  ///
  /// \returns The undecorated (demangled) name of this symbol.
  FORWARD_SYMBOL_METHOD(getUndecoratedName)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLPUBLICSYMBOL_H
