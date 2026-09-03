//===- TypeRecordMapping.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPERECORDMAPPING_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPERECORDMAPPING_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/CodeViewRecordIO.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <optional>

namespace llvm {
class BinaryStreamReader;
class BinaryStreamWriter;

namespace codeview {
class TypeIndex;
struct CVMemberRecord;

/// Maps CodeView type and member records through a shared CodeViewRecordIO.
class LLVM_ABI TypeRecordMapping : public TypeVisitorCallbacks {
public:
  /// Construct a mapping that deserializes type records from \p Reader.
  ///
  /// \param Reader Binary stream reader supplying record bytes.
  explicit TypeRecordMapping(BinaryStreamReader &Reader) : IO(Reader) {}
  /// Construct a mapping that serializes type records into \p Writer.
  ///
  /// \param Writer Binary stream writer receiving record bytes.
  explicit TypeRecordMapping(BinaryStreamWriter &Writer) : IO(Writer) {}
  /// Construct a mapping that streams type records via \p Streamer.
  ///
  /// \param Streamer Assembly streamer used to emit record bytes and comments.
  explicit TypeRecordMapping(CodeViewRecordStreamer &Streamer) : IO(Streamer) {}

  /// Bring base-class visitTypeBegin overloads into scope.
  using TypeVisitorCallbacks::visitTypeBegin;
  /// Begin mapping the type record \p Record.
  ///
  /// \param Record CodeView type whose fields are about to be mapped.
  /// \returns Success, or an error if the record cannot be started.
  Error visitTypeBegin(CVType &Record) override;
  /// Begin mapping the type record \p Record at type index \p Index.
  ///
  /// \param Record CodeView type whose fields are about to be mapped.
  /// \param Index Type index of \p Record in the type stream.
  /// \returns Success, or an error if the record cannot be started.
  Error visitTypeBegin(CVType &Record, TypeIndex Index) override;
  /// Finish mapping the type record \p Record.
  ///
  /// \param Record CodeView type whose visit is ending.
  /// \returns Success, or an error if the record cannot be ended.
  Error visitTypeEnd(CVType &Record) override;

  /// Begin mapping the field-list member record \p Record.
  ///
  /// \param Record Member record whose fields are about to be mapped.
  /// \returns Success, or an error if the member cannot be started.
  Error visitMemberBegin(CVMemberRecord &Record) override;
  /// Finish mapping the field-list member record \p Record.
  ///
  /// \param Record Member record whose visit is ending.
  /// \returns Success, or an error if the member cannot be ended.
  Error visitMemberEnd(CVMemberRecord &Record) override;

#define TYPE_RECORD(EnumName, EnumVal, Name)                                   \
  Error visitKnownRecord(CVType &CVR, Name##Record &Record) override;
#define MEMBER_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownMember(CVMemberRecord &CVR, Name##Record &Record) override;
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"

private:
  std::optional<TypeLeafKind> TypeKind;
  std::optional<TypeLeafKind> MemberKind;

  CodeViewRecordIO IO;
};
}
}

#endif
