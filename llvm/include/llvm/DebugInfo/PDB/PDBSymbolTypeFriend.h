//===- PDBSymbolTypeFriend.h - friend type info -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFRIEND_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFRIEND_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a C++ friend declaration.
///
/// Exposes the friend name, the class that grants friendship, and the friend
/// type for a SymTagFriend entry in the PDB type hierarchy.
class LLVM_ABI PDBSymbolTypeFriend : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB friend type symbols.
  static const PDB_SymType Tag = PDB_SymType::Friend;

  /// Return true if \p S is a PDB friend type symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a PDB friend type symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this friend type symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the symbol id of this friend's class parent.
  ///
  /// \returns The symbol id of this friend's class parent.
  decltype(auto) getClassParentId() const {
    return RawSymbol->getClassParentId();
  }
  /// Return this friend's class parent symbol.
  ///
  /// \returns The class parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getClassParent() const {
    uint32_t Id = getClassParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the name of this friend.
  ///
  /// \returns The name of this friend.
  FORWARD_SYMBOL_METHOD(getName)
  /// Return the symbol id of this friend's type.
  ///
  /// \returns The symbol id of this friend's type.
  decltype(auto) getTypeId() const { return RawSymbol->getTypeId(); }
  /// Return this friend's type symbol.
  ///
  /// \returns The type symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getType() const {
    uint32_t Id = getTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFRIEND_H
