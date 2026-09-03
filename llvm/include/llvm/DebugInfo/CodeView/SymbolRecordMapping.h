//===- SymbolRecordMapping.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLRECORDMAPPING_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLRECORDMAPPING_H

#include "llvm/DebugInfo/CodeView/CodeViewRecordIO.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorCallbacks.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class BinaryStreamReader;
class BinaryStreamWriter;

namespace codeview {
/// Maps CodeView symbol records between binary streams and structured types.
class LLVM_ABI SymbolRecordMapping : public SymbolVisitorCallbacks {
public:
  /// Construct a mapping that reads symbol fields from \p Reader.
  ///
  /// \param Reader Binary stream supplying serialized symbol record data.
  /// \param Container CodeView container that owns the symbol stream.
  explicit SymbolRecordMapping(BinaryStreamReader &Reader,
                               CodeViewContainer Container)
      : IO(Reader), Container(Container) {}

  /// Construct a mapping that writes symbol fields to \p Writer.
  ///
  /// \param Writer Binary stream that receives serialized symbol record data.
  /// \param Container CodeView container that owns the symbol stream.
  explicit SymbolRecordMapping(BinaryStreamWriter &Writer,
                               CodeViewContainer Container)
      : IO(Writer), Container(Container) {}

  /// Begin mapping \p Record by opening an IO record for its payload.
  ///
  /// \param Record Symbol record whose fields will be mapped.
  /// \returns An Error if record setup fails, otherwise success.
  Error visitSymbolBegin(CVSymbol &Record) override;

  /// Finish mapping \p Record by aligning and closing the active IO record.
  ///
  /// \param Record Symbol record whose visit is ending.
  /// \returns An Error if alignment or teardown fails, otherwise success.
  Error visitSymbolEnd(CVSymbol &Record) override;

#define SYMBOL_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownRecord(CVSymbol &CVR, Name &Record) override;
#define SYMBOL_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"

private:
  std::optional<SymbolKind> Kind;

  CodeViewRecordIO IO;
  CodeViewContainer Container;
};
}
}

#endif
