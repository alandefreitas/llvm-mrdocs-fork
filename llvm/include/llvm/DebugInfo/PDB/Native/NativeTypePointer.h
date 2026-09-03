//===- NativeTypePointer.h - info about pointer type -------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEPOINTER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEPOINTER_H

#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

/// Native PDB symbol for a pointer, reference, or member-pointer type.
///
/// Wraps either a simple CodeView pointer type index or a non-simple
/// \c PointerRecord from a native session.
class LLVM_ABI NativeTypePointer : public NativeRawSymbol {
public:
  /// Construct a pointer symbol for a simple CodeView pointer type.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id The symbol index id assigned to this pointer type.
  /// \param TI Simple CodeView type index describing the pointer.
  NativeTypePointer(NativeSession &Session, SymIndexId Id,
                    codeview::TypeIndex TI);

  /// Construct a pointer symbol for a non-simple CodeView pointer record.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id The symbol index id assigned to this pointer type.
  /// \param TI CodeView type index of this pointer type.
  /// \param PR CodeView pointer record holding mode, options, and referent.
  NativeTypePointer(NativeSession &Session, SymIndexId Id,
                    codeview::TypeIndex TI, codeview::PointerRecord PR);

  /// Destroy the native pointer type symbol.
  ~NativeTypePointer() override;

  /// Dump this symbol's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Return the symbol id of the containing class for a member pointer.
  ///
  /// \returns The containing type's symbol id, or zero if this is not a
  ///     member pointer.
  SymIndexId getClassParentId() const override;

  /// Return true if this pointer type is const-qualified.
  ///
  /// \returns True when the pointer record has the const option set.
  bool isConstType() const override;

  /// Return the size in bytes of this pointer or reference type.
  ///
  /// \returns The size from the pointer record, or the size implied by the
  ///     simple type mode when no record is present.
  uint64_t getLength() const override;

  /// Return true if this is an lvalue reference type.
  ///
  /// \returns True when the pointer record mode is an lvalue reference.
  bool isReference() const override;

  /// Return true if this is an rvalue reference type.
  ///
  /// \returns True when the pointer record mode is an rvalue reference.
  bool isRValueReference() const override;

  /// Return true if this is a pointer to a data member.
  ///
  /// \returns True when the pointer record mode is pointer-to-data-member.
  bool isPointerToDataMember() const override;

  /// Return true if this is a pointer to a member function.
  ///
  /// \returns True when the pointer record mode is pointer-to-member-function.
  bool isPointerToMemberFunction() const override;

  /// Return the symbol id of the pointee type.
  ///
  /// \returns The symbol index id of the type this pointer refers to.
  SymIndexId getTypeId() const override;

  /// Return true if this pointer type is restrict-qualified.
  ///
  /// \returns True when the pointer record has the restrict option set.
  bool isRestrictedType() const override;

  /// Return true if this pointer type is volatile-qualified.
  ///
  /// \returns True when the pointer record has the volatile option set.
  bool isVolatileType() const override;

  /// Return true if this pointer type is unaligned.
  ///
  /// \returns True when the pointer record has the unaligned option set.
  bool isUnalignedType() const override;

  /// Return true if this member pointer uses single inheritance.
  ///
  /// \returns True when the member-pointer representation is single
  ///     inheritance for data or function.
  bool isSingleInheritance() const override;

  /// Return true if this member pointer uses multiple inheritance.
  ///
  /// \returns True when the member-pointer representation is multiple
  ///     inheritance for data or function.
  bool isMultipleInheritance() const override;

  /// Return true if this member pointer uses virtual inheritance.
  ///
  /// \returns True when the member-pointer representation is virtual
  ///     inheritance for data or function.
  bool isVirtualInheritance() const override;

protected:
  /// Return true if this is a pointer to a data member or member function.
  ///
  /// \returns True when \c isPointerToDataMember or
  ///     \c isPointerToMemberFunction is true.
  bool isMemberPointer() const;

  /// CodeView type index of this pointer type.
  codeview::TypeIndex TI;

  /// CodeView pointer record for non-simple pointer types, if present.
  std::optional<codeview::PointerRecord> Record;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEPOINTER_H
