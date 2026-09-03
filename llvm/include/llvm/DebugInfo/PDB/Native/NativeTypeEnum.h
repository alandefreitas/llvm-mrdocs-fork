//===- NativeTypeEnum.h - info about enum type ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEENUM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEENUM_H

#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
class raw_ostream;
namespace pdb {

class NativeTypeBuiltin;

/// Native PDB symbol wrapping a CodeView enumeration type record.
///
/// Exposes enum properties and optional CV modifiers (const, volatile,
/// unaligned) for an \c LF_ENUM type from the TPI stream.
class LLVM_ABI NativeTypeEnum : public NativeRawSymbol {
public:
  /// Construct a native enum type from a CodeView enum record.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id The symbol index id assigned to this enum type.
  /// \param TI CodeView type index of this enum in the TPI stream.
  /// \param Record The CodeView enum record to wrap.
  NativeTypeEnum(NativeSession &Session, SymIndexId Id, codeview::TypeIndex TI,
                 codeview::EnumRecord Record);

  /// Construct a modified enum type that applies CV modifiers.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id The symbol index id assigned to this modified enum type.
  /// \param UnmodifiedType The unmodified enum type this modifier wraps.
  /// \param Modifier The CodeView modifier record (const, volatile, unaligned).
  NativeTypeEnum(NativeSession &Session, SymIndexId Id,
                 NativeTypeEnum &UnmodifiedType,
                 codeview::ModifierRecord Modifier);

  /// Destroy the native enum type symbol.
  ~NativeTypeEnum() override;

  /// Dump this enum type's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Find child symbols of the given type under this enum.
  ///
  /// Enumerator members are exposed when \p Type is \c PDB_SymType::Data.
  ///
  /// \param Type Symbol tag of children to enumerate.
  ///
  /// \returns An enumerator over matching children, or an empty enumerator
  ///     when \p Type is not supported.
  std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type) const override;

  /// Return the builtin type kind of this enum's underlying type.
  ///
  /// \returns The \c PDB_BuiltinType for the underlying simple type, or
  ///     \c PDB_BuiltinType::None if the underlying type is corrupt or not
  ///     a direct simple type.
  PDB_BuiltinType getBuiltinType() const override;

  /// Return the symbol tag for this enum type.
  ///
  /// \returns Always \c PDB_SymType::Enum.
  PDB_SymType getSymTag() const override;

  /// Return the symbol id of the unmodified enum type.
  ///
  /// \returns The unmodified type's symbol id when this is a modified type,
  ///     or zero otherwise.
  SymIndexId getUnmodifiedTypeId() const override;

  /// Return whether this enum declares a constructor or destructor.
  ///
  /// \returns True if the CodeView class options include
  ///     \c HasConstructorOrDestructor.
  bool hasConstructor() const override;

  /// Return whether this enum has an overloaded assignment operator.
  ///
  /// \returns True if the CodeView class options include
  ///     \c HasOverloadedAssignmentOperator.
  bool hasAssignmentOperator() const override;

  /// Return whether this enum has a conversion (cast) operator.
  ///
  /// \returns True if the CodeView class options include
  ///     \c HasConversionOperator.
  bool hasCastOperator() const override;

  /// Return the size in bytes of this enum's underlying type.
  ///
  /// \returns The length of the underlying builtin type, or zero if it cannot
  ///     be resolved.
  uint64_t getLength() const override;

  /// Return the name of this enum type.
  ///
  /// \returns The enum name from the CodeView record.
  std::string getName() const override;

  /// Return whether this type is const-qualified.
  ///
  /// \returns True if a modifier record is present and includes
  ///     \c ModifierOptions::Const.
  bool isConstType() const override;

  /// Return whether this type is volatile-qualified.
  ///
  /// \returns True if a modifier record is present and includes
  ///     \c ModifierOptions::Volatile.
  bool isVolatileType() const override;

  /// Return whether this type is unaligned.
  ///
  /// \returns True if a modifier record is present and includes
  ///     \c ModifierOptions::Unaligned.
  bool isUnalignedType() const override;

  /// Return whether this enum is nested inside another type.
  ///
  /// \returns True if the CodeView class options include \c Nested.
  bool isNested() const override;

  /// Return whether this enum has overloaded operators.
  ///
  /// \returns True if the CodeView class options include
  ///     \c HasOverloadedOperator.
  bool hasOverloadedOperator() const override;

  /// Return whether this enum contains nested types.
  ///
  /// \returns True if the CodeView class options include
  ///     \c ContainsNestedClass.
  bool hasNestedTypes() const override;

  /// Return whether this enum is an intrinsic type.
  ///
  /// \returns True if the CodeView class options include \c Intrinsic.
  bool isIntrinsic() const override;

  /// Return whether this enum is packed.
  ///
  /// \returns True if the CodeView class options include \c Packed.
  bool isPacked() const override;

  /// Return whether this enum is a C++ scoped enumeration.
  ///
  /// \returns True if the CodeView class options include \c Scoped.
  bool isScoped() const override;

  /// Return the symbol id of this enum's underlying type.
  ///
  /// \returns The symbol cache id for the underlying CodeView type index.
  SymIndexId getTypeId() const override;

  /// Return whether this type is a reference UDT.
  ///
  /// \returns Always false; enum types are not reference UDTs.
  bool isRefUdt() const override;

  /// Return whether this type is a value UDT.
  ///
  /// \returns Always false; enum types are not value UDTs.
  bool isValueUdt() const override;

  /// Return whether this type is an interface UDT.
  ///
  /// \returns Always false; enum types are not interface UDTs.
  bool isInterfaceUdt() const override;

  /// Return the native builtin type underlying this enum.
  ///
  /// \returns A reference to the \c NativeTypeBuiltin for the underlying type.
  const NativeTypeBuiltin &getUnderlyingBuiltinType() const;

  /// Return the CodeView enum record for this type.
  ///
  /// \returns The wrapped \c EnumRecord; valid only for unmodified enums.
  const codeview::EnumRecord &getEnumRecord() const { return *Record; }

protected:
  /// CodeView type index of this enum in the TPI stream.
  codeview::TypeIndex Index;

  /// CodeView enum record when this symbol is unmodified.
  std::optional<codeview::EnumRecord> Record;

  /// Unmodified enum type when this symbol applies CV modifiers.
  NativeTypeEnum *UnmodifiedType = nullptr;

  /// Optional CV modifiers applied to the unmodified enum type.
  std::optional<codeview::ModifierRecord> Modifiers;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEENUM_H
