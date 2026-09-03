//==- NativeEnumModules.h - Native Module Enumerator impl --------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMMODULES_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMMODULES_H

#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
namespace llvm {
namespace pdb {

class NativeSession;

/// Native enumerator over module (compiland) symbols in a PDB.
class LLVM_ABI NativeEnumModules : public IPDBEnumChildren<PDBSymbol> {
public:
  /// Construct an enumerator over the modules of \p Session.
  ///
  /// \param Session The native PDB session that owns the symbol cache.
  /// \param Index Zero-based starting position for sequential enumeration.
  NativeEnumModules(NativeSession &Session, uint32_t Index = 0);

  /// Return the number of modules available from this enumerator.
  ///
  /// \returns The total number of module (compiland) symbols.
  uint32_t getChildCount() const override;

  /// Return the module at the given zero-based \p Index.
  ///
  /// \param Index Zero-based index of the module to retrieve.
  ///
  /// \returns An owning pointer to the module symbol, or null if \p Index is
  ///     out of range or the module is unavailable.
  std::unique_ptr<PDBSymbol> getChildAtIndex(uint32_t Index) const override;

  /// Advance the enumerator and return the next module.
  ///
  /// \returns An owning pointer to the next module symbol, or null when
  ///     exhausted.
  std::unique_ptr<PDBSymbol> getNext() override;

  /// Reset the enumerator to its initial position.
  void reset() override;

private:
  NativeSession &Session;
  uint32_t Index;
};
}
}

#endif
