//===- NativeTypeUDT.h - info about class/struct type ------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEUDT_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEUDT_H

#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {

class raw_ostream;
namespace pdb {
class NativeSession;

/// Native PDB representation of a user-defined type (class, struct, or union).
///
/// Wraps a CodeView \c ClassRecord or \c UnionRecord, or a cv-qualified view
/// of an unmodified UDT, and exposes the UDT properties expected by the PDB
/// symbol API.
class LLVM_ABI NativeTypeUDT : public NativeRawSymbol {
public:
  /// Construct a native UDT symbol from a CodeView class or struct record.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id Symbol index ID assigned to this symbol in the cache.
  /// \param TI CodeView type index of this UDT in the TPI stream.
  /// \param Class CodeView class/struct record describing the UDT.
  NativeTypeUDT(NativeSession &Session, SymIndexId Id, codeview::TypeIndex TI,
                codeview::ClassRecord Class);

  /// Construct a native UDT symbol from a CodeView union record.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id Symbol index ID assigned to this symbol in the cache.
  /// \param TI CodeView type index of this UDT in the TPI stream.
  /// \param Union CodeView union record describing the UDT.
  NativeTypeUDT(NativeSession &Session, SymIndexId Id, codeview::TypeIndex TI,
                codeview::UnionRecord Union);

  /// Construct a cv-qualified view of an unmodified UDT.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id Symbol index ID assigned to this symbol in the cache.
  /// \param UnmodifiedType The underlying unmodified UDT symbol.
  /// \param Modifier CodeView modifier record describing const/volatile/
  ///     unaligned qualifiers.
  NativeTypeUDT(NativeSession &Session, SymIndexId Id,
                NativeTypeUDT &UnmodifiedType,
                codeview::ModifierRecord Modifier);

  /// Destroy the native UDT symbol.
  ~NativeTypeUDT() override;

  /// Dump this UDT symbol's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Return the name of this UDT.
  ///
  /// \returns The UDT name from the tag record, or from the unmodified type
  ///     when this symbol is a modified view.
  std::string getName() const override;

  /// Return the symbol id of this UDT's lexical parent.
  ///
  /// \returns Always zero; UDT symbols have no lexical parent id in the
  ///     native implementation.
  SymIndexId getLexicalParentId() const override;

  /// Return the symbol id of the unmodified UDT, if any.
  ///
  /// \returns The unmodified type's symbol id when this is a modified view;
  ///     otherwise zero.
  SymIndexId getUnmodifiedTypeId() const override;

  /// Return the symbol id of this UDT's virtual table shape.
  ///
  /// \returns The vtable shape symbol id for class/struct types, or zero for
  ///     unions and when no vtable shape is recorded.
  SymIndexId getVirtualTableShapeId() const override;

  /// Return the size in bytes of this UDT.
  ///
  /// \returns The size from the class or union record, or from the unmodified
  ///     type when this symbol is a modified view.
  uint64_t getLength() const override;

  /// Return the kind of user-defined type.
  ///
  /// \returns One of \c PDB_UdtType::Class, \c Struct, \c Union, or
  ///     \c Interface, forwarded from the unmodified type when applicable.
  PDB_UdtType getUdtKind() const override;

  /// Return whether this UDT declares a constructor or destructor.
  ///
  /// \returns True if the tag record's options include
  ///     \c HasConstructorOrDestructor; false otherwise.
  bool hasConstructor() const override;

  /// Return whether this UDT is const-qualified.
  ///
  /// \returns True if a modifier record is present and includes
  ///     \c ModifierOptions::Const; false otherwise.
  bool isConstType() const override;

  /// Return whether this UDT has an overloaded assignment operator.
  ///
  /// \returns True if the tag record's options include
  ///     \c HasOverloadedAssignmentOperator; false otherwise.
  bool hasAssignmentOperator() const override;

  /// Return whether this UDT has a conversion (cast) operator.
  ///
  /// \returns True if the tag record's options include
  ///     \c HasConversionOperator; false otherwise.
  bool hasCastOperator() const override;

  /// Return whether this UDT contains nested types.
  ///
  /// \returns True if the tag record's options include
  ///     \c ContainsNestedClass; false otherwise.
  bool hasNestedTypes() const override;

  /// Return whether this UDT has any overloaded operators.
  ///
  /// \returns True if the tag record's options include
  ///     \c HasOverloadedOperator; false otherwise.
  bool hasOverloadedOperator() const override;

  /// Return whether this UDT is a C++/CLI interface.
  ///
  /// \returns Always false in the native implementation.
  bool isInterfaceUdt() const override;

  /// Return whether this UDT is an intrinsic type.
  ///
  /// \returns True if the tag record's options include \c Intrinsic; false
  ///     otherwise.
  bool isIntrinsic() const override;

  /// Return whether this UDT is nested inside another type.
  ///
  /// \returns True if the tag record's options include \c Nested; false
  ///     otherwise.
  bool isNested() const override;

  /// Return whether this UDT is packed.
  ///
  /// \returns True if the tag record's options include \c Packed; false
  ///     otherwise.
  bool isPacked() const override;

  /// Return whether this UDT is a C++/CLI ref class.
  ///
  /// \returns Always false in the native implementation.
  bool isRefUdt() const override;

  /// Return whether this UDT is a scoped type (for example, a C++ nested type).
  ///
  /// \returns True if the tag record's options include \c Scoped; false
  ///     otherwise.
  bool isScoped() const override;

  /// Return whether this UDT is a C++/CLI value class.
  ///
  /// \returns Always false in the native implementation.
  bool isValueUdt() const override;

  /// Return whether this UDT is unaligned-qualified.
  ///
  /// \returns True if a modifier record is present and includes
  ///     \c ModifierOptions::Unaligned; false otherwise.
  bool isUnalignedType() const override;

  /// Return whether this UDT is volatile-qualified.
  ///
  /// \returns True if a modifier record is present and includes
  ///     \c ModifierOptions::Volatile; false otherwise.
  bool isVolatileType() const override;

protected:
  /// CodeView type index of this UDT in the TPI stream.
  codeview::TypeIndex Index;

  /// CodeView class/struct record when this UDT is a class or struct.
  std::optional<codeview::ClassRecord> Class;

  /// CodeView union record when this UDT is a union.
  std::optional<codeview::UnionRecord> Union;

  /// Underlying unmodified UDT when this symbol is a modified view.
  NativeTypeUDT *UnmodifiedType = nullptr;

  /// Pointer to the active class or union tag record, when present.
  codeview::TagRecord *Tag = nullptr;

  /// Optional cv-qualifier modifiers for a modified view of this UDT.
  std::optional<codeview::ModifierRecord> Modifiers;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEUDT_H
