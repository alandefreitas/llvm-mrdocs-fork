//==- IPDBSectionContrib.h - Interfaces for PDB SectionContribs --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBSECTIONCONTRIB_H
#define LLVM_DEBUGINFO_PDB_IPDBSECTIONCONTRIB_H

#include "PDBTypes.h"

namespace llvm {
namespace pdb {

/// IPDBSectionContrib defines an interface used to represent section
/// contributions whose information are stored in the PDB.
class LLVM_ABI IPDBSectionContrib {
public:
  /// Destroy the section contribution.
  virtual ~IPDBSectionContrib();

  /// Return the compiland that owns this section contribution.
  ///
  /// \returns The compiland that owns this section contribution.
  virtual std::unique_ptr<PDBSymbolCompiland> getCompiland() const = 0;

  /// Return the section index of this contribution's address.
  ///
  /// \returns The section index of this contribution's address.
  virtual uint32_t getAddressSection() const = 0;

  /// Return the section-relative address offset of this contribution.
  ///
  /// \returns The section-relative address offset of this contribution.
  virtual uint32_t getAddressOffset() const = 0;

  /// Return the relative virtual address of this contribution.
  ///
  /// \returns The relative virtual address of this contribution.
  virtual uint32_t getRelativeVirtualAddress() const = 0;

  /// Return the virtual address of this contribution.
  ///
  /// \returns The virtual address of this contribution.
  virtual uint64_t getVirtualAddress() const  = 0;

  /// Return the length in bytes of this contribution.
  ///
  /// \returns The length in bytes of this contribution.
  virtual uint32_t getLength() const = 0;

  /// Return true if this contribution is not paged.
  ///
  /// \returns True if this contribution is not paged.
  virtual bool isNotPaged() const = 0;

  /// Return true if this contribution contains code.
  ///
  /// \returns True if this contribution contains code.
  virtual bool hasCode() const = 0;

  /// Return true if this contribution contains 16-bit code.
  ///
  /// \returns True if this contribution contains 16-bit code.
  virtual bool hasCode16Bit() const = 0;

  /// Return true if this contribution contains initialized data.
  ///
  /// \returns True if this contribution contains initialized data.
  virtual bool hasInitializedData() const = 0;

  /// Return true if this contribution contains uninitialized data.
  ///
  /// \returns True if this contribution contains uninitialized data.
  virtual bool hasUninitializedData() const = 0;

  /// Return true if this contribution is marked as removed.
  ///
  /// \returns True if this contribution is marked as removed.
  virtual bool isRemoved() const = 0;

  /// Return true if this contribution is a COMDAT.
  ///
  /// \returns True if this contribution is a COMDAT.
  virtual bool hasComdat() const = 0;

  /// Return true if this contribution is discardable.
  ///
  /// \returns True if this contribution is discardable.
  virtual bool isDiscardable() const = 0;

  /// Return true if this contribution is not cached.
  ///
  /// \returns True if this contribution is not cached.
  virtual bool isNotCached() const = 0;

  /// Return true if this contribution is shared.
  ///
  /// \returns True if this contribution is shared.
  virtual bool isShared() const = 0;

  /// Return true if this contribution is executable.
  ///
  /// \returns True if this contribution is executable.
  virtual bool isExecutable() const = 0;

  /// Return true if this contribution is readable.
  ///
  /// \returns True if this contribution is readable.
  virtual bool isReadable() const = 0;

  /// Return true if this contribution is writable.
  ///
  /// \returns True if this contribution is writable.
  virtual bool isWritable() const = 0;

  /// Return the CRC-32 checksum of this contribution's data.
  ///
  /// \returns The CRC-32 checksum of this contribution's data.
  virtual uint32_t getDataCrc32() const = 0;

  /// Return the CRC-32 checksum of this contribution's relocations.
  ///
  /// \returns The CRC-32 checksum of this contribution's relocations.
  virtual uint32_t getRelocationsCrc32() const = 0;

  /// Return the SymIndexId of the compiland that owns this contribution.
  ///
  /// \returns The SymIndexId of the compiland that owns this contribution.
  virtual uint32_t getCompilandId() const = 0;
};
}
}

#endif // LLVM_DEBUGINFO_PDB_IPDBSECTIONCONTRIB_H
