//===- PDBSymbolExe.h - Accessors for querying executables in a PDB ----*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLEXE_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLEXE_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class raw_ostream;

namespace pdb {

/// PDB symbol for an executable (Exe), the root of the PDB symbol hierarchy.
///
/// Represents the executable or PDB as a whole and is the authority on the
/// child symbol types that can be enumerated from the file.
/// https://msdn.microsoft.com/en-us/library/4zh9y1kb.aspx
class LLVM_ABI PDBSymbolExe : public PDBSymbol {
private:
  using PDBSymbol::PDBSymbol;
  friend class PDBSymbol;

public:
  /// Symbol tag value identifying this concrete type as an Exe symbol.
  static const PDB_SymType Tag = PDB_SymType::Exe;

  /// Return true if \p S is a PDBSymbolExe.
  ///
  /// \param S Symbol to test.
  /// \return True if \p S is a PDBSymbolExe.
  static bool classof(const PDBSymbol *S) { return S->getSymTag() == Tag; }

  /// Dump this executable symbol using the given dumper.
  ///
  /// \param Dumper Visitor used to format and emit the symbol.
  void dump(PDBSymDumper &Dumper) const override;

  /// Return the PDB age for this executable.
  /// \return The PDB age for this executable.
  FORWARD_SYMBOL_METHOD(getAge)

  /// Return the GUID that uniquely identifies this PDB.
  /// \return The GUID that uniquely identifies this PDB.
  FORWARD_SYMBOL_METHOD(getGuid)

  /// Return true if this PDB was linked with C types.
  /// \return True if this PDB was linked with C types.
  FORWARD_SYMBOL_METHOD(hasCTypes)

  /// Return true if this PDB still contains private symbols.
  /// \return True if this PDB still contains private symbols.
  FORWARD_SYMBOL_METHOD(hasPrivateSymbols)

  /// Return the target machine type for this executable.
  /// \return The target machine type for this executable.
  FORWARD_SYMBOL_METHOD(getMachineType)

  /// Return the name of this executable symbol.
  /// \return The name of this executable symbol.
  FORWARD_SYMBOL_METHOD(getName)

  /// Return the signature value associated with this executable.
  /// \return The signature value associated with this executable.
  FORWARD_SYMBOL_METHOD(getSignature)

  /// Return the path of the PDB or symbols file for this executable.
  /// \return The path of the PDB or symbols file for this executable.
  FORWARD_SYMBOL_METHOD(getSymbolsFileName)

  /// Return the size in bytes of a pointer for this executable's machine.
  ///
  /// Prefers the length of a child pointer type when one is present; otherwise
  /// returns 4 for x86 and 8 for other machine types.
  /// \return The pointer size in bytes for this executable's machine.
  uint32_t getPointerByteSize() const;

private:
  void dumpChildren(raw_ostream &OS, StringRef Label, PDB_SymType ChildType,
                    int Indent) const;
};
} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLEXE_H
