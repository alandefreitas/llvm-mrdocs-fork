//===- ConfigManager.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_CONFIGMANAGER_H
#define LLVM_OBJCOPY_CONFIGMANAGER_H

#include "llvm/ObjCopy/COFF/COFFConfig.h"
#include "llvm/ObjCopy/CommonConfig.h"
#include "llvm/ObjCopy/DXContainer/DXContainerConfig.h"
#include "llvm/ObjCopy/ELF/ELFConfig.h"
#include "llvm/ObjCopy/MachO/MachOConfig.h"
#include "llvm/ObjCopy/MultiFormatConfig.h"
#include "llvm/ObjCopy/XCOFF/XCOFFConfig.h"
#include "llvm/ObjCopy/wasm/WasmConfig.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace objcopy {

/// Holds common and per-format configuration for a single objcopy operation.
struct LLVM_ABI ConfigManager : public MultiFormatConfig {
  /// Destroy this configuration manager.
  ~ConfigManager() override = default;

  /// Return the format-independent configuration.
  /// \returns the format-independent configuration shared by all object formats.
  const CommonConfig &getCommonConfig() const override { return Common; }

  /// Return the ELF configuration if the common options are supported for ELF.
  /// \returns the ELF config, or an error if unsupported options are set.
  Expected<const ELFConfig &> getELFConfig() const override;

  /// Return the COFF configuration if the common options are supported for COFF.
  /// \returns the COFF config, or an error if unsupported options are set.
  Expected<const COFFConfig &> getCOFFConfig() const override;

  /// Return the Mach-O configuration if the common options are supported.
  /// \returns the Mach-O config, or an error if unsupported options are set.
  Expected<const MachOConfig &> getMachOConfig() const override;

  /// Return the Wasm configuration if the common options are supported for Wasm.
  /// \returns the Wasm config, or an error if unsupported options are set.
  Expected<const WasmConfig &> getWasmConfig() const override;

  /// Return the XCOFF configuration if the common options are supported.
  /// \returns the XCOFF config, or an error if unsupported options are set.
  Expected<const XCOFFConfig &> getXCOFFConfig() const override;

  /// Return the DXContainer configuration if the common options are supported.
  /// \returns the DXContainer config, or an error if unsupported options are set.
  Expected<const DXContainerConfig &> getDXContainerConfig() const override;

  /// Format-independent configuration shared by all object formats.
  CommonConfig Common;
  /// ELF-specific configuration for copying or stripping.
  ELFConfig ELF;
  /// COFF-specific configuration for copying or stripping.
  COFFConfig COFF;
  /// Mach-O-specific configuration for copying or stripping.
  MachOConfig MachO;
  /// Wasm-specific configuration for copying or stripping.
  WasmConfig Wasm;
  /// XCOFF-specific configuration for copying or stripping.
  XCOFFConfig XCOFF;
  /// DXContainer-specific configuration for copying or stripping.
  DXContainerConfig DXContainer;
};

} // namespace objcopy
} // namespace llvm

#endif // LLVM_OBJCOPY_CONFIGMANAGER_H
