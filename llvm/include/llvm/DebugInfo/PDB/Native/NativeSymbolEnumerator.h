//===- NativeSymbolEnumerator.h - info about enumerator values --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVESYMBOLENUMERATOR_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVESYMBOLENUMERATOR_H

#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {

class raw_ostream;
namespace pdb {
class NativeSession;
class NativeTypeEnum;

/// Native PDB symbol for a single enumerator (enum constant) value.
///
/// Wraps a CodeView \c EnumeratorRecord together with its parent enum type
/// from a native session, exposing the constant as a \c PDB_SymType::Data
/// symbol.
class LLVM_ABI NativeSymbolEnumerator : public NativeRawSymbol {
public:
  /// Construct an enumerator symbol for the given enum parent and record.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id The symbol index id assigned to this enumerator.
  /// \param Parent The enum type that declares this enumerator.
  /// \param Record CodeView enumerator record holding the name and value.
  NativeSymbolEnumerator(NativeSession &Session, SymIndexId Id,
                         const NativeTypeEnum &Parent,
                         codeview::EnumeratorRecord Record);

  /// Destroy the enumerator symbol.
  ~NativeSymbolEnumerator() override;

  /// Dump this enumerator's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Return the symbol id of this enumerator's class parent.
  ///
  /// \returns The symbol index id of the parent enum type.
  SymIndexId getClassParentId() const override;

  /// Return the symbol id of this enumerator's lexical parent.
  ///
  /// \returns Zero; enumerator symbols have no lexical parent in this
  ///     implementation.
  SymIndexId getLexicalParentId() const override;

  /// Return the name of this enumerator.
  ///
  /// \returns The enumerator name from the CodeView record.
  std::string getName() const override;

  /// Return the symbol id of this enumerator's underlying type.
  ///
  /// \returns The type id of the parent enum's underlying type.
  SymIndexId getTypeId() const override;

  /// Return the data kind of this enumerator.
  ///
  /// \returns \c PDB_DataKind::Constant.
  PDB_DataKind getDataKind() const override;

  /// Return the location type of this enumerator.
  ///
  /// \returns \c PDB_LocType::Constant.
  PDB_LocType getLocationType() const override;

  /// Return whether this enumerator is const-qualified.
  ///
  /// \returns False; enumerator symbols are not type-qualified.
  bool isConstType() const override;

  /// Return whether this enumerator is volatile-qualified.
  ///
  /// \returns False; enumerator symbols are not type-qualified.
  bool isVolatileType() const override;

  /// Return whether this enumerator is unaligned.
  ///
  /// \returns False; enumerator symbols are not type-qualified.
  bool isUnalignedType() const override;

  /// Return the constant value of this enumerator.
  ///
  /// \returns The enumerator value typed according to the parent enum's
  ///     underlying builtin type.
  Variant getValue() const override;

protected:
  /// The enum type that declares this enumerator.
  const NativeTypeEnum &Parent;
  /// CodeView enumerator record holding the name and constant value.
  codeview::EnumeratorRecord Record;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVESYMBOLENUMERATOR_H
