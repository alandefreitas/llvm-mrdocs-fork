//===- PDBSymbolTypeFunctionSig.h - function signature type info *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFUNCTIONSIG_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFUNCTIONSIG_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class raw_ostream;
namespace pdb {

/// PDB symbol for a function signature type.
///
/// Exposes return type, argument list, calling convention, this-adjust, and
/// cv-qualifier accessors for function types in the PDB type system.
class LLVM_ABI PDBSymbolTypeFunctionSig : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying PDB function signature type symbols.
  static const PDB_SymType Tag = PDB_SymType::FunctionSig;

  /// Return true if \p S is a PDB function signature type symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a PDB function signature type symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Enumerate this function signature's argument type symbols.
  ///
  /// \returns An enumerator over argument type symbols.
  std::unique_ptr<IPDBEnumSymbols> getArguments() const;

  /// Dump this function signature type symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Dump the right-hand side of this function signature using the given
  /// dumper.
  ///
  /// \param Dumper Visitor used to format and emit the right-hand side.
  void dumpRight(PDBSymDumper &Dumper) const override;

  /// Dump this function signature's argument list to the given stream.
  ///
  /// \param OS Stream that receives the formatted argument list.
  void dumpArgList(raw_ostream &OS) const;

  /// Return true if this function signature is a C-style variadic function.
  ///
  /// \returns True if this function signature is a C-style variadic function.
  bool isCVarArgs() const;

  /// Return the calling convention of this function signature.
  ///
  /// \returns The calling convention of this function signature.
  FORWARD_SYMBOL_METHOD(getCallingConvention)
  /// Return the symbol id of this function signature's class parent.
  ///
  /// \returns The symbol id of this function signature's class parent.
  decltype(auto) getClassParentId() const {
    return RawSymbol->getClassParentId();
  }
  /// Return this function signature's class parent symbol.
  ///
  /// \returns The class parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getClassParent() const {
    uint32_t Id = getClassParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the symbol id of this function signature's unmodified type.
  ///
  /// \returns The symbol id of this function signature's unmodified type.
  decltype(auto) getUnmodifiedTypeId() const {
    return RawSymbol->getUnmodifiedTypeId();
  }
  /// Return this function signature's unmodified type symbol.
  ///
  /// \returns The unmodified type symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getUnmodifiedType() const {
    uint32_t Id = getUnmodifiedTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this function signature is const-qualified.
  ///
  /// \returns True if this function signature is const-qualified.
  FORWARD_SYMBOL_METHOD(isConstType)
  /// Return the number of parameters in this function signature.
  ///
  /// \returns The number of parameters in this function signature.
  FORWARD_SYMBOL_METHOD(getCount)
  /// Return the symbol id of this function signature's lexical parent.
  ///
  /// \returns The symbol id of this function signature's lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this function signature's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  // FORWARD_SYMBOL_METHOD(getObjectPointerType)
  /// Return the this-adjust value for this member function signature.
  ///
  /// \returns The this-adjust value for this member function signature.
  FORWARD_SYMBOL_METHOD(getThisAdjust)
  /// Return the symbol id of this function signature's return type.
  ///
  /// \returns The symbol id of this function signature's return type.
  decltype(auto) getReturnTypeId() const { return RawSymbol->getTypeId(); }
  /// Return this function signature's return type symbol.
  ///
  /// \returns The return type symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getReturnType() const {
    uint32_t Id = getReturnTypeId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return true if this function signature is unaligned.
  ///
  /// \returns True if this function signature is unaligned.
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  /// Return true if this function signature is volatile-qualified.
  ///
  /// \returns True if this function signature is volatile-qualified.
  FORWARD_SYMBOL_METHOD(isVolatileType)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFUNCTIONSIG_H
