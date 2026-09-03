//===- PDBSymbolTypePointer.h - pointer type info ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEPOINTER_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEPOINTER_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for a pointer or reference type.
///
/// Exposes the pointee type, pointer/reference kind, member-pointer flags,
/// size, and cv-qualifier accessors for pointer types in the PDB type system.
class LLVM_ABI PDBSymbolTypePointer : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB pointer type symbols.
  static const PDB_SymType Tag = PDB_SymType::PointerType;

  /// Return true if \p S is a PDB pointer type symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a PDB pointer type symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this pointer type symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Dump the right-hand side of this pointer type using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the right-hand side.
  void dumpRight(PDBSymDumper &Dumper) const override;

  /// Return true if this pointer type is const-qualified.
  ///
  /// \returns True if this pointer type is const-qualified.
  FORWARD_SYMBOL_METHOD(isConstType)
  /// Return the symbol id of this pointer type's class parent.
  ///
  /// \returns The symbol id of the class parent.
  decltype(auto) getClassParentId() const {
    return RawSymbol->getClassParentId();
  }
  /// Return this pointer type's class parent symbol.
  ///
  /// \returns The class parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getClassParent() const {
    uint32_t Id = getClassParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the length in bytes of this pointer type.
  ///
  /// \returns The length in bytes of this pointer type.
  FORWARD_SYMBOL_METHOD(getLength)
  /// Return the symbol id of this pointer type's lexical parent.
  ///
  /// \returns The symbol id of the lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this pointer type's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this type is an lvalue reference.
  ///
  /// \returns True if this type is an lvalue reference.
  FORWARD_SYMBOL_METHOD(isReference)
  /// Return true if this type is an rvalue reference.
  ///
  /// \returns True if this type is an rvalue reference.
  FORWARD_SYMBOL_METHOD(isRValueReference)
  /// Return true if this type is a pointer to a data member.
  ///
  /// \returns True if this type is a pointer to a data member.
  FORWARD_SYMBOL_METHOD(isPointerToDataMember)
  /// Return true if this type is a pointer to a member function.
  ///
  /// \returns True if this type is a pointer to a member function.
  FORWARD_SYMBOL_METHOD(isPointerToMemberFunction)
  /// Return the symbol id of this pointer's pointee type.
  ///
  /// \returns The symbol id of the pointee type.
  decltype(auto) getPointeeTypeId() const { return RawSymbol->getTypeId(); }
  /// Return this pointer's pointee type symbol.
  ///
  /// \returns The pointee type symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getPointeeType() const {
    uint32_t Id = getPointeeTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this pointer type is restricted.
  ///
  /// \returns True if this pointer type is restricted.
  FORWARD_SYMBOL_METHOD(isRestrictedType)
  /// Return true if this pointer type is unaligned.
  ///
  /// \returns True if this pointer type is unaligned.
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  /// Return true if this pointer type is volatile-qualified.
  ///
  /// \returns True if this pointer type is volatile-qualified.
  FORWARD_SYMBOL_METHOD(isVolatileType)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEPOINTER_H
