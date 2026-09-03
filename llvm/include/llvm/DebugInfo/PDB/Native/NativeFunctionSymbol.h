//===- NativeFunctionSymbol.h - info about function symbols -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEFUNCTIONSYMBOL_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEFUNCTIONSYMBOL_H

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
class raw_ostream;
namespace pdb {

class NativeSession;

/// Native PDB symbol wrapping a CodeView procedure symbol record.
///
/// Exposes name, address, length, and inline-frame accessors for a function
/// from a module debug stream.
class LLVM_ABI NativeFunctionSymbol : public NativeRawSymbol {
public:
  /// Construct a native function symbol for \p Sym.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id The symbol index id assigned to this function symbol.
  /// \param Sym The CodeView procedure symbol record to wrap.
  /// \param RecordOffset Offset of this record in the module symbol stream.
  NativeFunctionSymbol(NativeSession &Session, SymIndexId Id,
                       const codeview::ProcSym &Sym, uint32_t RecordOffset);

  /// Destroy the native function symbol.
  ~NativeFunctionSymbol() override;

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
  /// \returns The offset within the section from the procedure symbol record.
  uint32_t getAddressOffset() const override;

  /// Return the section index of this symbol's address.
  ///
  /// \returns The section index from the procedure symbol record.
  uint32_t getAddressSection() const override;

  /// Return the name of this symbol.
  ///
  /// \returns The function name from the procedure symbol record.
  std::string getName() const override;

  /// Return the length in bytes of this function's code.
  ///
  /// \returns The code size in bytes from the procedure symbol record.
  uint64_t getLength() const override;

  /// Return the relative virtual address of this symbol.
  ///
  /// \returns The RVA computed from the procedure symbol's section and offset.
  uint32_t getRelativeVirtualAddress() const override;

  /// Return the virtual address of this symbol.
  ///
  /// \returns The absolute virtual address of this function.
  uint64_t getVirtualAddress() const override;

  /// Find inline frames at the given virtual address.
  ///
  /// \param VA Virtual address to search.
  /// \returns An enumerator over matching inline frame symbols.
  std::unique_ptr<IPDBEnumSymbols>
  findInlineFramesByVA(uint64_t VA) const override;

protected:
  /// The underlying CodeView procedure symbol record.
  const codeview::ProcSym Sym;

  /// Offset of this record in the module symbol stream.
  uint32_t RecordOffset = 0;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVEFUNCTIONSYMBOL_H
