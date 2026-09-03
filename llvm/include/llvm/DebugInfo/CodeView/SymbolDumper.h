//===-- SymbolDumper.h - CodeView symbol info dumper ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLDUMPER_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLDUMPER_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/SymbolDumpDelegate.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

#include <memory>
#include <utility>

namespace llvm {
class ScopedPrinter;

namespace codeview {
class TypeCollection;

/// Dumper for CodeView symbol streams found in COFF object files and PDB files.
class CVSymbolDumper {
public:
  /// Construct a dumper for CodeView symbol records.
  ///
  /// \param W Printer used to emit the dumped symbol output.
  /// \param Types Type collection used to resolve type indices in symbols.
  /// \param Container Whether the symbols come from an object file or a PDB.
  /// \param ObjDelegate Delegate for object-file-specific symbol dump hooks.
  /// \param CPU Initial compilation CPU type used until an S_COMPILE* record
  ///        updates it.
  /// \param PrintRecordBytes If true, also print the raw bytes of each record.
  CVSymbolDumper(ScopedPrinter &W, TypeCollection &Types,
                 CodeViewContainer Container,
                 std::unique_ptr<SymbolDumpDelegate> ObjDelegate, CPUType CPU,
                 bool PrintRecordBytes)
      : W(W), Types(Types), Container(Container),
        ObjDelegate(std::move(ObjDelegate)), CompilationCPUType(CPU),
        PrintRecordBytes(PrintRecordBytes) {}

  /// Dump one CodeView symbol record.
  ///
  /// Returns false if there was a type parsing error, and true otherwise. This
  /// should be called in order, since the dumper maintains state about previous
  /// records which are necessary for cross type references.
  ///
  /// \param Record Symbol record to dump.
  /// \returns Success, or an Error if the record cannot be dumped.
  LLVM_ABI Error dump(CVRecord<SymbolKind> &Record);

  /// Dumps the type records in Data. Returns false if there was a type stream
  /// parse error, and true otherwise.
  ///
  /// \param Symbols Array of symbol records to dump.
  /// \returns Success, or an Error if the symbol stream cannot be parsed.
  LLVM_ABI Error dump(const CVSymbolArray &Symbols);

  /// Return the compilation CPU type observed while dumping symbols.
  ///
  /// \returns The compilation CPU type observed while dumping.
  CPUType getCompilationCPUType() const { return CompilationCPUType; }

private:
  ScopedPrinter &W;
  TypeCollection &Types;
  CodeViewContainer Container;
  std::unique_ptr<SymbolDumpDelegate> ObjDelegate;
  CPUType CompilationCPUType;
  bool PrintRecordBytes;
};
} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SYMBOLDUMPER_H
