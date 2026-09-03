//===- PDBSymbolTypeManaged.h - managed type info ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEMANAGED_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEMANAGED_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a managed (CLR) type.
///
/// Represents a managed type entry in the PDB type hierarchy and exposes its
/// name and dump support.
class LLVM_ABI PDBSymbolTypeManaged : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// The PDB symbol tag for managed type symbols (`PDB_SymType::ManagedType`).
  static const PDB_SymType Tag = PDB_SymType::ManagedType;

  /// Return true if \p S is a managed type symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a managed type symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this managed type symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the name of this managed type.
  ///
  /// \returns The name of this managed type.
  FORWARD_SYMBOL_METHOD(getName)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEMANAGED_H
