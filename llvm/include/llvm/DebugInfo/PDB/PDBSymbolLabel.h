//===- PDBSymbolLabel.h - label info ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLLABEL_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLLABEL_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a code label within a function or compiland.
///
/// Label symbols identify named addresses in the PDB lexical hierarchy.
/// https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/label
class LLVM_ABI PDBSymbolLabel : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// The PDB symbol tag for label symbols (`PDB_SymType::Label`).
  static const PDB_SymType Tag = PDB_SymType::Label;

  /// True if \p S is a label PDB symbol.
  ///
  /// \param S Symbol to test.
  ///
  /// \returns True if \p S is a label PDB symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this label symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the section-relative address offset of this label.
  ///
  /// \returns The section-relative address offset of this label.
  FORWARD_SYMBOL_METHOD(getAddressOffset)
  /// Return the section index of this label's address.
  ///
  /// \returns The section index of this label's address.
  FORWARD_SYMBOL_METHOD(getAddressSection)
  /// Return true if this label uses a custom calling convention.
  ///
  /// \returns True if this label uses a custom calling convention.
  FORWARD_SYMBOL_METHOD(hasCustomCallingConvention)
  /// Return true if this label uses a far return.
  ///
  /// \returns True if this label uses a far return.
  FORWARD_SYMBOL_METHOD(hasFarReturn)
  /// Return true if this label uses an interrupt return.
  ///
  /// \returns True if this label uses an interrupt return.
  FORWARD_SYMBOL_METHOD(hasInterruptReturn)
  /// Return the symbol id of this label's lexical parent.
  ///
  /// \returns The lexical parent symbol id.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return the lexical parent of this label.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the location type of this label.
  ///
  /// \returns The location type of this label.
  FORWARD_SYMBOL_METHOD(getLocationType)
  /// Return the name of this label.
  ///
  /// \returns The name of this label.
  FORWARD_SYMBOL_METHOD(getName)
  /// Return true if this label has the noinline attribute.
  ///
  /// \returns True if this label has the noinline attribute.
  FORWARD_SYMBOL_METHOD(hasNoInlineAttribute)
  /// Return true if this label has the noreturn attribute.
  ///
  /// \returns True if this label has the noreturn attribute.
  FORWARD_SYMBOL_METHOD(hasNoReturnAttribute)
  /// Return true if this label's code is unreachable.
  ///
  /// \returns True if this label's code is unreachable.
  FORWARD_SYMBOL_METHOD(isUnreached)
  /// Return the offset associated with this label.
  ///
  /// \returns The offset associated with this label.
  FORWARD_SYMBOL_METHOD(getOffset)
  /// Return true if this label has optimized-code debug info.
  ///
  /// \returns True if this label has optimized-code debug info.
  FORWARD_SYMBOL_METHOD(hasOptimizedCodeDebugInfo)
  /// Return the relative virtual address of this label.
  ///
  /// \returns The relative virtual address of this label.
  FORWARD_SYMBOL_METHOD(getRelativeVirtualAddress)
  /// Return the virtual address of this label.
  ///
  /// \returns The virtual address of this label.
  FORWARD_SYMBOL_METHOD(getVirtualAddress)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLLABEL_H
