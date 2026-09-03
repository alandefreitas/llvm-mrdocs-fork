//===-- GsymContext.h --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===/

#ifndef LLVM_DEBUGINFO_GSYM_GSYMCONTEXT_H
#define LLVM_DEBUGINFO_GSYM_GSYMCONTEXT_H

#include "llvm/DebugInfo/DIContext.h"
#include <cstdint>
#include <memory>

namespace llvm {

namespace gsym {

class GsymReader;

/// DIContext that answers symbolication queries from GSYM debug info.
///
/// This data structure is the top level entity that deals with GSYM
/// symbolication. This data structure exists only when there is a need for a
/// transparent interface to different symbolication formats (e.g. GSYM, PDB and
/// DWARF). More control and power over the debug information access can be had
/// by using the GSYM interfaces directly.
class LLVM_ABI GsymContext : public DIContext {
public:
  /// Construct a GSYM context that owns \p Reader.
  ///
  /// \param Reader GSYM reader used to answer symbolication queries.
  GsymContext(std::unique_ptr<GsymReader> Reader);
  /// Destroy this GSYM context and release its owned reader.
  ~GsymContext() override;

  /// Deleted copy constructor; GsymContext is not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  GsymContext(GsymContext &Other) = delete;
  /// Deleted copy assignment; GsymContext is not copyable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  GsymContext &operator=(GsymContext &Other) = delete;

  /// Return true if \p DICtx is a \c GsymContext (\c CK_GSYM).
  ///
  /// \param DICtx Context to test for GSYM kind.
  /// \returns True if \p DICtx has kind \c CK_GSYM.
  static bool classof(const DIContext *DICtx) {
    return DICtx->getKind() == CK_GSYM;
  }

  /// Dump GSYM debug information to \p OS using \p DIDumpOpts.
  ///
  /// Currently a no-op: GSYM has no DWARF-style dump implementation yet.
  ///
  /// \param OS Output stream that would receive the dump.
  /// \param DIDumpOpts Options controlling which debug information to dump.
  void dump(raw_ostream &OS, DIDumpOptions DIDumpOpts) override;

  /// Look up source line information for instruction address \p Address.
  ///
  /// \param Address Sectioned address to query; only
  ///        \c SectionedAddress::UndefSection is supported.
  /// \param Specifier Controls which \c DILineInfo fields are filled.
  /// \returns Line info on a successful GSYM lookup; otherwise \c std::nullopt.
  std::optional<DILineInfo> getLineInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;
  /// Look up source line information for data (variable) address \p Address.
  ///
  /// GSYM does not convey data-address line info, so this always returns
  /// \c std::nullopt.
  ///
  /// \param Address Sectioned data address to query.
  /// \returns Always \c std::nullopt; GSYM has no data-address line info.
  std::optional<DILineInfo>
  getLineInfoForDataAddress(object::SectionedAddress Address) override;
  /// Return line info for each instruction address in
  /// [\p Address, \p Address + \p Size).
  ///
  /// \param Address Start of the address range to query; only
  ///        \c SectionedAddress::UndefSection is supported.
  /// \param Size Size in bytes of the address range.
  /// \param Specifier Controls which \c DILineInfo fields are filled.
  /// \returns Table of address/line-info pairs for instructions in the range.
  DILineInfoTable getLineInfoForAddressRange(
      object::SectionedAddress Address, uint64_t Size,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;
  /// Return the inlining stack (caller frames) for \p Address.
  ///
  /// \param Address Sectioned address to query.
  /// \param Specifier Controls which \c DILineInfo fields are filled.
  /// \returns Inlining stack for \p Address, or empty if none is found.
  DIInliningInfo getInliningInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;

  /// Return local variables whose live ranges cover \p Address.
  ///
  /// GSYM does not convey local-variable information, so this always returns an
  /// empty vector.
  ///
  /// \param Address Sectioned address to query.
  /// \returns Always an empty vector; GSYM has no local-variable info.
  std::vector<DILocal>
  getLocalsForAddress(object::SectionedAddress Address) override;

private:
  const std::unique_ptr<GsymReader> Reader;
};

} // end namespace gsym

} // end namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_GSYMCONTEXT_H
