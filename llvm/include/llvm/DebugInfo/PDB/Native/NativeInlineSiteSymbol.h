//===- NativeInlineSiteSymbol.h - info about inline sites -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEINLINESITESYMBOL_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEINLINESITESYMBOL_H

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

class NativeSession;

/// Native PDB representation of an inlined call site.
///
/// Wraps a CodeView \c InlineSiteSym and the parent function's address so
/// callers can resolve the inlined function's name and line numbers.
class LLVM_ABI NativeInlineSiteSymbol : public NativeRawSymbol {
public:
  /// Construct a native inline-site symbol.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id Symbol index ID assigned to this symbol in the cache.
  /// \param Sym CodeView inline-site record describing the inlined call.
  /// \param ParentAddr Virtual address of the enclosing function or frame.
  NativeInlineSiteSymbol(NativeSession &Session, SymIndexId Id,
                         const codeview::InlineSiteSym &Sym,
                         uint64_t ParentAddr);

  /// Destroy the native inline-site symbol.
  ~NativeInlineSiteSymbol() override;

  /// Dump this symbol's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Return the qualified name of the inlined function.
  ///
  /// \returns The inlined function's name, including any class or scope
  ///     qualifier when available, or an empty string on failure.
  std::string getName() const override;
  /// Find inlinee line numbers covering the given VA range.
  ///
  /// \param VA Virtual address of the start of the range.
  /// \param Length Length in bytes of the address range.
  /// \returns An enumerator over matching line-number entries, or null if the
  ///     line information cannot be resolved.
  std::unique_ptr<IPDBEnumLineNumbers>
  findInlineeLinesByVA(uint64_t VA, uint32_t Length) const override;

private:
  const codeview::InlineSiteSym Sym;
  uint64_t ParentAddr;

  void getLineOffset(uint32_t OffsetInFunc, uint32_t &LineOffset,
                     uint32_t &FileOffset) const;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVEINLINESITESYMBOL_H
