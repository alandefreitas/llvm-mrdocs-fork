//===- PDBSymDumper.h - base interface for PDB symbol dumper *- C++ -----*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMDUMPER_H
#define LLVM_DEBUGINFO_PDB_PDBSYMDUMPER_H

#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class raw_ostream;
namespace pdb {

/// Visitor interface for dumping concrete PDB symbol types.
///
/// Concrete dumpers override the \c dump and \c dumpRight overloads for the
/// symbol kinds they support. Default \c dump implementations abort when the
/// dumper requires implementations; otherwise they are no-ops. Default
/// \c dumpRight overloads are empty no-ops.
class LLVM_ABI PDBSymDumper {
public:
  /// Construct a symbol dumper.
  ///
  /// \param ShouldRequireImpl If true, unimplemented \c dump overloads abort;
  ///     if false, they are no-ops.
  PDBSymDumper(bool ShouldRequireImpl);
  /// Destroy the symbol dumper.
  virtual ~PDBSymDumper();

  /// Dump a PDB annotation symbol.
  ///
  /// \param Symbol Annotation symbol to dump.
  virtual void dump(const PDBSymbolAnnotation &Symbol);
  /// Dump a PDB block symbol.
  ///
  /// \param Symbol Block symbol to dump.
  virtual void dump(const PDBSymbolBlock &Symbol);
  /// Dump a PDB compiland symbol.
  ///
  /// \param Symbol Compiland symbol to dump.
  virtual void dump(const PDBSymbolCompiland &Symbol);
  /// Dump a PDB compiland-details symbol.
  ///
  /// \param Symbol Compiland-details symbol to dump.
  virtual void dump(const PDBSymbolCompilandDetails &Symbol);
  /// Dump a PDB compiland environment symbol.
  ///
  /// \param Symbol Compiland environment symbol to dump.
  virtual void dump(const PDBSymbolCompilandEnv &Symbol);
  /// Dump a PDB custom symbol.
  ///
  /// \param Symbol Custom symbol to dump.
  virtual void dump(const PDBSymbolCustom &Symbol);
  /// Dump a PDB data symbol.
  ///
  /// \param Symbol Data symbol to dump.
  virtual void dump(const PDBSymbolData &Symbol);
  /// Dump a PDB executable symbol.
  ///
  /// \param Symbol Executable symbol to dump.
  virtual void dump(const PDBSymbolExe &Symbol);
  /// Dump a PDB function symbol.
  ///
  /// \param Symbol Function symbol to dump.
  virtual void dump(const PDBSymbolFunc &Symbol);
  /// Dump a PDB function-debug-end symbol.
  ///
  /// \param Symbol Function-debug-end symbol to dump.
  virtual void dump(const PDBSymbolFuncDebugEnd &Symbol);
  /// Dump a PDB function-debug-start symbol.
  ///
  /// \param Symbol Function-debug-start symbol to dump.
  virtual void dump(const PDBSymbolFuncDebugStart &Symbol);
  /// Dump a PDB label symbol.
  ///
  /// \param Symbol Label symbol to dump.
  virtual void dump(const PDBSymbolLabel &Symbol);
  /// Dump a PDB public symbol.
  ///
  /// \param Symbol Public symbol to dump.
  virtual void dump(const PDBSymbolPublicSymbol &Symbol);
  /// Dump a PDB thunk symbol.
  ///
  /// \param Symbol Thunk symbol to dump.
  virtual void dump(const PDBSymbolThunk &Symbol);
  /// Dump a PDB array type symbol.
  ///
  /// \param Symbol Array type symbol to dump.
  virtual void dump(const PDBSymbolTypeArray &Symbol);
  /// Dump a PDB base-class type symbol.
  ///
  /// \param Symbol Base-class type symbol to dump.
  virtual void dump(const PDBSymbolTypeBaseClass &Symbol);
  /// Dump a PDB builtin type symbol.
  ///
  /// \param Symbol Builtin type symbol to dump.
  virtual void dump(const PDBSymbolTypeBuiltin &Symbol);
  /// Dump a PDB custom type symbol.
  ///
  /// \param Symbol Custom type symbol to dump.
  virtual void dump(const PDBSymbolTypeCustom &Symbol);
  /// Dump a PDB dimension type symbol.
  ///
  /// \param Symbol Dimension type symbol to dump.
  virtual void dump(const PDBSymbolTypeDimension &Symbol);
  /// Dump a PDB enum type symbol.
  ///
  /// \param Symbol Enum type symbol to dump.
  virtual void dump(const PDBSymbolTypeEnum &Symbol);
  /// Dump a PDB friend type symbol.
  ///
  /// \param Symbol Friend type symbol to dump.
  virtual void dump(const PDBSymbolTypeFriend &Symbol);
  /// Dump a PDB function-argument type symbol.
  ///
  /// \param Symbol Function-argument type symbol to dump.
  virtual void dump(const PDBSymbolTypeFunctionArg &Symbol);
  /// Dump a PDB function-signature type symbol.
  ///
  /// \param Symbol Function-signature type symbol to dump.
  virtual void dump(const PDBSymbolTypeFunctionSig &Symbol);
  /// Dump a PDB managed type symbol.
  ///
  /// \param Symbol Managed type symbol to dump.
  virtual void dump(const PDBSymbolTypeManaged &Symbol);
  /// Dump a PDB pointer type symbol.
  ///
  /// \param Symbol Pointer type symbol to dump.
  virtual void dump(const PDBSymbolTypePointer &Symbol);
  /// Dump a PDB typedef type symbol.
  ///
  /// \param Symbol Typedef type symbol to dump.
  virtual void dump(const PDBSymbolTypeTypedef &Symbol);
  /// Dump a PDB UDT type symbol.
  ///
  /// \param Symbol UDT type symbol to dump.
  virtual void dump(const PDBSymbolTypeUDT &Symbol);
  /// Dump a PDB vtable type symbol.
  ///
  /// \param Symbol Vtable type symbol to dump.
  virtual void dump(const PDBSymbolTypeVTable &Symbol);
  /// Dump a PDB vtable-shape type symbol.
  ///
  /// \param Symbol Vtable-shape type symbol to dump.
  virtual void dump(const PDBSymbolTypeVTableShape &Symbol);
  /// Dump a PDB unknown symbol.
  ///
  /// \param Symbol Unknown symbol to dump.
  virtual void dump(const PDBSymbolUnknown &Symbol);
  /// Dump a PDB using-namespace symbol.
  ///
  /// \param Symbol Using-namespace symbol to dump.
  virtual void dump(const PDBSymbolUsingNamespace &Symbol);

