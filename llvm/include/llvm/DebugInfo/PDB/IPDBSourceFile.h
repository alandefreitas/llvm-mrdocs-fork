//===- IPDBSourceFile.h - base interface for a PDB source file --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBSOURCEFILE_H
#define LLVM_DEBUGINFO_PDB_IPDBSOURCEFILE_H

#include "PDBTypes.h"
#include "llvm/Support/Compiler.h"
#include <memory>
#include <string>

namespace llvm {
class raw_ostream;

namespace pdb {

/// IPDBSourceFile defines an interface used to represent source files whose
/// information are stored in the PDB.
class LLVM_ABI IPDBSourceFile {
public:
  /// Destroy the source file interface.
  virtual ~IPDBSourceFile();

  /// Dump a one-line description of this source file to \p OS.
  ///
  /// \param OS The stream to write to.
  /// \param Indent Number of spaces to indent before the description.
  void dump(raw_ostream &OS, int Indent) const;

  /// Return the path or name of this source file.
  ///
  /// \returns The path or name of this source file.
  virtual std::string getFileName() const = 0;
  /// Return the unique identifier for this source file within the PDB.
  ///
  /// \returns The unique identifier for this source file within the PDB.
  virtual uint32_t getUniqueId() const = 0;
  /// Return the checksum bytes recorded for this source file.
  ///
  /// \returns The checksum bytes recorded for this source file.
  virtual std::string getChecksum() const = 0;
  /// Return the checksum algorithm used for this source file.
  ///
  /// \returns The checksum algorithm used for this source file.
  virtual PDB_Checksum getChecksumType() const = 0;
  /// Return an enumerator over the compilands that contributed this file.
  ///
  /// \returns An enumerator over the compilands that contributed this file.
  virtual std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>
  getCompilands() const = 0;
};
}
}

#endif
