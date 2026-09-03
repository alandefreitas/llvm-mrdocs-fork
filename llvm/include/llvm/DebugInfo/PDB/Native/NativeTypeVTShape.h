//===- NativeTypeVTShape.h - info about virtual table shape ------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEVTSHAPE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEVTSHAPE_H

#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {
class NativeSession;

/// Native PDB implementation of a virtual table shape type symbol.
///
/// Wraps a CodeView \c VFTableShapeRecord and its type index, exposing the
/// slot count and type-qualifier accessors expected by the PDB symbol API.
class LLVM_ABI NativeTypeVTShape : public NativeRawSymbol {
public:
  /// Construct a native vtable shape symbol for the given CodeView record.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id The symbol index id assigned to this vtable shape.
  /// \param TI CodeView type index of this vtable shape.
  /// \param SR CodeView VFTableShapeRecord describing the vtable slots.
  NativeTypeVTShape(NativeSession &Session, SymIndexId Id,
                    codeview::TypeIndex TI, codeview::VFTableShapeRecord SR);

  /// Destroy the native vtable shape symbol.
  ~NativeTypeVTShape() override;

  /// Dump this vtable shape's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Return whether this vtable shape is const-qualified.
  ///
  /// \returns False; vtable shapes are not type-qualified in this
  ///     implementation.
  bool isConstType() const override;

  /// Return whether this vtable shape is volatile-qualified.
  ///
  /// \returns False; vtable shapes are not type-qualified in this
  ///     implementation.
  bool isVolatileType() const override;

  /// Return whether this vtable shape is unaligned.
  ///
  /// \returns False; vtable shapes are not type-qualified in this
  ///     implementation.
  bool isUnalignedType() const override;

  /// Return the number of slots in this vtable shape.
  ///
  /// \returns The number of entries in the CodeView VFTableShapeRecord.
  uint32_t getCount() const override;

protected:
  /// CodeView type index of this vtable shape.
  codeview::TypeIndex TI;
  /// CodeView VFTableShapeRecord describing the vtable slot descriptors.
  codeview::VFTableShapeRecord Record;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEVTSHAPE_H
