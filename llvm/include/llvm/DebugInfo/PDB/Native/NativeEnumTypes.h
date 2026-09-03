//==- NativeEnumTypes.h - Native Type Enumerator impl ------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMTYPES_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMTYPES_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"

#include <vector>

namespace llvm {
namespace codeview {
class LazyRandomTypeCollection;
}
namespace pdb {

class NativeSession;

/// Enumerator over type symbols from a native PDB session.
///
/// Yields \c PDBSymbol instances for CodeView types that either match the
/// type leaf kinds supplied at construction or appear in a precomputed list
/// of type indices.
class LLVM_ABI NativeEnumTypes : public IPDBEnumChildren<PDBSymbol> {
public:
  /// Construct an enumerator of types matching any of \p Kinds.
  ///
  /// Scans \p TypeCollection and retains non-forward-ref types whose leaf
  /// kinds are in \p Kinds, including \c LF_MODIFIER wrappers of such types.
  ///
  /// \param Session The native PDB session that owns the type and symbol
  ///     caches.
  /// \param TypeCollection The CodeView type collection to scan for matches.
  /// \param Kinds CodeView type leaf kinds to include; other types are
  ///     skipped.
  NativeEnumTypes(NativeSession &Session,
                  codeview::LazyRandomTypeCollection &TypeCollection,
                  std::vector<codeview::TypeLeafKind> Kinds);

  /// Construct an enumerator over the given type indices.
  ///
  /// \param Session The native PDB session that owns the type and symbol
  ///     caches.
  /// \param Indices Precomputed type indices to enumerate in order.
  NativeEnumTypes(NativeSession &Session,
                  std::vector<codeview::TypeIndex> Indices);

  /// Return the number of matching type symbols.
  ///
  /// \returns The total number of types selected at construction.
  uint32_t getChildCount() const override;

  /// Return the matching type symbol at the given zero-based \p Index.
  ///
  /// \param Index Zero-based index of the type to retrieve.
  ///
  /// \returns An owning pointer to the symbol, or null if \p Index is out of
  ///     range.
  std::unique_ptr<PDBSymbol> getChildAtIndex(uint32_t Index) const override;

  /// Advance the enumerator and return the next matching type symbol.
  ///
  /// \returns An owning pointer to the next symbol, or null when exhausted.
  std::unique_ptr<PDBSymbol> getNext() override;

  /// Reset the enumerator to its initial position.
  void reset() override;

private:
  std::vector<codeview::TypeIndex> Matches;
  uint32_t Index;
  NativeSession &Session;
};

} // namespace pdb
} // namespace llvm

#endif
