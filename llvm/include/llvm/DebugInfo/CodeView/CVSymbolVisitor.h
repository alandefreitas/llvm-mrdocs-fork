//===- CVSymbolVisitor.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CVSYMBOLVISITOR_H
#define LLVM_DEBUGINFO_CODEVIEW_CVSYMBOLVISITOR_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
class SymbolVisitorCallbacks;

/// Dispatches CodeView symbol records to a \c SymbolVisitorCallbacks sink.
class CVSymbolVisitor {
public:
  /// Options that restrict which symbols \c visitSymbolStreamFiltered visits.
  struct FilterOptions {
    /// Byte offset of the target symbol within the stream, if filtering.
    std::optional<uint32_t> SymbolOffset;
    /// How many enclosing parent scopes to visit around the target symbol.
    std::optional<uint32_t> ParentRecursiveDepth;
    /// How many nested child scope levels to visit under the target symbol.
    std::optional<uint32_t> ChildRecursiveDepth;
  };

  /// Construct a visitor that forwards records to \p Callbacks.
  ///
  /// \param Callbacks Sink invoked for each visited symbol record.
  LLVM_ABI CVSymbolVisitor(SymbolVisitorCallbacks &Callbacks);

  /// Visit a single symbol record without a stream offset.
  ///
  /// \param Record Symbol record to visit.
  ///
  /// \returns An Error if a callback fails, otherwise success.
  LLVM_ABI Error visitSymbolRecord(CVSymbol &Record);

  /// Visit a single symbol record at a known stream offset.
  ///
  /// \param Record Symbol record to visit.
  /// \param Offset Byte offset of \p Record within its symbol stream.
  ///
  /// \returns An Error if a callback fails, otherwise success.
  LLVM_ABI Error visitSymbolRecord(CVSymbol &Record, uint32_t Offset);

  /// Visit every symbol in \p Symbols in order.
  ///
  /// \param Symbols Array of CodeView symbol records to visit.
  ///
  /// \returns The first Error from a callback, or success if all visits
  /// succeed.
  LLVM_ABI Error visitSymbolStream(const CVSymbolArray &Symbols);

  /// Visit every symbol in \p Symbols, reporting offsets from
  /// \p InitialOffset.
  ///
  /// \param Symbols Array of CodeView symbol records to visit.
  /// \param InitialOffset Starting byte offset of the first record in the
  /// stream.
  ///
  /// \returns The first Error from a callback, or success if all visits
  /// succeed.
  LLVM_ABI Error visitSymbolStream(const CVSymbolArray &Symbols,
                                   uint32_t InitialOffset);

  /// Visit a filtered subset of \p Symbols according to \p Filter.
  ///
  /// When \p Filter has no \c SymbolOffset, every symbol is visited. Otherwise
  /// the symbol at that offset is visited along with surrounding parents and
  /// nested children limited by the configured recursive depths.
  ///
  /// \param Symbols Array of CodeView symbol records to filter and visit.
  /// \param Filter Selection and recursion limits for the visitation.
  ///
  /// \returns An Error if the offset is invalid or a callback fails, otherwise
  /// success.
  LLVM_ABI Error visitSymbolStreamFiltered(const CVSymbolArray &Symbols,
                                           const FilterOptions &Filter);

private:
  SymbolVisitorCallbacks &Callbacks;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_CVSYMBOLVISITOR_H
