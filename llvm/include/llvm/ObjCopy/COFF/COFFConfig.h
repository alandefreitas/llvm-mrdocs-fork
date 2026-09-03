//===- COFFConfig.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_COFF_COFFCONFIG_H
#define LLVM_OBJCOPY_COFF_COFFCONFIG_H

#include <optional>

namespace llvm {
/// Tools and configuration for copying and transforming object files.
namespace objcopy {

/// COFF-specific configuration for copying or stripping a single file.
struct COFFConfig {
  /// Optional Windows subsystem to write into the PE header.
  std::optional<unsigned> Subsystem;
  /// Optional major subsystem version to write into the PE header.
  std::optional<unsigned> MajorSubsystemVersion;
  /// Optional minor subsystem version to write into the PE header.
  std::optional<unsigned> MinorSubsystemVersion;
};

} // namespace objcopy
} // namespace llvm

#endif // LLVM_OBJCOPY_COFF_COFFCONFIG_H
