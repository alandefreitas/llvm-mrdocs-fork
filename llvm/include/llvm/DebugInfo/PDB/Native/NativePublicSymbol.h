//===- NativePublicSymbol.h - info about public symbols ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEPUBLICSYMBOL_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEPUBLICSYMBOL_H

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"

namespace llvm {

class raw_ostream;
namespace pdb {
class NativeSession;

/// Native PDB symbol wrapping a CodeView public symbol record.
///
/// Exposes name and address accessors for an entry from the PDB publics
/// stream.
class LLVM_ABI NativePublicSymbol : public NativeRawSymbol {
public:
  /// Construct a native public symbol for \p Sym.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id The symbol index id assigned to this public symbol.
  /// \param Sym The CodeView public symbol record to wrap.
  NativePublicSymbol(NativeSession &Session, SymIndexId Id,
                     const codeview::PublicSym32 &Sym);

  /// Destroy the native public symbol.
  ~NativePublicSymbol() override;

  /// Dump this symbol's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Return the section-relative address offset of this symbol.
  ///
  /// \returns The offset within the symbol's section.
  uint32_t getAddressOffset() const override;

  /// Return the section index of this symbol's address.
  ///
  /// \returns The section index containing this symbol.
  uint32_t getAddressSection() const override;

  /// Return the name of this symbol.
  ///
  /// \returns The public symbol name from the CodeView record.
  std::string getName() const override;

  /// Return the relative virtual address of this symbol.
  ///
  /// \returns The RVA of this public symbol.
  uint32_t getRelativeVirtualAddress() const override;

  /// Return the virtual address of this symbol.
  ///
  /// \returns The absolute virtual address of this public symbol.
  uint64_t getVirtualAddress() const override;

protected:
  /// The underlying CodeView public symbol record.
  const codeview::PublicSym32 Sym;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVEPUBLICSYMBOL_H
