//===- NativeTypeArray.h ------------------------------------------ C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEARRAY_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEARRAY_H

#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"

#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

class NativeSession;

/// Native PDB implementation of an array type symbol.
///
/// Wraps a CodeView \c ArrayRecord and its type index, exposing element type,
/// index type, length, and count accessors expected by the PDB symbol API.
class LLVM_ABI NativeTypeArray : public NativeRawSymbol {
public:
  /// Construct a native array type symbol for the given CodeView record.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id The symbol index id assigned to this array type.
  /// \param TI CodeView type index of this array type.
  /// \param Record CodeView array record describing element type and size.
  NativeTypeArray(NativeSession &Session, SymIndexId Id, codeview::TypeIndex TI,
                  codeview::ArrayRecord Record);

  /// Destroy the native array type symbol.
  ~NativeTypeArray() override;

  /// Dump this array type's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Return the symbol id of this array's index type.
  ///
  /// \returns The symbol index id of the CodeView index type from the record.
  SymIndexId getArrayIndexTypeId() const override;

  /// Return whether this array type is const-qualified.
  ///
  /// \returns False; array types are not type-qualified in this implementation.
  bool isConstType() const override;

  /// Return whether this array type is unaligned.
  ///
  /// \returns False; array types are not type-qualified in this implementation.
  bool isUnalignedType() const override;

  /// Return whether this array type is volatile-qualified.
  ///
  /// \returns False; array types are not type-qualified in this implementation.
  bool isVolatileType() const override;

  /// Return the number of elements in this array.
  ///
  /// \returns The array length divided by the element type's length.
  uint32_t getCount() const override;

  /// Return the symbol id of this array's element type.
  ///
  /// \returns The symbol index id of the CodeView element type from the record.
  SymIndexId getTypeId() const override;

  /// Return the size in bytes of this array type.
  ///
  /// \returns The size from the CodeView array record.
  uint64_t getLength() const override;

protected:
  /// CodeView array record holding element type, index type, and size.
  codeview::ArrayRecord Record;
  /// CodeView type index of this array type.
  codeview::TypeIndex Index;
};

} // namespace pdb
} // namespace llvm

#endif
