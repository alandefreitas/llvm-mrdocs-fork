//===- PDBSymbolTypeBaseClass.h - base class type information ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEBASECLASS_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEBASECLASS_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"

namespace llvm {

namespace pdb {

class PDBSymDumper;

/// PDB symbol for a base class relationship in a UDT.
///
/// Exposes access, offset, virtual-base layout, and UDT property accessors
/// for a SymTagBaseClass entry in the PDB type system.
class LLVM_ABI PDBSymbolTypeBaseClass : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB base class symbols.
  static const PDB_SymType Tag = PDB_SymType::BaseClass;

  /// Return true if \p S is a PDB base class symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a PDB base class symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this base class symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the access level of this base class.
  ///
  /// \returns The access level of this base class.
  FORWARD_SYMBOL_METHOD(getAccess)
  /// Return the symbol id of this base class's class parent.
  ///
  /// \returns The symbol id of the class parent.
  decltype(auto) getClassParentId() const {
    return RawSymbol->getClassParentId();
  }
  /// Return the class parent of this base class.
  ///
  /// \returns The class parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getClassParent() const {
    uint32_t Id = getClassParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this base class UDT has a constructor.
  ///
  /// \returns True if this base class UDT has a constructor.
  FORWARD_SYMBOL_METHOD(hasConstructor)
  /// Return true if this base class type is const-qualified.
  ///
  /// \returns True if this base class type is const-qualified.
  FORWARD_SYMBOL_METHOD(isConstType)
  /// Return true if this base class UDT has an assignment operator.
  ///
  /// \returns True if this base class UDT has an assignment operator.
  FORWARD_SYMBOL_METHOD(hasAssignmentOperator)
  /// Return true if this base class UDT has a cast operator.
  ///
  /// \returns True if this base class UDT has a cast operator.
  FORWARD_SYMBOL_METHOD(hasCastOperator)
  /// Return true if this base class UDT has nested types.
  ///
  /// \returns True if this base class UDT has nested types.
  FORWARD_SYMBOL_METHOD(hasNestedTypes)
  /// Return true if this is an indirect virtual base class.
  ///
  /// \returns True if this is an indirect virtual base class.
  FORWARD_SYMBOL_METHOD(isIndirectVirtualBaseClass)
  /// Return the length in bytes of this base class type.
  ///
  /// \returns The length in bytes of this base class type.
  FORWARD_SYMBOL_METHOD(getLength)
  /// Return the symbol id of this base class's lexical parent.
  ///
  /// \returns The symbol id of the lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return the lexical parent of this base class.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the name of this base class.
  ///
  /// \returns The name of this base class.
  FORWARD_SYMBOL_METHOD(getName)
  /// Return true if this base class is nested within another type.
  ///
  /// \returns True if this base class is nested within another type.
  FORWARD_SYMBOL_METHOD(isNested)
  /// Return the offset of this base class within its derived UDT.
  ///
  /// \returns The offset of this base class within its derived UDT.
  FORWARD_SYMBOL_METHOD(getOffset)
  /// Return true if this base class UDT has an overloaded operator.
  ///
  /// \returns True if this base class UDT has an overloaded operator.
  FORWARD_SYMBOL_METHOD(hasOverloadedOperator)
  /// Return true if this base class UDT is packed.
  ///
  /// \returns True if this base class UDT is packed.
  FORWARD_SYMBOL_METHOD(isPacked)
  /// Return true if this base class is scoped.
  ///
  /// \returns True if this base class is scoped.
  FORWARD_SYMBOL_METHOD(isScoped)
  /// Return the symbol id of this base class's type.
  ///
  /// \returns The symbol id of this base class's type.
  decltype(auto) getTypeId() const { return RawSymbol->getTypeId(); }
  /// Return the type symbol for this base class.
  ///
  /// \returns The type symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getType() const {
    uint32_t Id = getTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the UDT kind of this base class.
  ///
  /// \returns The UDT kind of this base class.
  FORWARD_SYMBOL_METHOD(getUdtKind)
  /// Return true if this base class type is unaligned.
  ///
  /// \returns True if this base class type is unaligned.
  FORWARD_SYMBOL_METHOD(isUnalignedType)

  /// Return true if this is a virtual base class.
  ///
  /// \returns True if this is a virtual base class.
  FORWARD_SYMBOL_METHOD(isVirtualBaseClass)
  /// Return the virtual base displacement index for this base class.
  ///
  /// \returns The virtual base displacement index for this base class.
  FORWARD_SYMBOL_METHOD(getVirtualBaseDispIndex)
  /// Return the virtual base pointer offset for this base class.
  ///
  /// \returns The virtual base pointer offset for this base class.
  FORWARD_SYMBOL_METHOD(getVirtualBasePointerOffset)
  // FORWARD_SYMBOL_METHOD(getVirtualBaseTableType)
  /// Return the symbol id of this base class's virtual table shape.
  ///
  /// \returns The symbol id of this base class's virtual table shape.
  decltype(auto) getVirtualTableShapeId() const {
    return RawSymbol->getVirtualTableShapeId();
  }
  /// Return the virtual table shape of this base class.
  ///
  /// \returns The virtual table shape symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getVirtualTableShape() const {
    uint32_t Id = getVirtualTableShapeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this base class type is volatile-qualified.
  ///
  /// \returns True if this base class type is volatile-qualified.
  FORWARD_SYMBOL_METHOD(isVolatileType)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEBASECLASS_H
