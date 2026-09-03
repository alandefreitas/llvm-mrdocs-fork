//===- PDBSymbolBlock.h - Accessors for querying PDB blocks -------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLBLOCK_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLBLOCK_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a nested lexical code block within a function.
///
/// Block symbols identify nested scopes within functions in the PDB lexical
/// hierarchy.
/// https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/block
class LLVM_ABI PDBSymbolBlock : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// The PDB symbol tag for block symbols (`PDB_SymType::Block`).
  static const PDB_SymType Tag = PDB_SymType::Block;

  /// True if \p S is a block PDB symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S has the block symbol tag.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this block symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the section-relative address offset of this block.
  ///
  /// \returns The section-relative address offset.
  FORWARD_SYMBOL_METHOD(getAddressOffset)
  /// Return the section index of this block's address.
  ///
  /// \returns The section index of the block's address.
  FORWARD_SYMBOL_METHOD(getAddressSection)
  /// Return the length in bytes of this block.
  ///
  /// \returns The length of the block in bytes.
  FORWARD_SYMBOL_METHOD(getLength)
  /// Return the symbol id of this block's lexical parent.
  ///
  /// \returns The symbol id of the lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return the lexical parent of this block.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the location type of this block.
  ///
  /// \returns The location type of the block.
  FORWARD_SYMBOL_METHOD(getLocationType)
  /// Return the name of this block.
  ///
  /// \returns The name of the block.
  FORWARD_SYMBOL_METHOD(getName)
  /// Return the relative virtual address of this block.
  ///
  /// \returns The relative virtual address of the block.
  FORWARD_SYMBOL_METHOD(getRelativeVirtualAddress)
  /// Return the virtual address of this block.
  ///
  /// \returns The virtual address of the block.
  FORWARD_SYMBOL_METHOD(getVirtualAddress)
};
}
}

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLBLOCK_H
