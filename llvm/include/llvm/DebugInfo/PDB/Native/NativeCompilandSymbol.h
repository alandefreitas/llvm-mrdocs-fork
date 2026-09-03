//===- NativeCompilandSymbol.h - native impl for compiland syms -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVECOMPILANDSYMBOL_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVECOMPILANDSYMBOL_H

#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptor.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"

namespace llvm {
namespace pdb {

/// Native PDB implementation of a Compiland (module) symbol.
///
/// Wraps a \c DbiModuleDescriptor from the DBI stream and exposes the
/// compiland properties expected by the PDB symbol API.
class LLVM_ABI NativeCompilandSymbol : public NativeRawSymbol {
public:
  /// Construct a Compiland symbol for the given module descriptor.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param SymbolId The symbol index id assigned to this Compiland symbol.
  /// \param MI The DBI module descriptor for this Compiland.
  NativeCompilandSymbol(NativeSession &Session, SymIndexId SymbolId,
                        DbiModuleDescriptor MI);

  /// Dump this Compiland symbol's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Return the symbol tag for this Compiland.
  ///
  /// \returns Always \c PDB_SymType::Compiland.
  PDB_SymType getSymTag() const override;

  /// Return whether Edit and Continue is enabled for this Compiland.
  ///
  /// \returns True if the module descriptor reports EC info; false otherwise.
  bool isEditAndContinueEnabled() const override;

  /// Return the symbol id of this Compiland's lexical parent.
  ///
  /// \returns Always zero; Compiland symbols have no lexical parent id in the
  ///     native implementation.
  SymIndexId getLexicalParentId() const override;

  /// Return the object-file (library) name for this Compiland.
  ///
  /// Matches DIA naming: this is the module's object file name, not the
  /// module name returned by \c getName.
  ///
  /// \returns The object file name from the DBI module descriptor.
  std::string getLibraryName() const override;

  /// Return the module name of this Compiland.
  ///
  /// Matches DIA naming: this is the module name, not the object file name
  /// returned by \c getLibraryName.
  ///
  /// \returns The module name from the DBI module descriptor.
  std::string getName() const override;

private:
  DbiModuleDescriptor Module;
};

} // namespace pdb
} // namespace llvm

#endif
