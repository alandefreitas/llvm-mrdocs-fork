//==- NativeEnumGlobals.h - Native Global Enumerator impl --------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMGLOBALS_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMGLOBALS_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"

#include <vector>

namespace llvm {
namespace pdb {

class NativeSession;

/// Enumerator over global symbols from a native PDB session.
///
/// Walks the globals table and yields \c PDBSymbol instances whose CodeView
/// symbol kinds match the kinds supplied at construction.
class LLVM_ABI NativeEnumGlobals : public IPDBEnumChildren<PDBSymbol> {
public:
  /// Construct an enumerator of globals matching any of \p Kinds.
  ///
  /// \param Session The native PDB session that owns the globals and symbol
  ///     streams.
  /// \param Kinds CodeView symbol kinds to include; other globals are skipped.
  NativeEnumGlobals(NativeSession &Session,
                    std::vector<codeview::SymbolKind> Kinds);

  /// Return the number of matching global symbols.
  ///
  /// \returns The total number of globals whose kinds were accepted.
  uint32_t getChildCount() const override;

  /// Return the matching global symbol at the given zero-based \p Index.
  ///
  /// \param Index Zero-based index of the global to retrieve.
  ///
  /// \returns An owning pointer to the symbol, or null if \p Index is out of
  ///     range.
  std::unique_ptr<PDBSymbol> getChildAtIndex(uint32_t Index) const override;

  /// Advance the enumerator and return the next matching global symbol.
  ///
  /// \returns An owning pointer to the next symbol, or null when exhausted.
  std::unique_ptr<PDBSymbol> getNext() override;

  /// Reset the enumerator to its initial position.
  void reset() override;

private:
  std::vector<uint32_t> MatchOffsets;
  uint32_t Index;
  NativeSession &Session;
};

} // namespace pdb
} // namespace llvm

#endif
