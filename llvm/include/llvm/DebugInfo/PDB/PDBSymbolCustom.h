//===- PDBSymbolCustom.h - compiler-specific types --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLCUSTOM_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLCUSTOM_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

namespace pdb {
/// PDB symbol for compiler-specific data that fits nowhere else in the hierarchy.
///
/// PDBSymbolCustom represents symbols that are compiler-specific and do not
/// fit anywhere else in the lexical hierarchy.
/// https://msdn.microsoft.com/en-us/library/d88sf09h.aspx
class LLVM_ABI PDBSymbolCustom : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// The PDB symbol tag for custom symbols (`PDB_SymType::Custom`).
  static const PDB_SymType Tag = PDB_SymType::Custom;

  /// True if \p S is a custom PDB symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S has the custom symbol tag.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this custom symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Append this symbol's raw data bytes to \p bytes.
  ///
  /// \param bytes Vector that receives the data bytes.
  void getDataBytes(llvm::SmallVector<uint8_t, 32> &bytes);
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLCUSTOM_H
