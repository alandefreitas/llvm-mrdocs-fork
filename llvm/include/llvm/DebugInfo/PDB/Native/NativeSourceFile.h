//===- NativeSourceFile.h - Native source file implementation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVESOURCEFILE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVESOURCEFILE_H

#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/PDB/IPDBSourceFile.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace pdb {
class PDBSymbolCompiland;
template <typename ChildType> class IPDBEnumChildren;
class NativeSession;

/// Native PDB implementation of \c IPDBSourceFile.
///
/// Wraps a CodeView file-checksum entry and a source-file id from a native
/// session, resolving the file name through the PDB string table.
class LLVM_ABI NativeSourceFile : public IPDBSourceFile {
public:
  /// Construct a native source file from a checksum entry.
  ///
  /// \param Session The native PDB session used to resolve the file name.
  /// \param FileId Unique identifier for this source file within the session.
  /// \param Checksum CodeView file-checksum entry for this source file.
  explicit NativeSourceFile(NativeSession &Session, uint32_t FileId,
                            const codeview::FileChecksumEntry &Checksum);

  /// Return the path or name of this source file.
  ///
  /// \returns The file name from the PDB string table, or an empty string if
  ///     the string table or name cannot be resolved.
  std::string getFileName() const override;

  /// Return the unique identifier for this source file within the PDB.
  ///
  /// \returns The file id supplied at construction.
  uint32_t getUniqueId() const override;

  /// Return the checksum bytes recorded for this source file.
  ///
  /// \returns The checksum bytes from the CodeView file-checksum entry.
  std::string getChecksum() const override;

  /// Return the checksum algorithm used for this source file.
  ///
  /// \returns The checksum kind from the CodeView file-checksum entry.
  PDB_Checksum getChecksumType() const override;

  /// Return an enumerator over the compilands that contributed this file.
  ///
  /// \returns Always null; enumerating compilands for a source file is not
  ///     yet implemented in the native PDB reader.
  std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>
  getCompilands() const override;

private:
  NativeSession &Session;
  uint32_t FileId;
  const codeview::FileChecksumEntry Checksum;
};
} // namespace pdb
} // namespace llvm
#endif
