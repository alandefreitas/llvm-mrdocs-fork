//===- TypeDeserializer.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPEDESERIALIZER_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPEDESERIALIZER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecordMapping.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <cstdint>
#include <memory>

namespace llvm {
namespace codeview {

/// Deserializes CodeView type records via TypeVisitorCallbacks.
class TypeDeserializer : public TypeVisitorCallbacks {
  struct MappingInfo {
    explicit MappingInfo(ArrayRef<uint8_t> RecordData)
        : Stream(RecordData, llvm::endianness::little), Reader(Stream),
          Mapping(Reader) {}

    BinaryByteStream Stream;
    BinaryStreamReader Reader;
    TypeRecordMapping Mapping;
  };

public:
  /// Construct an empty type deserializer.
  TypeDeserializer() = default;

  /// Deserialize the CodeView type \p CVT into the typed record \p Record.
  ///
  /// \param CVT Raw CodeView type record whose content is deserialized.
  /// \param Record Destination typed record; Kind is set from \p CVT.
  /// \returns Success, or an error if mapping or field deserialization fails.
  template <typename T> static Error deserializeAs(CVType &CVT, T &Record) {
    Record.Kind = static_cast<TypeRecordKind>(CVT.kind());
    MappingInfo I(CVT.content());
    if (auto EC = I.Mapping.visitTypeBegin(CVT))
      return EC;
    if (auto EC = I.Mapping.visitKnownRecord(CVT, Record))
      return EC;
    if (auto EC = I.Mapping.visitTypeEnd(CVT))
      return EC;
    return Error::success();
  }

  /// Deserialize the type record bytes in \p Data as a value of type \p T.
  ///
  /// \param Data Serialized CodeView type record including the record prefix.
  /// \returns The deserialized record, or an error if deserialization fails.
  template <typename T>
  static Expected<T> deserializeAs(ArrayRef<uint8_t> Data) {
    const RecordPrefix *Prefix =
        reinterpret_cast<const RecordPrefix *>(Data.data());
    TypeRecordKind K =
        static_cast<TypeRecordKind>(uint16_t(Prefix->RecordKind));
    T Record(K);
    CVType CVT(Data);
    if (auto EC = deserializeAs<T>(CVT, Record))
      return std::move(EC);
    return Record;
  }

  /// Begin deserializing the type record \p Record.
  ///
  /// \param Record CodeView type whose content becomes the active mapping.
  /// \returns Success, or an error from the underlying type record mapping.
  Error visitTypeBegin(CVType &Record) override {
    assert(!Mapping && "Already in a type mapping!");
    Mapping = std::make_unique<MappingInfo>(Record.content());
    return Mapping->Mapping.visitTypeBegin(Record);
  }

  /// Begin deserializing the type record \p Record at type index \p Index.
  ///
  /// \param Record CodeView type whose content becomes the active mapping.
  /// \param Index Type index of \p Record in the type stream (unused here).
  /// \returns Success, or an error from the underlying type record mapping.
  Error visitTypeBegin(CVType &Record, TypeIndex Index) override {
    return visitTypeBegin(Record);
  }

  /// Finish deserializing the type record \p Record and clear the mapping.
  ///
  /// \param Record CodeView type whose visit is ending.
  /// \returns Success, or an error from the underlying type record mapping.
  Error visitTypeEnd(CVType &Record) override {
    assert(Mapping && "Not in a type mapping!");
    auto EC = Mapping->Mapping.visitTypeEnd(Record);
    Mapping.reset();
    return EC;
  }

#define TYPE_RECORD(EnumName, EnumVal, Name)                                   \
  Error visitKnownRecord(CVType &CVR, Name##Record &Record) override {         \
    return visitKnownRecordImpl<Name##Record>(CVR, Record);                    \
  }
#define MEMBER_RECORD(EnumName, EnumVal, Name)
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"

private:
  template <typename RecordType>
  Error visitKnownRecordImpl(CVType &CVR, RecordType &Record) {
    return Mapping->Mapping.visitKnownRecord(CVR, Record);
  }

  std::unique_ptr<MappingInfo> Mapping;
};

/// Deserializes CodeView field-list member records from a binary stream.
class FieldListDeserializer : public TypeVisitorCallbacks {
  struct MappingInfo {
    explicit MappingInfo(BinaryStreamReader &R)
        : Reader(R), Mapping(Reader), StartOffset(0) {}

    BinaryStreamReader &Reader;
    TypeRecordMapping Mapping;
    uint32_t StartOffset;
  };

public:
  /// Begin an LF_FIELDLIST visit over members read from \p Reader.
  ///
  /// \param Reader Binary stream positioned at the field-list member payload.
  explicit FieldListDeserializer(BinaryStreamReader &Reader) : Mapping(Reader) {
    RecordPrefix Pre(static_cast<uint16_t>(TypeLeafKind::LF_FIELDLIST));
    CVType FieldList(&Pre, sizeof(Pre));
    consumeError(Mapping.Mapping.visitTypeBegin(FieldList));
  }

  /// End the LF_FIELDLIST visit started by the constructor.
  ~FieldListDeserializer() override {
    RecordPrefix Pre(static_cast<uint16_t>(TypeLeafKind::LF_FIELDLIST));
    CVType FieldList(&Pre, sizeof(Pre));
    consumeError(Mapping.Mapping.visitTypeEnd(FieldList));
  }

  /// Begin deserializing the field-list member \p Record.
  ///
  /// \param Record Member record whose visit is starting.
  /// \returns Success, or an error from the underlying type record mapping.
  Error visitMemberBegin(CVMemberRecord &Record) override {
    Mapping.StartOffset = Mapping.Reader.getOffset();
    return Mapping.Mapping.visitMemberBegin(Record);
  }

  /// Finish deserializing the field-list member \p Record.
  ///
  /// \param Record Member record whose visit is ending.
  /// \returns Success, or an error from the underlying type record mapping.
  Error visitMemberEnd(CVMemberRecord &Record) override {
    if (auto EC = Mapping.Mapping.visitMemberEnd(Record))
      return EC;
    return Error::success();
  }

#define TYPE_RECORD(EnumName, EnumVal, Name)
#define MEMBER_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownMember(CVMemberRecord &CVR, Name##Record &Record) override { \
    return visitKnownMemberImpl<Name##Record>(CVR, Record);                    \
  }
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"

private:
  template <typename RecordType>
  Error visitKnownMemberImpl(CVMemberRecord &CVR, RecordType &Record) {
    if (auto EC = Mapping.Mapping.visitKnownMember(CVR, Record))
      return EC;

    uint32_t EndOffset = Mapping.Reader.getOffset();
    uint32_t RecordLength = EndOffset - Mapping.StartOffset;
    Mapping.Reader.setOffset(Mapping.StartOffset);
    if (auto EC = Mapping.Reader.readBytes(CVR.Data, RecordLength))
      return EC;
    assert(Mapping.Reader.getOffset() == EndOffset);
    return Error::success();
  }
  MappingInfo Mapping;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_TYPEDESERIALIZER_H
