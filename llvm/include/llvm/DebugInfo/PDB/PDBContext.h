//===-- PDBContext.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBCONTEXT_H
#define LLVM_DEBUGINFO_PDB_PDBCONTEXT_H

#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include <cstdint>
#include <memory>
#include <string>

namespace llvm {

namespace object {
class COFFObjectFile;
} // end namespace object

namespace pdb {

/// DIContext implementation that answers queries from PDB debug information.
///
/// This data structure is the top level entity that deals with PDB debug
/// information parsing. This data structure exists only when there is a
/// need for a transparent interface to different debug information formats
/// (e.g. PDB and DWARF). More control and power over the debug information
/// access can be had by using the PDB interfaces directly.
class LLVM_ABI PDBContext : public DIContext {
public:
  /// Construct a PDB context for \p Object backed by \p PDBSession.
  ///
  /// \param Object COFF object whose addresses are resolved against the PDB.
  /// \param PDBSession Owned PDB session used to answer debug-info queries.
  PDBContext(const object::COFFObjectFile &Object,
             std::unique_ptr<IPDBSession> PDBSession);
  /// Copy construction is deleted; PDBContext is not copyable.
  ///
  /// \param Other Unused; copy construction is not supported.
  PDBContext(PDBContext &Other) = delete;
  /// Copy assignment is deleted; PDBContext is not copyable.
  ///
  /// \param Other Unused; copy assignment is not supported.
  PDBContext &operator=(PDBContext &Other) = delete;

  /// True if \p DICtx is a PDBContext (kind \c CK_PDB).
  ///
  /// \param DICtx Debug-info context to test.
  /// \returns True if \p DICtx has kind \c CK_PDB.
  static bool classof(const DIContext *DICtx) {
    return DICtx->getKind() == CK_PDB;
  }

  /// Dump PDB debug information from this context to \p OS using \p DIDumpOpts.
  ///
  /// \param OS Stream that receives the dump.
  /// \param DIDumpOpts Options controlling which sections and detail to dump.
  void dump(raw_ostream &OS, DIDumpOptions DIDumpOpts) override;

  /// Look up source line info for the instruction at \p Address.
  ///
  /// \param Address Instruction address to look up.
  /// \param Specifier Controls which DILineInfo fields are filled.
  /// \returns Line info for \p Address, or std::nullopt if missing.
  std::optional<DILineInfo> getLineInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;
  /// Look up source line info for a data (variable) address.
  ///
  /// \param Address Data address to look up.
  /// \returns Line info for \p Address, or std::nullopt if missing.
  std::optional<DILineInfo>
  getLineInfoForDataAddress(object::SectionedAddress Address) override;
  /// Return line info for each instruction address in [Address, Address+Size).
  ///
  /// \param Address Start of the address range to query.
  /// \param Size Length in bytes of the address range.
  /// \param Specifier Controls which DILineInfo fields are filled.
  /// \returns A table of addresses and their corresponding line info.
  DILineInfoTable getLineInfoForAddressRange(
      object::SectionedAddress Address, uint64_t Size,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;
  /// Return the inlining stack for the instruction at \p Address.
  ///
  /// \param Address Instruction address to look up.
  /// \param Specifier Controls which DILineInfo fields are filled.
  /// \returns The inlining stack for \p Address, empty if none is available.
  DIInliningInfo getInliningInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;

  /// Return local variables whose live ranges cover \p Address.
  ///
  /// \param Address Code address whose live locals are requested.
  /// \returns Local variables live at \p Address.
  std::vector<DILocal>
  getLocalsForAddress(object::SectionedAddress Address) override;

private:
  std::string getFunctionName(uint64_t Address, DINameKind NameKind) const;
  std::unique_ptr<IPDBSession> Session;
};

} // end namespace pdb

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBCONTEXT_H
