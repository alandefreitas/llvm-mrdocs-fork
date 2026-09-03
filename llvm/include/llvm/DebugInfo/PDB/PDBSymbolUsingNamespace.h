//===- PDBSymbolUsingNamespace.h - using namespace info ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLUSINGNAMESPACE_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLUSINGNAMESPACE_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a using-namespace directive.
///
/// Exposes the namespace name and lexical-parent accessors for a
/// SymTagUsingNamespace entry in the PDB lexical hierarchy.
class LLVM_ABI PDBSymbolUsingNamespace : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// The PDB symbol tag for using-namespace symbols
  /// (`PDB_SymType::UsingNamespace`).
  static const PDB_SymType Tag = PDB_SymType::UsingNamespace;

  /// True if \p S is a using-namespace PDB symbol.
  ///
  /// \param S Symbol to test.
  ///
  /// \returns True if \p S is a using-namespace PDB symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this using-namespace symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the symbol id of this directive's lexical parent.
  ///
  /// \returns The symbol id of this directive's lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this directive's lexical parent symbol.
  ///
  /// \returns This directive's lexical parent symbol.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the name of the namespace introduced by this directive.
  ///
  /// \returns The name of the namespace introduced by this directive.
  FORWARD_SYMBOL_METHOD(getName)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLUSINGNAMESPACE_H
