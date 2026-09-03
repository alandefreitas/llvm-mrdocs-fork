//===- CVTypeVisitor.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CVTYPEVISITOR_H
#define LLVM_DEBUGINFO_CODEVIEW_CVTYPEVISITOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
class TypeIndex;
class TypeCollection;
class TypeVisitorCallbacks;
struct CVMemberRecord;

/// How type or member record bytes are supplied during visitation.
enum VisitorDataSource {
  /// Record bytes are passed into the visitation function and must be
  /// deserialized before continuing through the pipeline.
  VDS_BytesPresent,
  /// Record bytes are not present; the visitor callbacks must supply them.
  VDS_BytesExternal
};

/// Visit a single type record at a known type index.
///
/// \param Record Type record to visit.
/// \param Index Type index of \p Record in its type stream or collection.
/// \param Callbacks Sink invoked for the visited type record.
/// \param Source Whether record bytes are already present or must be supplied
/// by \p Callbacks.
///
/// \returns An Error if a callback fails, otherwise success.
LLVM_ABI Error visitTypeRecord(CVType &Record, TypeIndex Index,
                               TypeVisitorCallbacks &Callbacks,
                               VisitorDataSource Source = VDS_BytesPresent);

/// Visit a single type record without a type index.
///
/// \param Record Type record to visit.
/// \param Callbacks Sink invoked for the visited type record.
/// \param Source Whether record bytes are already present or must be supplied
/// by \p Callbacks.
///
/// \returns An Error if a callback fails, otherwise success.
LLVM_ABI Error visitTypeRecord(CVType &Record, TypeVisitorCallbacks &Callbacks,
                               VisitorDataSource Source = VDS_BytesPresent);

/// Visit a single member record.
///
/// \param Record Member record to visit.
/// \param Callbacks Sink invoked for the visited member record.
/// \param Source Whether record bytes are already present or must be supplied
/// by \p Callbacks.
///
/// \returns An Error if a callback fails, otherwise success.
LLVM_ABI Error visitMemberRecord(CVMemberRecord Record,
                                 TypeVisitorCallbacks &Callbacks,
                                 VisitorDataSource Source = VDS_BytesPresent);

/// Visit a single member record from its leaf kind and raw bytes.
///
/// \param Kind Leaf kind of the member record.
/// \param Record Serialized bytes of the member record payload.
/// \param Callbacks Sink invoked for the visited member record.
///
/// \returns An Error if a callback fails, otherwise success.
LLVM_ABI Error visitMemberRecord(TypeLeafKind Kind, ArrayRef<uint8_t> Record,
                                 TypeVisitorCallbacks &Callbacks);

/// Visit every member record in a field-list byte stream.
///
/// \param FieldList Serialized field-list bytes containing member records.
/// \param Callbacks Sink invoked for each visited member record.
///
/// \returns The first Error from a callback, or success if all visits succeed.
LLVM_ABI Error visitMemberRecordStream(ArrayRef<uint8_t> FieldList,
                                       TypeVisitorCallbacks &Callbacks);

/// Visit every type record in \p Types in order.
///
/// \param Types Array of CodeView type records to visit.
/// \param Callbacks Sink invoked for each visited type record.
/// \param Source Whether record bytes are already present or must be supplied
/// by \p Callbacks.
///
/// \returns The first Error from a callback, or success if all visits succeed.
LLVM_ABI Error visitTypeStream(const CVTypeArray &Types,
                               TypeVisitorCallbacks &Callbacks,
                               VisitorDataSource Source = VDS_BytesPresent);

/// Visit every type record in the range \p Types.
///
/// \param Types Range of CodeView type records to visit.
/// \param Callbacks Sink invoked for each visited type record.
///
/// \returns The first Error from a callback, or success if all visits succeed.
LLVM_ABI Error visitTypeStream(CVTypeRange Types,
                               TypeVisitorCallbacks &Callbacks);

/// Visit every type record in the collection \p Types.
///
/// \param Types Type collection whose records are visited in order.
/// \param Callbacks Sink invoked for each visited type record.
///
/// \returns The first Error from a callback, or success if all visits succeed.
LLVM_ABI Error visitTypeStream(TypeCollection &Types,
                               TypeVisitorCallbacks &Callbacks);

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_CVTYPEVISITOR_H
