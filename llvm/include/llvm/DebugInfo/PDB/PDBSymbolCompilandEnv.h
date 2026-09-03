//===- PDBSymbolCompilandEnv.h - compiland environment variables *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILANDENV_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILANDENV_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {
/// PDB symbol for a compiland environment variable (name/value pair).
///
/// Children of a Compiland that record compiler environment strings such as
/// the working directory or source path used when the module was built.
/// https://msdn.microsoft.com/en-us/library/60ff2zxd.aspx
class LLVM_ABI PDBSymbolCompilandEnv : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// The PDB symbol tag for CompilandEnv symbols (`PDB_SymType::CompilandEnv`).
  static const PDB_SymType Tag = PDB_SymType::CompilandEnv;

  /// True if \p S is a CompilandEnv PDB symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a CompilandEnv symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this CompilandEnv symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the symbol id of this environment entry's lexical parent.
  ///
  /// \returns The lexical parent symbol id.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this environment entry's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the name of this environment variable.
  ///
  /// \returns The environment variable name.
  FORWARD_SYMBOL_METHOD(getName)
  /// Return the string value of this environment variable.
  ///
  /// \returns The value as a string, or an empty string if the underlying
  ///     variant is not a string.
  std::string getValue() const;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILANDENV_H
