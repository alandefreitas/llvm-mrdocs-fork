//===- PDBSymbolTypeVTable.h - VTable type info -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEVTABLE_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEVTABLE_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a class virtual function table (vtable).
///
/// Exposes the owning class, vtable offset, pointed-to type, and
/// const/volatile/unaligned qualifiers for a SymTagVTable entry.
class LLVM_ABI PDBSymbolTypeVTable : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB vtable type symbols.
  static const PDB_SymType Tag = PDB_SymType::VTable;

  /// Return true if \p S is a PDB vtable type symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a PDB vtable type symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this vtable type symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the symbol id of this vtable's class parent.
  ///
  /// \returns The symbol id of the class parent.
  decltype(auto) getClassParentId() const {
    return RawSymbol->getClassParentId();
  }
  /// Return this vtable's class parent symbol.
  ///
  /// \returns The class parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getClassParent() const {
    uint32_t Id = getClassParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the byte offset of this vtable within its class.
  ///
  /// \returns The byte offset of this vtable within its class.
  FORWARD_SYMBOL_METHOD(getOffset)
  /// Return true if this vtable type is const-qualified.
  ///
  /// \returns True if this vtable type is const-qualified.
  FORWARD_SYMBOL_METHOD(isConstType)
  /// Return the symbol id of this vtable's lexical parent.
  ///
  /// \returns The symbol id of the lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this vtable's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the symbol id of this vtable's type.
  ///
  /// \returns The symbol id of this vtable's type.
  decltype(auto) getTypeId() const { return RawSymbol->getTypeId(); }
  /// Return this vtable's type symbol.
  ///
  /// \returns The type symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getType() const {
    uint32_t Id = getTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this vtable type is unaligned.
  ///
  /// \returns True if this vtable type is unaligned.
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  /// Return true if this vtable type is volatile-qualified.
  ///
  /// \returns True if this vtable type is volatile-qualified.
  FORWARD_SYMBOL_METHOD(isVolatileType)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEVTABLE_H
