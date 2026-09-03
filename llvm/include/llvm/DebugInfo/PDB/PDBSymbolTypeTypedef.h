//===- PDBSymbolTypeTypedef.h - typedef type info ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPETYPEDEF_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPETYPEDEF_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a typedef (type alias).
///
/// Exposes the typedef name, underlying type, size, and related UDT-style
/// attributes for a SymTagTypedef entry in the PDB type hierarchy.
class LLVM_ABI PDBSymbolTypeTypedef : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB typedef symbols.
  static const PDB_SymType Tag = PDB_SymType::Typedef;

  /// Return true if \p S is a PDB typedef symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a PDB typedef symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this typedef symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the builtin type kind for this typedef, if applicable.
  ///
  /// \returns The builtin type kind for this typedef, if applicable.
  FORWARD_SYMBOL_METHOD(getBuiltinType)
  /// Return the symbol id of this typedef's class parent.
  ///
  /// \returns The symbol id of this typedef's class parent.
  decltype(auto) getClassParentId() const {
    return RawSymbol->getClassParentId();
  }
  /// Return this typedef's class parent symbol.
  ///
  /// \returns The class parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getClassParent() const {
    uint32_t Id = getClassParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if the underlying type has a constructor.
  ///
  /// \returns True if the underlying type has a constructor.
  FORWARD_SYMBOL_METHOD(hasConstructor)
  /// Return true if this typedef is const-qualified.
  ///
  /// \returns True if this typedef is const-qualified.
  FORWARD_SYMBOL_METHOD(isConstType)
  /// Return true if the underlying type has an assignment operator.
  ///
  /// \returns True if the underlying type has an assignment operator.
  FORWARD_SYMBOL_METHOD(hasAssignmentOperator)
  /// Return true if the underlying type has a cast operator.
  ///
  /// \returns True if the underlying type has a cast operator.
  FORWARD_SYMBOL_METHOD(hasCastOperator)
  /// Return true if the underlying type has nested types.
  ///
  /// \returns True if the underlying type has nested types.
  FORWARD_SYMBOL_METHOD(hasNestedTypes)
  /// Return the length in bytes of this typedef's underlying type.
  ///
  /// \returns The length in bytes of this typedef's underlying type.
  FORWARD_SYMBOL_METHOD(getLength)
  /// Return the symbol id of this typedef's lexical parent.
  ///
  /// \returns The symbol id of this typedef's lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this typedef's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the name of this typedef.
  ///
  /// \returns The name of this typedef.
  FORWARD_SYMBOL_METHOD(getName)
  /// Return true if this typedef is nested within another type.
  ///
  /// \returns True if this typedef is nested within another type.
  FORWARD_SYMBOL_METHOD(isNested)
  /// Return true if the underlying type has an overloaded operator.
  ///
  /// \returns True if the underlying type has an overloaded operator.
  FORWARD_SYMBOL_METHOD(hasOverloadedOperator)
  /// Return true if the underlying type is packed.
  ///
  /// \returns True if the underlying type is packed.
  FORWARD_SYMBOL_METHOD(isPacked)
  /// Return true if this typedef refers to a reference type.
  ///
  /// \returns True if this typedef refers to a reference type.
  FORWARD_SYMBOL_METHOD(isReference)
  /// Return true if this typedef is scoped.
  ///
  /// \returns True if this typedef is scoped.
  FORWARD_SYMBOL_METHOD(isScoped)
  /// Return the symbol id of this typedef's underlying type.
  ///
  /// \returns The symbol id of this typedef's underlying type.
  decltype(auto) getTypeId() const { return RawSymbol->getTypeId(); }
  /// Return this typedef's underlying type symbol.
  ///
  /// \returns The underlying type symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getType() const {
    uint32_t Id = getTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the UDT kind of the underlying type, if it is a UDT.
  ///
  /// \returns The UDT kind of the underlying type, if it is a UDT.
  FORWARD_SYMBOL_METHOD(getUdtKind)
  /// Return true if this typedef is unaligned.
  ///
  /// \returns True if this typedef is unaligned.
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  /// Return the symbol id of the underlying type's virtual table shape.
  ///
  /// \returns The symbol id of the underlying type's virtual table shape.
  decltype(auto) getVirtualTableShapeId() const {
    return RawSymbol->getVirtualTableShapeId();
  }
  /// Return the underlying type's virtual table shape symbol.
  ///
  /// \returns The virtual table shape symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getVirtualTableShape() const {
    uint32_t Id = getVirtualTableShapeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this typedef is volatile-qualified.
  ///
  /// \returns True if this typedef is volatile-qualified.
  FORWARD_SYMBOL_METHOD(isVolatileType)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPETYPEDEF_H
