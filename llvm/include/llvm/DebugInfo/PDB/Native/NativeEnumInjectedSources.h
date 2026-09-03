//==- NativeEnumInjectedSources.cpp - Native Injected Source Enumerator --*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMINJECTEDSOURCES_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMINJECTEDSOURCES_H

#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBInjectedSource.h"
#include "llvm/DebugInfo/PDB/Native/InjectedSourceStream.h"

namespace llvm {
namespace pdb {

class InjectedSourceStream;
class PDBFile;
class PDBStringTable;

/// Native enumerator over injected source files stored in a PDB.
class LLVM_ABI NativeEnumInjectedSources
    : public IPDBEnumChildren<IPDBInjectedSource> {
public:
  /// Construct an enumerator over the injected sources of \p File.
  ///
  /// \param File The PDB file that owns the injected source data streams.
  /// \param IJS The injected source stream to enumerate.
  /// \param Strings The PDB string table used to resolve source names.
  NativeEnumInjectedSources(PDBFile &File, const InjectedSourceStream &IJS,
                            const PDBStringTable &Strings);

  /// Return the number of injected sources available from this enumerator.
  ///
  /// \returns The total number of injected source entries.
  uint32_t getChildCount() const override;

  /// Return the injected source at the given zero-based \p Index.
  ///
  /// \param Index Zero-based index of the injected source to retrieve.
  ///
  /// \returns An owning pointer to the injected source, or null if \p Index
  ///     is out of range.
  std::unique_ptr<IPDBInjectedSource>
  getChildAtIndex(uint32_t Index) const override;

  /// Advance the enumerator and return the next injected source.
  ///
  /// \returns An owning pointer to the next injected source, or null when
  ///     exhausted.
  std::unique_ptr<IPDBInjectedSource> getNext() override;

  /// Reset the enumerator to its initial position.
  void reset() override;

private:
  PDBFile &File;
  const InjectedSourceStream &Stream;
  const PDBStringTable &Strings;
  InjectedSourceStream::const_iterator Cur;
};

} // namespace pdb
} // namespace llvm

#endif
