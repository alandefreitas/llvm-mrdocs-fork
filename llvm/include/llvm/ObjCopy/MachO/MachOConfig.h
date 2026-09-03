//===- MachOConfig.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_MACHO_MACHOCONFIG_H
#define LLVM_OBJCOPY_MACHO_MACHOCONFIG_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include <optional>
#include <vector>

namespace llvm {
namespace objcopy {

/// Mach-O specific configuration for copying/stripping a single file.
struct MachOConfig {
  /// RPath entries to append to the binary.
  std::vector<StringRef> RPathToAdd;
  /// RPath entries to prepend to the binary.
  std::vector<StringRef> RPathToPrepend;
  /// Mapping from old RPath entries to their replacements.
  DenseMap<StringRef, StringRef> RPathsToUpdate;
  /// Mapping from old install names to their replacements.
  DenseMap<StringRef, StringRef> InstallNamesToUpdate;
  /// RPath entries to remove from the binary.
  DenseSet<StringRef> RPathsToRemove;

  /// Shared library install name set by install-name-tool's -id option.
  std::optional<StringRef> SharedLibId;

  /// Segment names to remove when those segments are empty.
  DenseSet<StringRef> EmptySegmentsToRemove;

  /// Whether to strip Swift symbols from the binary.
  bool StripSwiftSymbols = false;
  /// Whether to keep undefined symbols.
  bool KeepUndefined = false;

  /// Whether to remove all RPath entries (--delete_all_rpaths).
  bool RemoveAllRpaths = false;
};

} // namespace objcopy
} // namespace llvm

#endif // LLVM_OBJCOPY_MACHO_MACHOCONFIG_H
