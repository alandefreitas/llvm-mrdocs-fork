//===-- TypeDumpVisitor.h - CodeView type info dumper -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPEDUMPVISITOR_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPEDUMPVISITOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class ScopedPrinter;

namespace codeview {
class TypeIndex;
struct CVMemberRecord;
struct MemberAttributes;

class TypeCollection;

/// Dumper for CodeView type streams found in COFF object files and PDB files.
class LLVM_ABI TypeDumpVisitor : public TypeVisitorCallbacks {
public:
  /// Construct a dumper for CodeView type records.
  ///
  /// \param TpiTypes Type collection used to resolve type indices.
  /// \param W Printer used to emit the dumped type output, or null to suppress
  ///        printing.
  /// \param PrintRecordBytes If true, also print the raw bytes of each record.
  TypeDumpVisitor(TypeCollection &TpiTypes, ScopedPrinter *W,
                  bool PrintRecordBytes)
      : W(W), PrintRecordBytes(PrintRecordBytes), TpiTypes(TpiTypes) {}

  /// Set the IPI type collection used when resolving item indices.
  ///
  /// When dumping types from an IPI stream in a PDB, a type index may refer to
  /// a type or an item ID. The dumper will lookup the "name" of the index in
  /// the item database if appropriate. If ItemDB is null, it will use TypeDB,
  /// which is correct when dumping types from an object file (/Z7).
  ///
  /// \param Types Item (IPI) type collection to consult for item index names.
  void setIpiTypes(TypeCollection &Types) { IpiTypes = &Types; }

  /// Print the type index \p TI under the field name \p FieldName.
  ///
  /// \param FieldName Label printed before the type index value.
  /// \param TI Type index to resolve and print.
  void printTypeIndex(StringRef FieldName, TypeIndex TI) const;

  /// Print the item index \p TI under the field name \p FieldName.
  ///
  /// \param FieldName Label printed before the item index value.
  /// \param TI Item index to resolve and print.
  void printItemIndex(StringRef FieldName, TypeIndex TI) const;

  /// Action to take on unknown types. By default, they are ignored.
  ///
  /// \param Record Unknown CodeView type record to dump.
  ///
  /// \returns An Error if visitation should abort, otherwise success.
  Error visitUnknownType(CVType &Record) override;
  /// Dump an unrecognized field-list member record.
  ///
  /// \param Record Unknown member record to dump.
  ///
  /// \returns An Error if visitation should abort, otherwise success.
  Error visitUnknownMember(CVMemberRecord &Record) override;

  /// Paired begin/end actions for all types. Receives all record data,
  /// including the fixed-length record prefix.
  ///
  /// \param Record CodeView type whose visit is starting.
  ///
  /// \returns An Error if visitation should abort, otherwise success.
  Error visitTypeBegin(CVType &Record) override;
  /// Begin dumping a type record at a known type index.
  ///
  /// \param Record CodeView type whose visit is starting.
  /// \param Index Type index of \p Record in the type stream.
  ///
  /// \returns An Error if visitation should abort, otherwise success.
  Error visitTypeBegin(CVType &Record, TypeIndex Index) override;
  /// Finish dumping a type record.
  ///
  /// \param Record CodeView type whose visit is ending.
  ///
  /// \returns An Error if visitation should abort, otherwise success.
  Error visitTypeEnd(CVType &Record) override;
  /// Begin dumping a field-list member record.
  ///
  /// \param Record Member record whose visit is starting.
  ///
  /// \returns An Error if visitation should abort, otherwise success.
  Error visitMemberBegin(CVMemberRecord &Record) override;
  /// Finish dumping a field-list member record.
  ///
  /// \param Record Member record whose visit is ending.
  ///
  /// \returns An Error if visitation should abort, otherwise success.
  Error visitMemberEnd(CVMemberRecord &Record) override;

#define TYPE_RECORD(EnumName, EnumVal, Name)                                   \
  Error visitKnownRecord(CVType &CVR, Name##Record &Record) override;
#define MEMBER_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownMember(CVMemberRecord &CVR, Name##Record &Record) override;
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"

private:
  void printMemberAttributes(MemberAttributes Attrs);
  void printMemberAttributes(MemberAccess Access, MethodKind Kind,
                             MethodOptions Options);

  /// Get the database of indices for the stream that we are dumping. If ItemDB
  /// is set, then we must be dumping an item (IPI) stream. This will also
  /// always get the appropriate DB for printing item names.
  TypeCollection &getSourceTypes() const {
    return IpiTypes ? *IpiTypes : TpiTypes;
  }

  ScopedPrinter *W;

  bool PrintRecordBytes = false;

  TypeCollection &TpiTypes;
  TypeCollection *IpiTypes = nullptr;
};

} // end namespace codeview
} // end namespace llvm

#endif
