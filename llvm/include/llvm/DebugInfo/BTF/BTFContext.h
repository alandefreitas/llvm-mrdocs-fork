//===- BTFContext.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BTFContext interface is used by llvm-objdump tool to print source
// code alongside disassembly.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_BTF_BTFCONTEXT_H
#define LLVM_DEBUGINFO_BTF_BTFCONTEXT_H

#include "llvm/DebugInfo/BTF/BTFParser.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// DIContext that answers queries from BPF Type Format (BTF) debug info.
///
/// Used by llvm-objdump to print source code alongside disassembly. Wraps a
/// BTFParser that reads `.BTF` / `.BTF.ext` sections from an object file.
class LLVM_ABI BTFContext final : public DIContext {
  BTFParser BTF;

public:
  /// Construct an empty BTF context with kind \c CK_BTF.
  BTFContext() : DIContext(CK_BTF) {}

  /// Dump BTF debug information to \p OS using \p DumpOpts.
  ///
  /// Currently a no-op: this override is invoked from objdump's \c --dwarf=?
  /// path, and BTF has no DWARF-style dump implementation yet.
  ///
  /// \param OS Output stream that would receive the dump.
  /// \param DumpOpts Options controlling which debug information to dump.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) override {
    // This function is called from objdump when --dwarf=? option is set.
    // BTF is no DWARF, so ignore this operation for now.
  }

  /// Look up source line information for instruction address \p Address.
  ///
  /// \param Address Sectioned address to query in the BTF line table.
  /// \param Specifier Controls which \c DILineInfo fields are filled.
  /// \returns Line info on an exact BTF match; otherwise \c std::nullopt.
  std::optional<DILineInfo> getLineInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;

  /// Look up source line information for data (variable) address \p Address.
  ///
  /// BTF does not convey data-address line info, so this always returns
  /// \c std::nullopt.
  ///
  /// \param Address Sectioned data address to query.
  /// \returns Always \c std::nullopt; BTF has no data-address line info.
  std::optional<DILineInfo>
  getLineInfoForDataAddress(object::SectionedAddress Address) override;

  /// Return line info for each instruction address in
  /// [\p Address, \p Address + \p Size).
  ///
  /// Currently unused for BTF and always returns an empty table.
  ///
  /// \param Address Start of the address range to query.
  /// \param Size Size in bytes of the address range.
  /// \param Specifier Controls which \c DILineInfo fields are filled.
  /// \returns An empty \c DILineInfoTable; range queries are unused for BTF.
  DILineInfoTable getLineInfoForAddressRange(
      object::SectionedAddress Address, uint64_t Size,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;

  /// Return the inlining stack (caller frames) for \p Address.
  ///
  /// BTF does not convey inlining information, so this always returns an empty
  /// \c DIInliningInfo.
  ///
  /// \param Address Sectioned address to query.
  /// \param Specifier Controls which \c DILineInfo fields are filled.
  /// \returns An empty \c DIInliningInfo; BTF has no inlining data.
  DIInliningInfo getInliningInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;

  /// Return local variables whose live ranges cover \p Address.
  ///
  /// BTF does not convey local-variable information, so this always returns an
  /// empty vector.
  ///
  /// \param Address Sectioned address to query.
  /// \returns An empty vector; BTF has no local-variable live-range data.
  std::vector<DILocal>
  getLocalsForAddress(object::SectionedAddress Address) override;

  /// Create a BTF context by parsing line info from \p Obj.
  ///
  /// \param Obj Object file that may contain `.BTF` / `.BTF.ext` sections.
  /// \param ErrorHandler Callback invoked if BTF parsing fails; the context
  ///        may still be returned with incomplete data.
  /// \returns A new \c BTFContext owning the parsed BTF data from \p Obj.
  static std::unique_ptr<BTFContext> create(
      const object::ObjectFile &Obj,
      std::function<void(Error)> ErrorHandler = WithColor::defaultErrorHandler);
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_BTF_BTFCONTEXT_H
