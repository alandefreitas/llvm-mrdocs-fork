//===- SymbolSerializer.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLSERIALIZER_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLSERIALIZER_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/SymbolRecordMapping.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorCallbacks.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>

namespace llvm {
namespace codeview {

/// Serializes CodeView symbol records via SymbolVisitorCallbacks.
class LLVM_ABI SymbolSerializer : public SymbolVisitorCallbacks {
  BumpPtrAllocator &Storage;
  // Since this is a fixed size buffer, use a stack allocated buffer.  This
  // yields measurable performance increase over the repeated heap allocations
  // when serializing many independent records via writeOneSymbol.
  std::array<uint8_t, MaxRecordLength> RecordBuffer;
  MutableBinaryByteStream Stream;
  BinaryStreamWriter Writer;
  SymbolRecordMapping Mapping;
  std::optional<SymbolKind> CurrentSymbol;

  Error writeRecordPrefix(SymbolKind Kind) {
    RecordPrefix Prefix;
    Prefix.RecordKind = Kind;
    Prefix.RecordLen = 0;
    if (auto EC = Writer.writeObject(Prefix))
      return EC;
    return Error::success();
  }

public:
  /// Construct a symbol serializer that allocates stable record bytes in
  /// \p Storage for \p Container.
  ///
  /// \param Storage Allocator used for stable copies of serialized records.
  /// \param Container CodeView container that controls record alignment rules.
  SymbolSerializer(BumpPtrAllocator &Storage, CodeViewContainer Container);

  /// Serialize a single symbol \p Sym into a stable \c CVSymbol in \p Storage.
  ///
  /// \param Sym Typed symbol record to serialize.
  /// \param Storage Allocator used for the stable serialized record bytes.
  /// \param Container CodeView container that controls record alignment rules.
  /// \returns The serialized symbol record whose data lives in \p Storage.
  template <typename SymType>
  static CVSymbol writeOneSymbol(SymType &Sym, BumpPtrAllocator &Storage,
                                 CodeViewContainer Container) {
    RecordPrefix Prefix{uint16_t(Sym.Kind)};
    CVSymbol Result(&Prefix, sizeof(Prefix));
    SymbolSerializer Serializer(Storage, Container);
    consumeError(Serializer.visitSymbolBegin(Result));
    consumeError(Serializer.visitKnownRecord(Result, Sym));
    consumeError(Serializer.visitSymbolEnd(Result));
    return Result;
  }

  /// Begin serializing the symbol record \p Record into the scratch buffer.
  ///
  /// \param Record CodeView symbol whose kind seeds the record prefix.
  /// \returns Success, or an error from writing the prefix or starting mapping.
  Error visitSymbolBegin(CVSymbol &Record) override;

  /// Finish serializing \p Record, fix its length, and copy bytes to storage.
  ///
  /// \param Record CodeView symbol whose visit is ending; RecordData is updated.
  /// \returns Success, or an error from mapping, length fixup, or allocation.
  Error visitSymbolEnd(CVSymbol &Record) override;

#define SYMBOL_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownRecord(CVSymbol &CVR, Name &Record) override {               \
    return visitKnownRecordImpl(CVR, Record);                                  \
  }
#define SYMBOL_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"

private:
  template <typename RecordKind>
  Error visitKnownRecordImpl(CVSymbol &CVR, RecordKind &Record) {
    return Mapping.visitKnownRecord(CVR, Record);
  }
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SYMBOLSERIALIZER_H
