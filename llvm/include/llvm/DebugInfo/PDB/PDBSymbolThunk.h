//===- PDBSymbolThunk.h - Support for querying PDB thunks ---------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTHUNK_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTHUNK_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a thunk that redirects execution to another address.
///
/// Exposes target location, thunk ordinal, and related attributes for a
/// SymTagThunk entry in the PDB lexical hierarchy.
/// https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/thunk
class LLVM_ABI PDBSymbolThunk : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB thunk symbols.
  static const PDB_SymType Tag = PDB_SymType::Thunk;

  /// Return true if \p S is a PDB thunk symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a PDB thunk symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this thunk symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the access level of this thunk.
  ///
  /// \returns The access level of this thunk.
  FORWARD_SYMBOL_METHOD(getAccess)
  /// Return the section-relative address offset of this thunk.
  ///
  /// \returns The section-relative address offset of this thunk.
  FORWARD_SYMBOL_METHOD(getAddressOffset)
  /// Return the section index of this thunk's address.
  ///
  /// \returns The section index of this thunk's address.
  FORWARD_SYMBOL_METHOD(getAddressSection)
  /// Return the symbol id of this thunk's class parent.
  ///
  /// \returns The symbol id of this thunk's class parent.
  decltype(auto) getClassParentId() const {
    return RawSymbol->getClassParentId();
  }
  /// Return this thunk's class parent symbol.
  ///
  /// \returns The class parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getClassParent() const {
    uint32_t Id = getClassParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this thunk is const-qualified.
  ///
  /// \returns True if this thunk is const-qualified.
  FORWARD_SYMBOL_METHOD(isConstType)
  /// Return true if this thunk introduces a virtual function.
  ///
  /// \returns True if this thunk introduces a virtual function.
  FORWARD_SYMBOL_METHOD(isIntroVirtualFunction)
  /// Return true if this thunk is static.
  ///
  /// \returns True if this thunk is static.
  FORWARD_SYMBOL_METHOD(isStatic)
  /// Return the length in bytes of this thunk.
  ///
  /// \returns The length in bytes of this thunk.
  FORWARD_SYMBOL_METHOD(getLength)
  /// Return the symbol id of this thunk's lexical parent.
  ///
  /// \returns The symbol id of this thunk's lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this thunk's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the name of this thunk.
  ///
  /// \returns The name of this thunk.
  FORWARD_SYMBOL_METHOD(getName)
  /// Return true if this thunk is pure virtual.
  ///
  /// \returns True if this thunk is pure virtual.
  FORWARD_SYMBOL_METHOD(isPureVirtual)
  /// Return the relative virtual address of this thunk.
  ///
  /// \returns The relative virtual address of this thunk.
  FORWARD_SYMBOL_METHOD(getRelativeVirtualAddress)
  /// Return the target offset for this thunk.
  ///
  /// \returns The target offset for this thunk.
  FORWARD_SYMBOL_METHOD(getTargetOffset)
  /// Return the target relative virtual address for this thunk.
  ///
  /// \returns The target relative virtual address for this thunk.
  FORWARD_SYMBOL_METHOD(getTargetRelativeVirtualAddress)
  /// Return the target virtual address for this thunk.
  ///
  /// \returns The target virtual address for this thunk.
  FORWARD_SYMBOL_METHOD(getTargetVirtualAddress)
  /// Return the target section for this thunk.
  ///
  /// \returns The target section for this thunk.
  FORWARD_SYMBOL_METHOD(getTargetSection)
  /// Return the thunk ordinal for this thunk.
  ///
  /// \returns The thunk ordinal for this thunk.
  FORWARD_SYMBOL_METHOD(getThunkOrdinal)
  /// Return the symbol id of this thunk's type.
  ///
  /// \returns The symbol id of this thunk's type.
  decltype(auto) getTypeId() const { return RawSymbol->getTypeId(); }
  /// Return this thunk's type symbol.
  ///
  /// \returns The type symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getType() const {
    uint32_t Id = getTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this thunk is unaligned.
  ///
  /// \returns True if this thunk is unaligned.
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  /// Return true if this thunk is virtual.
  ///
  /// \returns True if this thunk is virtual.
  FORWARD_SYMBOL_METHOD(isVirtual)
  /// Return the virtual address of this thunk.
  ///
  /// \returns The virtual address of this thunk.
  FORWARD_SYMBOL_METHOD(getVirtualAddress)
  /// Return the virtual base offset for this thunk.
  ///
  /// \returns The virtual base offset for this thunk.
  FORWARD_SYMBOL_METHOD(getVirtualBaseOffset)
  /// Return true if this thunk is volatile-qualified.
  ///
  /// \returns True if this thunk is volatile-qualified.
  FORWARD_SYMBOL_METHOD(isVolatileType)
};
} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTHUNK_H
