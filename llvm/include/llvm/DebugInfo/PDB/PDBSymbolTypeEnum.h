//===- PDBSymbolTypeEnum.h - enum type info ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEENUM_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEENUM_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/Support/Compiler.h"

#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"

namespace llvm {

namespace pdb {

class PDBSymDumper;
class PDBSymbolTypeBuiltin;

/// PDB symbol for an enumeration type.
///
/// Exposes the enum name, underlying type, size, nesting/packing/scoping
/// attributes, and const/volatile/unaligned qualifiers for a SymTagEnum
/// entry in the PDB type hierarchy.
/// https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/enum
class LLVM_ABI PDBSymbolTypeEnum : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// The PDB symbol tag for enum type symbols (`PDB_SymType::Enum`).
  static const PDB_SymType Tag = PDB_SymType::Enum;

  /// Return true if \p S is an enum type symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is an enum type symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this enum type symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the builtin type kind of this enum's underlying type.
  ///
  /// \returns The builtin type kind of the underlying type.
  FORWARD_SYMBOL_METHOD(getBuiltinType)
  /// Return the symbol id of this enum's class parent.
  ///
  /// \returns The class parent symbol id.
  decltype(auto) getClassParentId() const {
    return RawSymbol->getClassParentId();
  }
  /// Return this enum's class parent symbol.
  ///
  /// \returns The class parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getClassParent() const {
    uint32_t Id = getClassParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this enum declares a constructor or destructor.
  ///
  /// \returns True if this enum declares a constructor or destructor.
  FORWARD_SYMBOL_METHOD(hasConstructor)
  /// Return true if this enum type is const-qualified.
  ///
  /// \returns True if this enum type is const-qualified.
  FORWARD_SYMBOL_METHOD(isConstType)
  /// Return true if this enum has an overloaded assignment operator.
  ///
  /// \returns True if this enum has an overloaded assignment operator.
  FORWARD_SYMBOL_METHOD(hasAssignmentOperator)
  /// Return true if this enum has a conversion (cast) operator.
  ///
  /// \returns True if this enum has a conversion (cast) operator.
  FORWARD_SYMBOL_METHOD(hasCastOperator)
  /// Return true if this enum contains nested types.
  ///
  /// \returns True if this enum contains nested types.
  FORWARD_SYMBOL_METHOD(hasNestedTypes)
  /// Return the length in bytes of this enum's underlying type.
  ///
  /// \returns The length in bytes of the underlying type.
  FORWARD_SYMBOL_METHOD(getLength)
  /// Return the symbol id of this enum's lexical parent.
  ///
  /// \returns The lexical parent symbol id.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this enum's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the symbol id of the unmodified enum type.
  ///
  /// \returns The unmodified type symbol id.
  decltype(auto) getUnmodifiedTypeId() const {
    return RawSymbol->getUnmodifiedTypeId();
  }
  /// Return the unmodified form of this enum type.
  ///
  /// \returns The unmodified type symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getUnmodifiedType() const {
    uint32_t Id = getUnmodifiedTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the name of this enum type.
  ///
  /// \returns The name of this enum type.
  FORWARD_SYMBOL_METHOD(getName)
  /// Return the source line of this enum type's definition.
  ///
  /// \returns The source line of this enum type's definition.
  FORWARD_SYMBOL_METHOD(getSrcLineOnTypeDefn)
  /// Return true if this enum is nested inside another type.
  ///
  /// \returns True if this enum is nested inside another type.
  FORWARD_SYMBOL_METHOD(isNested)
  /// Return true if this enum has overloaded operators.
  ///
  /// \returns True if this enum has overloaded operators.
  FORWARD_SYMBOL_METHOD(hasOverloadedOperator)
  /// Return true if this enum is packed.
  ///
  /// \returns True if this enum is packed.
  FORWARD_SYMBOL_METHOD(isPacked)
  /// Return true if this enum is a C++ scoped enumeration.
  ///
  /// \returns True if this enum is a C++ scoped enumeration.
  FORWARD_SYMBOL_METHOD(isScoped)
  /// Return the symbol id of this enum's underlying type.
  ///
  /// \returns The underlying type symbol id.
  decltype(auto) getUnderlyingTypeId() const {
    return RawSymbol->getTypeId();
  }
  /// Return this enum's underlying builtin type.
  ///
  /// \returns The underlying type symbol, or nullptr if none.
  std::unique_ptr<PDBSymbolTypeBuiltin> getUnderlyingType() const {
    uint32_t Id = getUnderlyingTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbolTypeBuiltin>(Id);
  }
  /// Return true if this enum type is unaligned.
  ///
  /// \returns True if this enum type is unaligned.
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  /// Return true if this enum type is volatile-qualified.
  ///
  /// \returns True if this enum type is volatile-qualified.
  FORWARD_SYMBOL_METHOD(isVolatileType)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEENUM_H
