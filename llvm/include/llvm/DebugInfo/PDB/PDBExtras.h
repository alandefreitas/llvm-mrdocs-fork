//===- PDBExtras.h - helper functions and classes for PDBs ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBEXTRAS_H
#define LLVM_DEBUGINFO_PDB_PDBEXTRAS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>

namespace llvm {

namespace pdb {

/// Map from PDB symbol type tags to occurrence counts.
using TagStats = DenseMap<PDB_SymType, int>;

/// Write the name of a \c PDB_VariantType enumerator to \p OS.
///
/// \param OS The stream to write to.
/// \param Value The variant type enumerator to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const PDB_VariantType &Value);

/// Write a calling-convention name (e.g. \c __cdecl) to \p OS.
///
/// \param OS The stream to write to.
/// \param Conv The calling convention to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const PDB_CallingConv &Conv);

/// Write the name of a \c PDB_BuiltinType enumerator to \p OS.
///
/// \param OS The stream to write to.
/// \param Type The builtin type enumerator to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const PDB_BuiltinType &Type);

/// Write the name of a \c PDB_DataKind enumerator to \p OS.
///
/// \param OS The stream to write to.
/// \param Data The data-kind enumerator to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const PDB_DataKind &Data);

/// Write a CPU register name to \p OS.
///
/// \param OS The stream to write to.
/// \param CpuReg The CPU and register pair to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const llvm::codeview::CPURegister &CpuReg);

/// Write the name of a \c PDB_LocType enumerator to \p OS.
///
/// \param OS The stream to write to.
/// \param Loc The location-type enumerator to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const PDB_LocType &Loc);

/// Write the name of a CodeView thunk ordinal to \p OS.
///
/// \param OS The stream to write to.
/// \param Thunk The thunk ordinal to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const codeview::ThunkOrdinal &Thunk);

/// Write the name of a \c PDB_Checksum enumerator to \p OS.
///
/// \param OS The stream to write to.
/// \param Checksum The checksum algorithm enumerator to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const PDB_Checksum &Checksum);

/// Write the name of a \c PDB_Lang enumerator to \p OS.
///
/// \param OS The stream to write to.
/// \param Lang The source-language enumerator to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const PDB_Lang &Lang);

/// Write the name of a \c PDB_SymType enumerator to \p OS.
///
/// \param OS The stream to write to.
/// \param Tag The symbol-tag enumerator to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const PDB_SymType &Tag);

/// Write the name of a \c PDB_MemberAccess enumerator to \p OS.
///
/// \param OS The stream to write to.
/// \param Access The member-access enumerator to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const PDB_MemberAccess &Access);

/// Write the name of a \c PDB_UdtType enumerator to \p OS.
///
/// \param OS The stream to write to.
/// \param Type The UDT-kind enumerator to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const PDB_UdtType &Type);

/// Write the name of a \c PDB_Machine enumerator to \p OS.
///
/// \param OS The stream to write to.
/// \param Machine The target-machine enumerator to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const PDB_Machine &Machine);

/// Write the active payload of a \c Variant to \p OS.
///
/// \param OS The stream to write to.
/// \param Value The variant whose typed value is printed.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Variant &Value);

/// Write a dotted major.minor.build version string to \p OS.
///
/// \param OS The stream to write to.
/// \param Version The version components to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const VersionInfo &Version);

/// Write symbol-tag counts from \p Stats to \p OS.
///
/// \param OS The stream to write to.
/// \param Stats Map of symbol tags to occurrence counts.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const TagStats &Stats);

/// Write the name of a PDB source-compression method to \p OS.
///
/// \param OS The stream to write to.
/// \param Compression Integer encoding of a \c PDB_SourceCompression value.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &dumpPDBSourceCompression(raw_ostream &OS,
                                               uint32_t Compression);

/// Print an indented \c Name: Value field on a new line of a symbol dump.
///
/// \param OS The stream to write to.
/// \param Name The field label to print before the colon.
/// \param Value The field value to stream after the colon.
/// \param Indent Number of spaces to indent before \p Name.
template <typename T>
void dumpSymbolField(raw_ostream &OS, StringRef Name, T Value, int Indent) {
  OS << "\n";
  OS.indent(Indent);
  OS << Name << ": " << Value;
}

} // end namespace pdb

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBEXTRAS_H