  /// Dump the right-hand side of an array type symbol.
  ///
  /// \param Symbol Array type symbol whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeArray &Symbol) {}
  /// Dump the right-hand side of a base-class type symbol.
  ///
  /// \param Symbol Base-class type symbol whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeBaseClass &Symbol) {}
  /// Dump the right-hand side of a builtin type symbol.
  ///
  /// \param Symbol Builtin type symbol whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeBuiltin &Symbol) {}
  /// Dump the right-hand side of a custom type symbol.
  ///
  /// \param Symbol Custom type symbol whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeCustom &Symbol) {}
  /// Dump the right-hand side of a dimension type symbol.
  ///
  /// \param Symbol Dimension type symbol whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeDimension &Symbol) {}
  /// Dump the right-hand side of an enum type symbol.
  ///
  /// \param Symbol Enum type symbol whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeEnum &Symbol) {}
  /// Dump the right-hand side of a friend type symbol.
  ///
  /// \param Symbol Friend type symbol whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeFriend &Symbol) {}
  /// Dump the right-hand side of a function-argument type symbol.
  ///
  /// \param Symbol Function-argument type whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeFunctionArg &Symbol) {}
  /// Dump the right-hand side of a function-signature type symbol.
  ///
  /// \param Symbol Function-signature type whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeFunctionSig &Symbol) {}
  /// Dump the right-hand side of a managed type symbol.
  ///
  /// \param Symbol Managed type symbol whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeManaged &Symbol) {}
  /// Dump the right-hand side of a pointer type symbol.
  ///
  /// \param Symbol Pointer type symbol whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypePointer &Symbol) {}
  /// Dump the right-hand side of a typedef type symbol.
  ///
  /// \param Symbol Typedef type symbol whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeTypedef &Symbol) {}
  /// Dump the right-hand side of a UDT type symbol.
  ///
  /// \param Symbol UDT type symbol whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeUDT &Symbol) {}
  /// Dump the right-hand side of a vtable type symbol.
  ///
  /// \param Symbol Vtable type symbol whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeVTable &Symbol) {}
  /// Dump the right-hand side of a vtable-shape type symbol.
  ///
  /// \param Symbol Vtable-shape type whose right-hand side is dumped.
  virtual void dumpRight(const PDBSymbolTypeVTableShape &Symbol) {}

private:
  bool RequireImpl;
};
}
}

#endif
