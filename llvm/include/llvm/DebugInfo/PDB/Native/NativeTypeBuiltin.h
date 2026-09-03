//===- NativeTypeBuiltin.h ---------------------------------------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEBUILTIN_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEBUILTIN_H

#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"

#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

class NativeSession;

/// Native PDB representation of a builtin (primitive) type symbol.
///
/// Stores the builtin type kind, size, and CodeView type modifiers for a
/// simple type from the PDB type stream.
class LLVM_ABI NativeTypeBuiltin : public NativeRawSymbol {
public:
  /// Construct a native builtin type symbol.
  ///
  /// \param PDBSession The native PDB session that owns this symbol.
  /// \param Id Symbol index ID assigned to this symbol in the cache.
  /// \param Mods CodeView modifier options (const, volatile, unaligned).
  /// \param T The builtin type kind represented by this symbol.
  /// \param L Size in bytes of this builtin type.
  NativeTypeBuiltin(NativeSession &PDBSession, SymIndexId Id,
                    codeview::ModifierOptions Mods, PDB_BuiltinType T,
                    uint64_t L);
  /// Destroy the native builtin type symbol.
  ~NativeTypeBuiltin() override;

  /// Dump this symbol's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Return the symbol tag for this builtin type.
  ///
  /// \returns Always \c PDB_SymType::BuiltinType.
  PDB_SymType getSymTag() const override;

  /// Return the builtin type kind for this type symbol.
  ///
  /// \returns The \c PDB_BuiltinType value stored for this symbol.
  PDB_BuiltinType getBuiltinType() const override;
  /// Return true if this type is const-qualified.
  ///
  /// \returns True if the CodeView modifiers include const.
  bool isConstType() const override;
  /// Return the length in bytes of this builtin type.
  ///
  /// \returns The size in bytes stored for this type.
  uint64_t getLength() const override;
  /// Return true if this type is unaligned.
  ///
  /// \returns True if the CodeView modifiers include unaligned.
  bool isUnalignedType() const override;
  /// Return true if this type is volatile-qualified.
  ///
  /// \returns True if the CodeView modifiers include volatile.
  bool isVolatileType() const override;

protected:
  /// The native PDB session that owns this symbol.
  NativeSession &Session;
  /// CodeView modifier options (const, volatile, unaligned) for this type.
  codeview::ModifierOptions Mods;
  /// The builtin type kind represented by this symbol.
  PDB_BuiltinType Type;
  /// Size in bytes of this builtin type.
  uint64_t Length;
};

} // namespace pdb
} // namespace llvm

#endif
