//===- PDBSymbolTypeCustom.h - custom compiler type information -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPECUSTOM_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPECUSTOM_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a custom (vendor-defined) type.
///
/// Represents a SymTagCustomType entry: a compiler- or vendor-specific type
/// identified by OEM identifiers rather than a standard PDB type kind.
class LLVM_ABI PDBSymbolTypeCustom : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// The PDB symbol tag for custom types (`PDB_SymType::CustomType`).
  static const PDB_SymType Tag = PDB_SymType::CustomType;

  /// True if \p S is a custom-type PDB symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a custom-type PDB symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this custom type symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the OEM id associated with this custom type.
  ///
  /// \returns The OEM id associated with this custom type.
  FORWARD_SYMBOL_METHOD(getOemId)
  /// Return the OEM symbol id associated with this custom type.
  ///
  /// \returns The OEM symbol id associated with this custom type.
  FORWARD_SYMBOL_METHOD(getOemSymbolId)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPECUSTOM_H
