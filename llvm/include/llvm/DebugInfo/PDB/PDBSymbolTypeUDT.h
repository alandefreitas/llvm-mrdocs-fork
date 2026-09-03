//===- PDBSymbolTypeUDT.h - UDT type info -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEUDT_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEUDT_H

#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/Support/Compiler.h"

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

class PDBSymDumper;

/// PDB symbol for a user-defined type (class, struct, union, or interface).
///
/// Exposes UDT kind, size, nesting and packing flags, operator/constructor
/// presence, cv-qualifiers, and related type and parent accessors for UDT
/// entries in the PDB type hierarchy.
class LLVM_ABI PDBSymbolTypeUDT : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB UDT type symbols.
  static const PDB_SymType Tag = PDB_SymType::UDT;

  /// Return true if \p S is a PDB UDT type symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a PDB UDT type symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this UDT type symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the symbol id of this UDT's class parent.
  ///
  /// \returns The symbol id of this UDT's class parent.
  decltype(auto) getClassParentId() const {
    return RawSymbol->getClassParentId();
  }
  /// Return this UDT's class parent symbol.
  ///
  /// \returns The class parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getClassParent() const {
    uint32_t Id = getClassParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the symbol id of this UDT's unmodified type.
  ///
  /// \returns The symbol id of this UDT's unmodified type.
  decltype(auto) getUnmodifiedTypeId() const {
    return RawSymbol->getUnmodifiedTypeId();
  }
  /// Return this UDT's unmodified type symbol.
  ///
  /// \returns The unmodified type symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getUnmodifiedType() const {
    uint32_t Id = getUnmodifiedTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this UDT has a constructor.
  ///
  /// \returns True if this UDT has a constructor.
  FORWARD_SYMBOL_METHOD(hasConstructor)
  /// Return true if this UDT type is const-qualified.
  ///
  /// \returns True if this UDT type is const-qualified.
  FORWARD_SYMBOL_METHOD(isConstType)
  /// Return true if this UDT has an assignment operator.
  ///
  /// \returns True if this UDT has an assignment operator.
  FORWARD_SYMBOL_METHOD(hasAssignmentOperator)
  /// Return true if this UDT has a cast operator.
  ///
  /// \returns True if this UDT has a cast operator.
  FORWARD_SYMBOL_METHOD(hasCastOperator)
  /// Return true if this UDT has nested types.
  ///
  /// \returns True if this UDT has nested types.
  FORWARD_SYMBOL_METHOD(hasNestedTypes)
  /// Return the length in bytes of this UDT.
  ///
  /// \returns The length in bytes of this UDT.
  FORWARD_SYMBOL_METHOD(getLength)
  /// Return the symbol id of this UDT's lexical parent.
  ///
  /// \returns The symbol id of this UDT's lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this UDT's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the name of this UDT.
  ///
  /// \returns The name of this UDT.
  FORWARD_SYMBOL_METHOD(getName)
  /// Return the source line of this UDT's type definition.
  ///
  /// \returns The source line of this UDT's type definition.
  FORWARD_SYMBOL_METHOD(getSrcLineOnTypeDefn)
  /// Return true if this UDT is nested within another type.
  ///
  /// \returns True if this UDT is nested within another type.
  FORWARD_SYMBOL_METHOD(isNested)
  /// Return true if this UDT has an overloaded operator.
  ///
  /// \returns True if this UDT has an overloaded operator.
  FORWARD_SYMBOL_METHOD(hasOverloadedOperator)
  /// Return true if this UDT is packed.
  ///
  /// \returns True if this UDT is packed.
  FORWARD_SYMBOL_METHOD(isPacked)
  /// Return true if this UDT is scoped.
  ///
  /// \returns True if this UDT is scoped.
  FORWARD_SYMBOL_METHOD(isScoped)
  /// Return the UDT kind (class, struct, union, or interface).
  ///
  /// \returns The UDT kind (class, struct, union, or interface).
  FORWARD_SYMBOL_METHOD(getUdtKind)
  /// Return true if this UDT type is unaligned.
  ///
  /// \returns True if this UDT type is unaligned.
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  /// Return the symbol id of this UDT's virtual table shape.
  ///
  /// \returns The symbol id of this UDT's virtual table shape.
  decltype(auto) getVirtualTableShapeId() const {
    return RawSymbol->getVirtualTableShapeId();
  }
  /// Return this UDT's virtual table shape symbol.
  ///
  /// \returns The virtual table shape symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getVirtualTableShape() const {
    uint32_t Id = getVirtualTableShapeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this UDT type is volatile-qualified.
  ///
  /// \returns True if this UDT type is volatile-qualified.
  FORWARD_SYMBOL_METHOD(isVolatileType)
  /// Return the access level of this UDT.
  ///
  /// \returns The access level of this UDT.
  FORWARD_SYMBOL_METHOD(getAccess)
};
}
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEUDT_H
