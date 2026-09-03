//===- NativeExeSymbol.h - native impl for PDBSymbolExe ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEEXESYMBOL_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEEXESYMBOL_H

#include "llvm/DebugInfo/CodeView/GUID.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

class NativeSession;

class DbiStream;

/// Native PDB implementation of the executable (Exe) root symbol.
///
/// Represents the PDB as a whole and is the authority on the various child
/// symbol types that can be enumerated from the file.
class LLVM_ABI NativeExeSymbol : public NativeRawSymbol {
  // EXE symbol is the authority on the various symbol types.
  DbiStream *Dbi = nullptr;

public:
  /// Construct an Exe symbol for the given native PDB session.
  ///
  /// \param Session The native PDB session that owns this symbol.
  /// \param Id The symbol index id assigned to this Exe symbol.
  NativeExeSymbol(NativeSession &Session, SymIndexId Id);

  /// Find child symbols of the given type under this Exe symbol.
  ///
  /// \param Type Symbol tag of children to enumerate.
  ///
  /// \returns An enumerator over matching child symbols, or null if \p Type
  ///     is not supported for this Exe symbol.
  std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type) const override;

  /// Return the PDB age from the info stream.
  ///
  /// \returns The age value, or zero if the info stream is unavailable.
  uint32_t getAge() const override;

  /// Return the path of the PDB file associated with this Exe symbol.
  ///
  /// \returns The file path of the underlying PDB.
  std::string getSymbolsFileName() const override;

  /// Return the GUID from the PDB info stream.
  ///
  /// \returns The PDB GUID, or a zero GUID if the info stream is unavailable.
  codeview::GUID getGuid() const override;

  /// Return whether the PDB was linked with C types (/debug:ctypes).
  ///
  /// \returns True if the DBI stream reports C types; false otherwise.
  bool hasCTypes() const override;

  /// Return whether the PDB still contains private symbols.
  ///
  /// \returns True if the PDB is not stripped; false otherwise.
  bool hasPrivateSymbols() const override;
};

} // namespace pdb
} // namespace llvm

#endif
