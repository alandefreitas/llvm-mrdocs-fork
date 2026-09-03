//===- PDBSymbolTypeFunctionArg.h - function arg type info ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFUNCTIONARG_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFUNCTIONARG_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a function argument type.
///
/// Exposes class parent, lexical parent, and argument type accessors for
/// function argument types in the PDB type system.
class LLVM_ABI PDBSymbolTypeFunctionArg : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB function argument type symbols.
  static const PDB_SymType Tag = PDB_SymType::FunctionArg;

  /// Return true if \p S is a PDB function argument type symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S has the FunctionArg symbol tag.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this function argument type symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the symbol id of this argument type's class parent.
  ///
  /// \returns The class parent symbol id.
  decltype(auto) getClassParentId() const {
    return RawSymbol->getClassParentId();
  }
  /// Return this argument type's class parent symbol.
  ///
  /// \returns The class parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getClassParent() const {
    uint32_t Id = getClassParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the symbol id of this argument type's lexical parent.
  ///
  /// \returns The lexical parent symbol id.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this argument type's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the symbol id of this argument's type.
  ///
  /// \returns The argument type symbol id.
  decltype(auto) getTypeId() const { return RawSymbol->getTypeId(); }
  /// Return this argument's type symbol.
  ///
  /// \returns The argument type symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getType() const {
    uint32_t Id = getTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFUNCTIONARG_H
