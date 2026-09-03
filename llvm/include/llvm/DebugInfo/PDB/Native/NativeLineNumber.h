//===- NativeLineNumber.h - Native line number implementation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVELINENUMBER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVELINENUMBER_H

#include "llvm/DebugInfo/CodeView/Line.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"

namespace llvm {
namespace pdb {

class NativeSession;

/// Native PDB implementation of \c IPDBLineNumber.
///
/// Wraps a CodeView line-info record together with section/offset location,
/// length, and source-file and compiland identifiers from a native session.
class LLVM_ABI NativeLineNumber : public IPDBLineNumber {
public:
  /// Construct a native line number from CodeView and location data.
  ///
  /// \param Session The native PDB session used to resolve virtual addresses.
  /// \param Line CodeView line information for the starting and ending lines.
  /// \param ColumnNumber Starting column number associated with this entry.
  /// \param Length Length in bytes of the code covered by this line number.
  /// \param Section Section index of this line number's address.
  /// \param Offset Section-relative address offset of this line number.
  /// \param SrcFileId Source file id associated with this line number.
  /// \param CompilandId Compiland id associated with this line number.
  explicit NativeLineNumber(const NativeSession &Session,
                            const codeview::LineInfo Line,
                            uint32_t ColumnNumber, uint32_t Length,
                            uint32_t Section, uint32_t Offset,
                            uint32_t SrcFileId, uint32_t CompilandId);

  /// Return the starting source line number.
  ///
  /// \returns The starting line number from the CodeView line info.
  uint32_t getLineNumber() const override;

  /// Return the ending source line number.
  ///
  /// \returns The ending line number from the CodeView line info.
  uint32_t getLineNumberEnd() const override;

  /// Return the starting column number.
  ///
  /// \returns The column number supplied at construction.
  uint32_t getColumnNumber() const override;

  /// Return the ending column number.
  ///
  /// \returns The ending column number, or zero when unavailable.
  uint32_t getColumnNumberEnd() const override;

  /// Return the section index of this line number's address.
  ///
  /// \returns The section index supplied at construction.
  uint32_t getAddressSection() const override;

  /// Return the section-relative address offset of this line number.
  ///
  /// \returns The section-relative offset supplied at construction.
  uint32_t getAddressOffset() const override;

  /// Return the relative virtual address of this line number.
  ///
  /// \returns The RVA computed from the section and offset via the session.
  uint32_t getRelativeVirtualAddress() const override;

  /// Return the virtual address of this line number.
  ///
  /// \returns The VA computed from the section and offset via the session.
  uint64_t getVirtualAddress() const override;

  /// Return the length in bytes of the code covered by this line number.
  ///
  /// \returns The length in bytes supplied at construction.
  uint32_t getLength() const override;

  /// Return the source file id associated with this line number.
  ///
  /// \returns The source file id supplied at construction.
  uint32_t getSourceFileId() const override;

  /// Return the compiland id associated with this line number.
  ///
  /// \returns The compiland id supplied at construction.
  uint32_t getCompilandId() const override;

  /// Return whether this line number marks the start of a statement.
  ///
  /// \returns True if the CodeView line info marks a statement start.
  bool isStatement() const override;

private:
  const NativeSession &Session;
  const codeview::LineInfo Line;
  uint32_t ColumnNumber;
  uint32_t Section;
  uint32_t Offset;
  uint32_t Length;
  uint32_t SrcFileId;
  uint32_t CompilandId;
};
} // namespace pdb
} // namespace llvm
#endif
