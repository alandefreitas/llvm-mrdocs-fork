//===- PDBSymbolTypeDimension.h - array dimension type info -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEDIMENSION_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEDIMENSION_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

/// PDB symbol for an array or matrix dimension.
///
/// Exposes lower- and upper-bound symbol ids for a SymTagDimension symbol,
/// typically used with FORTRAN multi-dimensional array types.
class LLVM_ABI PDBSymbolTypeDimension : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// The PDB symbol tag for dimension symbols (`PDB_SymType::Dimension`).
  static const PDB_SymType Tag = PDB_SymType::Dimension;

  /// True if \p S is a dimension PDB symbol.
  ///
  /// \param S Symbol to test.
  ///
  /// \returns True if \p S is a dimension PDB symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this dimension symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the symbol id of this dimension's lower bound.
  ///
  /// \returns The symbol id of this dimension's lower bound.
  FORWARD_SYMBOL_METHOD(getLowerBoundId)
  /// Return the symbol id of this dimension's upper bound.
  ///
  /// \returns The symbol id of this dimension's upper bound.
  FORWARD_SYMBOL_METHOD(getUpperBoundId)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEDIMENSION_H
