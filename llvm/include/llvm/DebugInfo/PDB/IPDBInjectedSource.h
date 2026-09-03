//===- IPDBInjectedSource.h - base class for PDB injected file --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBINJECTEDSOURCE_H
#define LLVM_DEBUGINFO_PDB_IPDBINJECTEDSOURCE_H

#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <string>

namespace llvm {
namespace pdb {
/// IPDBInjectedSource defines an interface for source files injected into a PDB.
///
/// Source files which were injected directly into the PDB file during the
/// compilation process. This is used, for example, to add natvis files to a
/// PDB, but in theory could be used to add arbitrary source code.
class LLVM_ABI IPDBInjectedSource {
public:
  /// Destroy the injected source.
  virtual ~IPDBInjectedSource();

  /// Return the CRC-32 of the original injected file contents.
  ///
  /// \returns The CRC-32 of the original injected file contents.
  virtual uint32_t getCrc32() const = 0;
  /// Return the size in bytes of the original injected source file.
  ///
  /// \returns The size in bytes of the original injected source file.
  virtual uint64_t getCodeByteSize() const = 0;
  /// Return the name of the injected source file.
  ///
  /// \returns The name of the injected source file.
  virtual std::string getFileName() const = 0;
  /// Return the name of the object file associated with this injected source.
  ///
  /// \returns The name of the object file associated with this injected source.
  virtual std::string getObjectFileName() const = 0;
  /// Return the virtual file name of this injected source within the PDB.
  ///
  /// \returns The virtual file name of this injected source within the PDB.
  virtual std::string getVirtualFileName() const = 0;
  /// Return the compression encoding of the injected source contents.
  ///
  /// The returned value depends on the PDB producer, but 0 is guaranteed to
  /// mean "no compression". The enum PDB_SourceCompression lists known return
  /// values.
  ///
  /// \returns The compression encoding of the injected source contents.
  virtual uint32_t getCompression() const = 0;
  /// Return the raw contents of the injected source as stored in the PDB.
  ///
  /// \returns The raw contents of the injected source as stored in the PDB.
  virtual std::string getCode() const = 0;
};
} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_IPDBINJECTEDSOURCE_H
