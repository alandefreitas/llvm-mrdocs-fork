//===- ELFConfig.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_ELF_ELFCONFIG_H
#define LLVM_OBJCOPY_ELF_ELFCONFIG_H

#include "llvm/ObjCopy/CommonConfig.h"
#include "llvm/Object/ELFTypes.h"

namespace llvm {
namespace objcopy {

/// Description of an ELF note to remove, as specified by --remove-note.
struct RemoveNoteInfo {
  /// Owner name of the note to remove; empty matches any owner.
  StringRef Name;
  /// Type ID of the note to remove.
  uint32_t TypeId;
};

/// ELF-specific configuration for copying or stripping a single file.
struct ELFConfig {
  /// Default ELF symbol visibility applied to newly added symbols.
  uint8_t NewSymbolVisibility = (uint8_t)ELF::STV_DEFAULT;

  /// Matchers for symbols whose visibility should be set, with the new value.
  std::vector<std::pair<NameMatcher, uint8_t>> SymbolsToSetVisibility;

  /// Expression that maps the input ELF entry point to the output entry point.
  ///
  /// The input parameter is an entry point address in the input ELF file. The
  /// entry address in the output file is calculated with
  /// EntryExpr(input_address), when either --set-start or --change-start is
  /// used.
  std::function<uint64_t(uint64_t)> EntryExpr;

  /// Allow section removals that leave dangling cross-section references.
  bool AllowBrokenLinks = false;
  /// Keep STT_FILE symbols when stripping other symbols.
  bool KeepFileSymbols = false;
  /// Localize symbols with hidden or internal visibility.
  bool LocalizeHidden = false;
  /// Verify that added .note sections are well-formed.
  bool VerifyNoteSections = true;

  /// Notes to remove as specified by --remove-note.
  SmallVector<RemoveNoteInfo, 0> NotesToRemove;
};

} // namespace objcopy
} // namespace llvm

#endif // LLVM_OBJCOPY_ELF_ELFCONFIG_H
