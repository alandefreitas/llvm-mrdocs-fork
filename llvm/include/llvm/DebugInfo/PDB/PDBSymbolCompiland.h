//===- PDBSymbolCompiland.h - Accessors for querying PDB compilands -----*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILAND_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILAND_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {

class raw_ostream;

namespace pdb {

/// PDB symbol for a Compiland (compilation unit / module).
///
/// Exposes module name, object-file (library) name, Edit and Continue status,
/// lexical parent accessors, and helpers that resolve the primary source file
/// path for this Compiland in the PDB lexical hierarchy.
class LLVM_ABI PDBSymbolCompiland : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB Compiland symbols.
  static const PDB_SymType Tag = PDB_SymType::Compiland;

  /// Return true if \p S is a PDB Compiland symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a Compiland symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this Compiland symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return true if Edit and Continue is enabled for this Compiland.
  ///
  /// \returns True if Edit and Continue is enabled.
  FORWARD_SYMBOL_METHOD(isEditAndContinueEnabled)
  /// Return the symbol id of this Compiland's lexical parent.
  ///
  /// \returns The lexical parent symbol id.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this Compiland's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the object-file (library) name for this Compiland.
  ///
  /// \returns The object-file or library name.
  FORWARD_SYMBOL_METHOD(getLibraryName)
  /// Return the module name of this Compiland.
  ///
  /// \returns The Compiland module name.
  FORWARD_SYMBOL_METHOD(getName)

  /// Return the basename of this Compiland's primary source file.
  ///
  /// \returns The primary source file basename.
  std::string getSourceFileName() const;
  /// Return the full path of this Compiland's primary source file.
  ///
  /// Resolves the recorded source name against CompilandEnv, the session
  /// source-file list, or CompilandDetails language heuristics when needed.
  ///
  /// \returns The full path of the primary source file.
  std::string getSourceFileFullPath() const;
};
}
}

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILAND_H
