//===- NativeTypeFunctionSig.h - info about function signature ---*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEFUNCTIONSIG_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEFUNCTIONSIG_H

#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

/// Native PDB representation of a function-signature type.
///
/// Wraps either a CodeView \c ProcedureRecord or \c MemberFunctionRecord and
/// exposes calling convention, return type, argument list, and related flags.
class LLVM_ABI NativeTypeFunctionSig : public NativeRawSymbol {
protected:
  /// Initialize class-parent and argument-list state after construction.
  void initialize() override;

public:
  /// Construct a function signature from a free-function procedure record.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id Symbol index id assigned to this signature in the cache.
  /// \param TI CodeView type index of this function signature.
  /// \param Proc CodeView procedure record describing the signature.
  NativeTypeFunctionSig(NativeSession &Session, SymIndexId Id,
                        codeview::TypeIndex TI, codeview::ProcedureRecord Proc);

  /// Construct a function signature from a member-function record.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id Symbol index id assigned to this signature in the cache.
  /// \param TI CodeView type index of this function signature.
  /// \param MemberFunc CodeView member-function record describing the
  ///     signature.
  NativeTypeFunctionSig(NativeSession &Session, SymIndexId Id,
                        codeview::TypeIndex TI,
                        codeview::MemberFunctionRecord MemberFunc);

  /// Destroy the native function-signature symbol.
  ~NativeTypeFunctionSig() override;

  /// Dump this function signature's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Find child symbols of the given type under this function signature.
  ///
  /// Only \c PDB_SymType::FunctionArg children are supported; other types
  /// yield an empty enumerator.
  ///
  /// \param Type The kind of child symbols to enumerate.
  /// \returns An enumerator over matching children, or an empty enumerator
  ///     when \p Type is not \c FunctionArg.
  std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type) const override;

  /// Return the symbol id of the enclosing class for a member function.
  ///
  /// \returns The class parent id when this is a member function; otherwise
  ///     zero.
  SymIndexId getClassParentId() const override;

  /// Return the calling convention of this function signature.
  ///
  /// \returns The CodeView calling convention from the procedure or member
  ///     function record.
  PDB_CallingConv getCallingConvention() const override;

  /// Return the number of parameters in this function signature.
  ///
  /// For member functions this includes the implicit \c this parameter.
  ///
  /// \returns The parameter count, including \c this for member functions.
  uint32_t getCount() const override;

  /// Return the symbol id of the function's return type.
  ///
  /// \returns The symbol id corresponding to the return type index.
  SymIndexId getTypeId() const override;

  /// Return the \c this-pointer adjustment for a member function.
  ///
  /// \returns The this-adjust value from the member-function record, or zero
  ///     for a free function.
  int32_t getThisAdjust() const override;

  /// Return whether this member function is a constructor.
  ///
  /// \returns True if the member-function options mark a constructor; false
  ///     for free functions or non-constructor members.
  bool hasConstructor() const override;

  /// Return whether this function signature is const-qualified.
  ///
  /// \returns Always false; constness is not stored on this record.
  bool isConstType() const override;

  /// Return whether this is a constructor that initializes virtual bases.
  ///
  /// \returns True if the member-function options mark a constructor with
  ///     virtual bases; false otherwise.
  bool isConstructorVirtualBase() const override;

  /// Return whether the return type is a C++ UDT returned via hidden pointer.
  ///
  /// \returns True if the \c CxxReturnUdt function option is set.
  bool isCxxReturnUdt() const override;

  /// Return whether this function signature is unaligned.
  ///
  /// \returns Always false; alignment is not stored on this record.
  bool isUnalignedType() const override;

  /// Return whether this function signature is volatile-qualified.
  ///
  /// \returns Always false; volatility is not stored on this record.
  bool isVolatileType() const override;

private:
  void initializeArgList(codeview::TypeIndex ArgListTI);

  union {
    /// CodeView member-function record when \c IsMemberFunction is true.
    codeview::MemberFunctionRecord MemberFunc;
    /// CodeView procedure record when \c IsMemberFunction is false.
    codeview::ProcedureRecord Proc;
  };

  SymIndexId ClassParentId = 0;
  codeview::TypeIndex Index;
  codeview::ArgListRecord ArgList;
  bool IsMemberFunction = false;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEFUNCTIONSIG_H
