//===- PDB.h - base header file for creating a PDB reader -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDB_H
#define LLVM_DEBUGINFO_PDB_PDB_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace llvm {
namespace pdb {

class IPDBSession;

/// Create a PDB session by loading debug data from a PDB file.
///
/// \param Type The PDB reader implementation to use.
/// \param Path Path to the PDB file.
/// \param Session On success, set to the newly created session.
///
/// \returns Success, or an error if the PDB could not be loaded.
LLVM_ABI Error loadDataForPDB(PDB_ReaderType Type, StringRef Path,
                              std::unique_ptr<IPDBSession> &Session);

/// Create a PDB session by loading debug data for an executable.
///
/// Locates the PDB associated with the executable (via the native reader) or
/// opens it through DIA, then creates a session over that PDB.
///
/// \param Type The PDB reader implementation to use.
/// \param Path Path to the executable image.
/// \param Session On success, set to the newly created session.
///
/// \returns Success, or an error if the PDB could not be found or loaded.
LLVM_ABI Error loadDataForEXE(PDB_ReaderType Type, StringRef Path,
                              std::unique_ptr<IPDBSession> &Session);

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDB_H
