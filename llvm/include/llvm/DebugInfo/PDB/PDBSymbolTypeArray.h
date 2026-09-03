//===- PDBSymbolTypeArray.h - array type information ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEARRAY_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEARRAY_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for an array type.
///
/// Exposes element type, index type, count, length, rank, and cv-qualifier
/// accessors for array types in the PDB type system.
class LLVM_ABI PDBSymbolTypeArray : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB array type symbols.
  static const PDB_SymType Tag = PDB_SymType::ArrayType;

  /// Return true if \p S is a PDB array type symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a PDB array type symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this array type symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Dump the right-hand side of this array type using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the right-hand side.
  void dumpRight(PDBSymDumper &Dumper) const override;

  /// Return the symbol id of this array's index type.
  ///
  /// \returns The symbol id of the array index type.
  decltype(auto) getArrayIndexTypeId() const {
    return RawSymbol->getArrayIndexTypeId();
  }
  /// Return this array's index type symbol.
  ///
  /// \returns The array index type symbol.
  std::unique_ptr<PDBSymbol> getArrayIndexType() const {
    uint32_t Id = getArrayIndexTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this array type is const-qualified.
  ///
  /// \returns True if this array type is const-qualified.
  FORWARD_SYMBOL_METHOD(isConstType)
  /// Return the number of elements in this array.
  ///
  /// \returns The number of elements in this array.
  FORWARD_SYMBOL_METHOD(getCount)
  /// Return the length in bytes of this array type.
  ///
  /// \returns The length in bytes of this array type.
  FORWARD_SYMBOL_METHOD(getLength)
  /// Return the symbol id of this array type's lexical parent.
  ///
  /// \returns The symbol id of the lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this array type's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the rank of this multi-dimensional array type.
  ///
  /// \returns The rank of this multi-dimensional array type.
  FORWARD_SYMBOL_METHOD(getRank)
  /// Return the symbol id of this array's element type.
  ///
  /// \returns The symbol id of the array element type.
  decltype(auto) getElementTypeId() const {
    return RawSymbol->getTypeId();
  }
  /// Return this array's element type symbol.
  ///
  /// \returns The array element type symbol.
  std::unique_ptr<PDBSymbol> getElementType() const {
    uint32_t Id = getElementTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this array type is unaligned.
  ///
  /// \returns True if this array type is unaligned.
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  /// Return true if this array type is volatile-qualified.
  ///
  /// \returns True if this array type is volatile-qualified.
  FORWARD_SYMBOL_METHOD(isVolatileType)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEARRAY_H
