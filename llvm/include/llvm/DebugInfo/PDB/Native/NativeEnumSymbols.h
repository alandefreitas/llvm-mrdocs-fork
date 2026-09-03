//==- NativeEnumSymbols.h - Native Symbols Enumerator impl -------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMSYMBOLS_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMSYMBOLS_H

#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

#include <vector>

namespace llvm {
namespace pdb {

class NativeSession;

/// Enumerator over a fixed list of symbols from a native PDB session.
///
/// Yields \c PDBSymbol instances for the symbol index IDs supplied at
/// construction, resolving each through the session symbol cache.
class LLVM_ABI NativeEnumSymbols : public IPDBEnumChildren<PDBSymbol> {
public:
  /// Construct an enumerator over the given list of symbol index IDs.
  ///
  /// \param Session The native PDB session used to resolve symbols by ID.
  /// \param Symbols Symbol index IDs to enumerate, in order.
  NativeEnumSymbols(NativeSession &Session, std::vector<SymIndexId> Symbols);

  /// Return the number of symbols available from this enumerator.
  ///
  /// \returns The total number of symbol index IDs supplied at construction.
  uint32_t getChildCount() const override;

  /// Return the symbol at the given zero-based \p Index.
  ///
  /// \param Index Zero-based index of the symbol to retrieve.
  ///
  /// \returns An owning pointer to the symbol, or null if \p Index is out of
  ///     range.
  std::unique_ptr<PDBSymbol> getChildAtIndex(uint32_t Index) const override;

  /// Advance the enumerator and return the next symbol.
  ///
  /// \returns An owning pointer to the next symbol, or null when exhausted.
  std::unique_ptr<PDBSymbol> getNext() override;

  /// Reset the enumerator to its initial position.
  void reset() override;

private:
  std::vector<SymIndexId> Symbols;
  uint32_t Index;
  NativeSession &Session;
};

} // namespace pdb
} // namespace llvm

#endif
