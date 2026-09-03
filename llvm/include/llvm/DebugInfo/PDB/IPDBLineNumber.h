//===- IPDBLineNumber.h - base interface for PDB line no. info ---*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBLINENUMBER_H
#define LLVM_DEBUGINFO_PDB_IPDBLINENUMBER_H

#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {
namespace pdb {
/// IPDBLineNumber defines an interface used to represent source line number
/// information stored in the PDB.
class LLVM_ABI IPDBLineNumber {
public:
  /// Destroy the line number.
  virtual ~IPDBLineNumber();

  /// Return the starting source line number.
  /// \returns The starting source line number.
  virtual uint32_t getLineNumber() const = 0;
  /// Return the ending source line number.
  /// \returns The ending source line number.
  virtual uint32_t getLineNumberEnd() const = 0;
  /// Return the starting column number.
  /// \returns The starting column number.
  virtual uint32_t getColumnNumber() const = 0;
  /// Return the ending column number.
  /// \returns The ending column number.
  virtual uint32_t getColumnNumberEnd() const = 0;
  /// Return the section index of this line number's address.
  /// \returns The section index of this line number's address.
  virtual uint32_t getAddressSection() const = 0;
  /// Return the section-relative address offset of this line number.
  /// \returns The section-relative address offset of this line number.
  virtual uint32_t getAddressOffset() const = 0;
  /// Return the relative virtual address of this line number.
  /// \returns The relative virtual address of this line number.
  virtual uint32_t getRelativeVirtualAddress() const = 0;
  /// Return the virtual address of this line number.
  /// \returns The virtual address of this line number.
  virtual uint64_t getVirtualAddress() const = 0;
  /// Return the length in bytes of the code covered by this line number.
  /// \returns The length in bytes of the code covered by this line number.
  virtual uint32_t getLength() const = 0;
  /// Return the source file id associated with this line number.
  /// \returns The source file id associated with this line number.
  virtual uint32_t getSourceFileId() const = 0;
  /// Return the compiland id associated with this line number.
  /// \returns The compiland id associated with this line number.
  virtual uint32_t getCompilandId() const = 0;
  /// Return whether this line number marks the start of a statement.
  /// \returns True if this line number marks the start of a statement.
  virtual bool isStatement() const = 0;
};
}
}

#endif
