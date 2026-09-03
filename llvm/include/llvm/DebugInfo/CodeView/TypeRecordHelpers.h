//===- TypeRecordHelpers.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPERECORDHELPERS_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPERECORDHELPERS_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace codeview {

/// Given an arbitrary codeview type, determine if it is an LF_STRUCTURE,
/// LF_CLASS, LF_INTERFACE, LF_UNION, or LF_ENUM with the forward ref class
/// option.
/// \param CVT CodeView type record to test for a UDT forward reference.
/// \returns True if \p CVT is a UDT forward reference.
LLVM_ABI bool isUdtForwardRef(CVType CVT);

/// Given a CVType which is assumed to be an LF_MODIFIER, return the
/// TypeIndex of the type that the LF_MODIFIER modifies.
/// \param CVT CodeView LF_MODIFIER type record whose underlying type is sought.
/// \returns The TypeIndex of the type modified by \p CVT.
LLVM_ABI TypeIndex getModifiedType(const CVType &CVT);

/// Return true if this record should be in the IPI stream of a PDB. In an
/// object file, these record kinds will appear mixed into the .debug$T section.
/// \param K Type leaf kind to test for an ID record.
/// \returns True if \p K is an ID record kind for the IPI stream.
inline bool isIdRecord(TypeLeafKind K) {
  switch (K) {
  case TypeLeafKind::LF_FUNC_ID:
  case TypeLeafKind::LF_MFUNC_ID:
  case TypeLeafKind::LF_STRING_ID:
  case TypeLeafKind::LF_SUBSTR_LIST:
  case TypeLeafKind::LF_BUILDINFO:
  case TypeLeafKind::LF_UDT_SRC_LINE:
  case TypeLeafKind::LF_UDT_MOD_SRC_LINE:
    return true;
  default:
    return false;
  }
}

/// Given an arbitrary codeview type, determine if it is an LF_STRUCTURE,
/// LF_CLASS, LF_INTERFACE, LF_UNION.
/// \param CVT CodeView type record to test for an aggregate leaf kind.
/// \returns True if \p CVT is an aggregate type record.
inline bool isAggregate(CVType CVT) {
  switch (CVT.kind()) {
  case LF_STRUCTURE:
  case LF_CLASS:
  case LF_INTERFACE:
  case LF_UNION:
    return true;
  default:
    return false;
  }
}

/// Given an arbitrary codeview type index, determine its size.
/// \param TI Type index whose size in bytes should be computed.
/// \returns The size in bytes of the type referred to by \p TI.
LLVM_ABI uint64_t getSizeInBytesForTypeIndex(TypeIndex TI);

/// Given an arbitrary codeview type, return the type's size in the case
/// of aggregate (LF_STRUCTURE, LF_CLASS, LF_INTERFACE, LF_UNION).
/// \param CVT Aggregate CodeView type record whose size in bytes is sought.
/// \returns The size in bytes of the aggregate type \p CVT.
LLVM_ABI uint64_t getSizeInBytesForTypeRecord(CVType CVT);

} // namespace codeview
} // namespace llvm

#endif
