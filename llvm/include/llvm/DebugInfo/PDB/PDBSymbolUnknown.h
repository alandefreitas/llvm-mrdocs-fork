//===- PDBSymbolUnknown.h - unknown symbol type -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLUNKNOWN_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLUNKNOWN_H

#include "PDBSymbol.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for an unrecognized or out-of-range symbol tag.
///
/// Used when a raw symbol's tag is `PDB_SymType::None` or greater than or
/// equal to `PDB_SymType::Max`, so no more specific concrete type applies.
class LLVM_ABI PDBSymbolUnknown : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// True if \p S has an unknown or unsupported PDB symbol tag.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S has an unknown or unsupported PDB symbol tag.
  static bool classof(const PDBSymbol *S) {
    return S->getSymTag() == PDB_SymType::None ||
           S->getSymTag() >= PDB_SymType::Max;
  }

  /// Dump this unknown symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLUNKNOWN_H
