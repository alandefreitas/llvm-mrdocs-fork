//===- NativeTypeTypedef.h - info about typedef ------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPETYPEDEF_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPETYPEDEF_H

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {

class raw_ostream;

namespace pdb {

class NativeSession;

/// Native PDB symbol wrapping a CodeView typedef (UDT alias) record.
///
/// Exposes the typedef name and the symbol index of the underlying type.
class LLVM_ABI NativeTypeTypedef : public NativeRawSymbol {
public:
  /// Construct a native typedef symbol for \p Typedef.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id The symbol index id assigned to this typedef symbol.
  /// \param Typedef The CodeView UDT/typedef symbol record to wrap.
  NativeTypeTypedef(NativeSession &Session, SymIndexId Id,
                    codeview::UDTSym Typedef);

  /// Destroy the native typedef symbol.
  ~NativeTypeTypedef() override;

  /// Dump this symbol's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Return the name of this typedef.
  ///
  /// \returns The typedef name from the CodeView UDT/typedef record.
  std::string getName() const override;

  /// Return the symbol index id of the underlying type.
  ///
  /// \returns The symbol index id of the type this typedef aliases.
  SymIndexId getTypeId() const override;

protected:
  /// The underlying CodeView typedef (UDT alias) record.
  codeview::UDTSym Record;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPETYPEDEF_H
