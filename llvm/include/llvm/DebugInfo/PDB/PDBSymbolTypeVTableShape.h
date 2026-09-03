//===- PDBSymbolTypeVTableShape.h - VTable shape info -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEVTABLESHAPE_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEVTABLESHAPE_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a vtable shape type.
///
/// Describes the layout of a virtual function table, including the number of
/// entries and const/volatile/unaligned qualifiers.
class LLVM_ABI PDBSymbolTypeVTableShape : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB vtable shape symbols.
  static const PDB_SymType Tag = PDB_SymType::VTableShape;

  /// Return true if \p S is a PDB vtable shape symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a PDB vtable shape symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this vtable shape symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return true if this vtable shape type is const-qualified.
  ///
  /// \returns True if this vtable shape type is const-qualified.
  FORWARD_SYMBOL_METHOD(isConstType)
  /// Return the number of entries in this vtable shape.
  ///
  /// \returns The number of entries in this vtable shape.
  FORWARD_SYMBOL_METHOD(getCount)
  /// Return the symbol id of this vtable shape's lexical parent.
  ///
  /// \returns The symbol id of the lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this vtable shape's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this vtable shape type is unaligned.
  ///
  /// \returns True if this vtable shape type is unaligned.
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  /// Return true if this vtable shape type is volatile-qualified.
  ///
  /// \returns True if this vtable shape type is volatile-qualified.
  FORWARD_SYMBOL_METHOD(isVolatileType)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEVTABLESHAPE_H
