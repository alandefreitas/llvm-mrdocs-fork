//===- PDBSymbolTypeBuiltin.h - builtin type information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEBUILTIN_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEBUILTIN_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a builtin (primitive) type.
///
/// Exposes the builtin type kind, size, and const/volatile/unaligned
/// qualifiers for a simple type in the PDB type hierarchy.
class LLVM_ABI PDBSymbolTypeBuiltin : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// The PDB symbol tag for builtin type symbols (`PDB_SymType::BuiltinType`).
  static const PDB_SymType Tag = PDB_SymType::BuiltinType;

  /// Return true if \p S is a builtin type symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a builtin type symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Destroy the builtin type symbol.
  ~PDBSymbolTypeBuiltin() override;

  /// Dump this builtin type symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the builtin type kind for this type symbol.
  ///
  /// \returns The builtin type kind for this type symbol.
  FORWARD_SYMBOL_METHOD(getBuiltinType)
  /// Return true if this type is const-qualified.
  ///
  /// \returns True if this type is const-qualified.
  FORWARD_SYMBOL_METHOD(isConstType)
  /// Return the length in bytes of this builtin type.
  ///
  /// \returns The length in bytes of this builtin type.
  FORWARD_SYMBOL_METHOD(getLength)
  /// Return the symbol id of this type's lexical parent.
  ///
  /// \returns The symbol id of the lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this type's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this type is unaligned.
  ///
  /// \returns True if this type is unaligned.
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  /// Return true if this type is volatile-qualified.
  ///
  /// \returns True if this type is volatile-qualified.
  FORWARD_SYMBOL_METHOD(isVolatileType)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEBUILTIN_H
