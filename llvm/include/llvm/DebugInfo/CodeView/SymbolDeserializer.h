//===- SymbolDeserializer.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLDESERIALIZER_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLDESERIALIZER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolRecordMapping.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorCallbacks.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorDelegate.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
class SymbolVisitorDelegate;

/// Deserializes CodeView symbol records into structured \c SymbolRecord types.
class SymbolDeserializer : public SymbolVisitorCallbacks {
  struct MappingInfo {
    MappingInfo(ArrayRef<uint8_t> RecordData, CodeViewContainer Container)
        : Stream(RecordData, llvm::endianness::little), Reader(Stream),
          Mapping(Reader, Container) {}

    BinaryByteStream Stream;
    BinaryStreamReader Reader;
    SymbolRecordMapping Mapping;
  };

public:
  /// Deserialize \p Symbol into the existing structured record \p Record.
  ///
  /// Uses an object-file container and does not require record alignment
  /// because nothing follows the single record being decoded.
  ///
  /// \param Symbol Raw CodeView symbol record to deserialize.
  /// \param Record Structured symbol record filled on success.
  /// \returns An Error if deserialization fails, otherwise success.
  template <typename T> static Error deserializeAs(CVSymbol Symbol, T &Record) {
    // If we're just deserializing one record, then don't worry about alignment
    // as there's nothing that comes after.
    SymbolDeserializer S(nullptr, CodeViewContainer::ObjectFile);
    if (auto EC = S.visitSymbolBegin(Symbol))
      return EC;
    if (auto EC = S.visitKnownRecord(Symbol, Record))
      return EC;
    if (auto EC = S.visitSymbolEnd(Symbol))
      return EC;
    return Error::success();
  }

  /// Deserialize \p Symbol into a newly constructed structured record of type
  /// \c T.
  ///
  /// \param Symbol Raw CodeView symbol record to deserialize.
  /// \returns The deserialized record, or an Error if deserialization fails.
  template <typename T> static Expected<T> deserializeAs(CVSymbol Symbol) {
    T Record(static_cast<SymbolRecordKind>(Symbol.kind()));
    if (auto EC = deserializeAs<T>(Symbol, Record))
      return std::move(EC);
    return Record;
  }

  /// Construct a deserializer for symbols in \p Container.
  ///
  /// \param Delegate Optional visitor delegate used for record offsets; may be
  /// null.
  /// \param Container CodeView container that owns the symbol stream.
  explicit SymbolDeserializer(SymbolVisitorDelegate *Delegate,
                              CodeViewContainer Container)
      : Delegate(Delegate), Container(Container) {}

  /// Begin visiting \p Record at stream offset \p Offset.
  ///
  /// \param Record Symbol record whose payload will be mapped.
  /// \param Offset Byte offset of \p Record within its symbol stream.
  /// \returns An Error if mapping setup fails, otherwise success.
  Error visitSymbolBegin(CVSymbol &Record, uint32_t Offset) override {
    return visitSymbolBegin(Record);
  }

  /// Begin visiting \p Record by installing a mapping over its content.
  ///
  /// \param Record Symbol record whose payload will be mapped.
  /// \returns An Error if mapping setup fails, otherwise success.
  Error visitSymbolBegin(CVSymbol &Record) override {
    assert(!Mapping && "Already in a symbol mapping!");
    Mapping = std::make_unique<MappingInfo>(Record.content(), Container);
    return Mapping->Mapping.visitSymbolBegin(Record);
  }

  /// Finish visiting \p Record and tear down the active mapping.
  ///
  /// \param Record Symbol record whose visit is ending.
  /// \returns An Error if mapping teardown fails, otherwise success.
  Error visitSymbolEnd(CVSymbol &Record) override {
    assert(Mapping && "Not in a symbol mapping!");
    auto EC = Mapping->Mapping.visitSymbolEnd(Record);
    Mapping.reset();
    return EC;
  }

#define SYMBOL_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownRecord(CVSymbol &CVR, Name &Record) override {               \
    return visitKnownRecordImpl(CVR, Record);                                  \
  }
#define SYMBOL_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"

private:
  template <typename T> Error visitKnownRecordImpl(CVSymbol &CVR, T &Record) {

    Record.RecordOffset =
        Delegate ? Delegate->getRecordOffset(Mapping->Reader) : 0;
    if (auto EC = Mapping->Mapping.visitKnownRecord(CVR, Record))
      return EC;
    return Error::success();
  }

  SymbolVisitorDelegate *Delegate;
  CodeViewContainer Container;
  std::unique_ptr<MappingInfo> Mapping;
};
}
}

#endif
