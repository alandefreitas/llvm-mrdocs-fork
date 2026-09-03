//===- PDBSymbolFuncDebugEnd.h - function end bounds info -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLFUNCDEBUGEND_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLFUNCDEBUGEND_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace pdb {

/// PDB symbol for the end of a function's debug code range.
///
/// Marks the address where debugger-visible function code ends (typically the
/// start of the epilogue), as a child of the enclosing function symbol.
class LLVM_ABI PDBSymbolFuncDebugEnd : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Sym tag value identifying function-debug-end symbols.
  static const PDB_SymType Tag = PDB_SymType::FuncDebugEnd;

  /// Return true if \p S is a function-debug-end symbol.
  ///
  /// \param S Symbol to test.
  /// \returns True if \p S is a function-debug-end symbol.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this function-debug-end symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the section-relative address offset of this symbol.
  ///
  /// \returns The section-relative address offset of this symbol.
  FORWARD_SYMBOL_METHOD(getAddressOffset)
  /// Return the section index of this symbol's address.
  ///
  /// \returns The section index of this symbol's address.
  FORWARD_SYMBOL_METHOD(getAddressSection)
  /// Return true if this function uses a custom calling convention.
  ///
  /// \returns True if this function uses a custom calling convention.
  FORWARD_SYMBOL_METHOD(hasCustomCallingConvention)
  /// Return true if this function uses a far return.
  ///
  /// \returns True if this function uses a far return.
  FORWARD_SYMBOL_METHOD(hasFarReturn)
  /// Return true if this function uses an interrupt return.
  ///
  /// \returns True if this function uses an interrupt return.
  FORWARD_SYMBOL_METHOD(hasInterruptReturn)
  /// Return true if this symbol is static.
  ///
  /// \returns True if this symbol is static.
  FORWARD_SYMBOL_METHOD(isStatic)
  /// Return the symbol id of this symbol's lexical parent.
  ///
  /// \returns The symbol id of this symbol's lexical parent.
  decltype(auto) getLexicalParentId() const {
    return RawSymbol->getLexicalParentId();
  }
  /// Return this symbol's lexical parent symbol.
  ///
  /// \returns The lexical parent symbol, or nullptr if none.
  std::unique_ptr<PDBSymbol> getLexicalParent() const {
    uint32_t Id = getLexicalParentId();
    return getConcreteSymbolByIdHelper<PDBSymbol>(Id);
  }
  /// Return the location type of this symbol.
  ///
  /// \returns The location type of this symbol.
  FORWARD_SYMBOL_METHOD(getLocationType)
  /// Return true if this function has the noinline attribute.
  ///
  /// \returns True if this function has the noinline attribute.
  FORWARD_SYMBOL_METHOD(hasNoInlineAttribute)
  /// Return true if this function has the noreturn attribute.
  ///
  /// \returns True if this function has the noreturn attribute.
  FORWARD_SYMBOL_METHOD(hasNoReturnAttribute)
  /// Return true if this code is unreachable.
  ///
  /// \returns True if this code is unreachable.
  FORWARD_SYMBOL_METHOD(isUnreached)
  /// Return the offset associated with this symbol.
  ///
  /// \returns The offset associated with this symbol.
  FORWARD_SYMBOL_METHOD(getOffset)
  /// Return true if this symbol has optimized-code debug info.
  ///
  /// \returns True if this symbol has optimized-code debug info.
  FORWARD_SYMBOL_METHOD(hasOptimizedCodeDebugInfo)
  /// Return the relative virtual address of this symbol.
  ///
  /// \returns The relative virtual address of this symbol.
  FORWARD_SYMBOL_METHOD(getRelativeVirtualAddress)
  /// Return the virtual address of this symbol.
  ///
  /// \returns The virtual address of this symbol.
  FORWARD_SYMBOL_METHOD(getVirtualAddress)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLFUNCDEBUGEND_H
