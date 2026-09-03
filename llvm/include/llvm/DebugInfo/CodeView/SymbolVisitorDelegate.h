//===-- SymbolVisitorDelegate.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLVISITORDELEGATE_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLVISITORDELEGATE_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm {

class BinaryStreamReader;

namespace codeview {

class DebugStringTableSubsectionRef;

/// Interface that supplies context while visiting CodeView symbol records.
class SymbolVisitorDelegate {
public:
  /// Destroy the symbol visitor delegate.
  virtual ~SymbolVisitorDelegate() = default;

  /// Return the byte offset of the current symbol record within its stream.
  ///
  /// \param Reader Stream reader positioned at the current symbol record.
  ///
  /// \returns The byte offset of the record relative to the start of the
  /// containing section or stream.
  virtual uint32_t getRecordOffset(BinaryStreamReader Reader) = 0;

  /// Resolve a source file name from a CodeView file checksum table offset.
  ///
  /// \param FileOffset Offset into the file checksums subsection.
  ///
  /// \returns The corresponding source file name.
  virtual StringRef getFileNameForFileOffset(uint32_t FileOffset) = 0;

  /// Return the CodeView string table used to resolve name references.
  ///
  /// \returns A reference to the debug string table subsection.
  virtual DebugStringTableSubsectionRef getStringTable() = 0;
};

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SYMBOLVISITORDELEGATE_H
