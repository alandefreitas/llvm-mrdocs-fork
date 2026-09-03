//===- DebugSymbolRVASubsection.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGSYMBOLRVASUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGSYMBOLRVASUBSECTION_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace llvm {

class BinaryStreamReader;

namespace codeview {

/// Read-only view of a CodeView COFF symbol RVA debug subsection.
class DebugSymbolRVASubsectionRef final : public DebugSubsectionRef {
public:
  /// Fixed stream array of little-endian 32-bit relative virtual addresses.
  using ArrayType = FixedStreamArray<support::ulittle32_t>;

  /// Construct an empty, uninitialized symbol RVA subsection reference.
  LLVM_ABI DebugSymbolRVASubsectionRef();

  /// Return true if \p S is a COFF symbol RVA subsection reference.
  ///
  /// \param S Subsection reference to test.
  ///
  /// \returns True if \p S is a COFF symbol RVA subsection reference.
  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::CoffSymbolRVA;
  }

  /// Return an iterator to the first symbol RVA.
  ///
  /// \returns An iterator to the first symbol RVA.
  ArrayType::Iterator begin() const { return RVAs.begin(); }
  /// Return an iterator past the last symbol RVA.
  ///
  /// \returns An iterator past the last symbol RVA.
  ArrayType::Iterator end() const { return RVAs.end(); }

  /// Initialize this view by reading symbol RVAs from \p Reader.
  ///
  /// \param Reader Stream reader positioned at the subsection contents.
  ///
  /// \returns An Error if the subsection could not be parsed.
  LLVM_ABI Error initialize(BinaryStreamReader &Reader);

private:
  ArrayType RVAs;
};

/// Writable CodeView COFF symbol RVA debug subsection.
class LLVM_ABI DebugSymbolRVASubsection final : public DebugSubsection {
public:
  /// Construct an empty COFF symbol RVA subsection.
  DebugSymbolRVASubsection();

  /// Return true if \p S is a COFF symbol RVA subsection.
  ///
  /// \param S Subsection to test.
  ///
  /// \returns True if \p S is a COFF symbol RVA subsection.
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::CoffSymbolRVA;
  }

  /// Write this subsection's serialized form to \p Writer.
  ///
  /// \param Writer Destination stream writer.
  ///
  /// \returns An Error on failure, or success if the write completed.
  Error commit(BinaryStreamWriter &Writer) const override;
  /// Return the serialized size of this subsection in bytes.
  ///
  /// \returns The serialized size of this subsection in bytes.
  uint32_t calculateSerializedSize() const override;

  /// Append relative virtual address \p RVA to this subsection.
  ///
  /// \param RVA Relative virtual address of a COFF symbol.
  void addRVA(uint32_t RVA) { RVAs.push_back(support::ulittle32_t(RVA)); }

private:
  std::vector<support::ulittle32_t> RVAs;
};

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGSYMBOLRVASUBSECTION_H
