//===- DebugSymbolsSubsection.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGSYMBOLSSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGSYMBOLSSUBSECTION_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
/// Read-only view of a CodeView Symbols debug subsection.
class DebugSymbolsSubsectionRef final : public DebugSubsectionRef {
public:
  /// Construct an empty, uninitialized symbols subsection reference.
  DebugSymbolsSubsectionRef()
      : DebugSubsectionRef(DebugSubsectionKind::Symbols) {}

  /// Return true if \p S is a Symbols subsection reference.
  ///
  /// \param S Subsection reference to test.
  ///
  /// \returns True if \p S is a Symbols subsection reference.
  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::Symbols;
  }

  /// Initialize this view from symbol records read via \p Reader.
  ///
  /// \param Reader Reader positioned at the start of the symbols data.
  ///
  /// \returns An Error on failure, or success if initialization succeeded.
  LLVM_ABI Error initialize(BinaryStreamReader Reader);

  /// Return an iterator to the first symbol record.
  ///
  /// \returns An iterator to the first symbol record.
  CVSymbolArray::Iterator begin() const { return Records.begin(); }
  /// Return an iterator past the last symbol record.
  ///
  /// \returns An iterator past the last symbol record.
  CVSymbolArray::Iterator end() const { return Records.end(); }

private:
  CVSymbolArray Records;
};

/// Writable CodeView Symbols debug subsection.
class LLVM_ABI DebugSymbolsSubsection final : public DebugSubsection {
public:
  /// Construct an empty writable symbols subsection.
  DebugSymbolsSubsection() : DebugSubsection(DebugSubsectionKind::Symbols) {}
  /// Return true if \p S is a Symbols subsection.
  ///
  /// \param S Subsection to test.
  ///
  /// \returns True if \p S is a Symbols subsection.
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::Symbols;
  }

  /// Return the serialized size of this subsection in bytes.
  ///
  /// \returns The serialized size of this subsection in bytes.
  uint32_t calculateSerializedSize() const override;
  /// Write this subsection's serialized form to \p Writer.
  ///
  /// \param Writer Destination stream writer.
  ///
  /// \returns An Error on failure, or success if the write completed.
  Error commit(BinaryStreamWriter &Writer) const override;

  /// Append \p Symbol to this subsection.
  ///
  /// \param Symbol Symbol record to add.
  void addSymbol(CVSymbol Symbol);

private:
  uint32_t Length = 0;
  std::vector<CVSymbol> Records;
};
}
}

#endif
