//===- MultiFormatConfig.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_MULTIFORMATCONFIG_H
#define LLVM_OBJCOPY_MULTIFORMATCONFIG_H

#include "llvm/Support/Error.h"

namespace llvm {
namespace objcopy {

struct CommonConfig;
struct ELFConfig;
struct COFFConfig;
struct MachOConfig;
struct WasmConfig;
struct XCOFFConfig;
struct DXContainerConfig;

/// Interface providing common and per-format configuration for an objcopy
/// operation.
class MultiFormatConfig {
public:
  /// Destroy this multi-format configuration.
  virtual ~MultiFormatConfig() = default;

  /// Return the format-independent configuration.
  /// \returns the format-independent configuration.
  virtual const CommonConfig &getCommonConfig() const = 0;
  /// Return the ELF configuration if the common options are supported for ELF.
  /// \returns the ELF config, or an error if unsupported options are set.
  virtual Expected<const ELFConfig &> getELFConfig() const = 0;
  /// Return the COFF configuration if the common options are supported for COFF.
  /// \returns the COFF config, or an error if unsupported options are set.
  virtual Expected<const COFFConfig &> getCOFFConfig() const = 0;
  /// Return the Mach-O configuration if the common options are supported.
  /// \returns the Mach-O config, or an error if unsupported options are set.
  virtual Expected<const MachOConfig &> getMachOConfig() const = 0;
  /// Return the Wasm configuration if the common options are supported for Wasm.
  /// \returns the Wasm config, or an error if unsupported options are set.
  virtual Expected<const WasmConfig &> getWasmConfig() const = 0;
  /// Return the XCOFF configuration if the common options are supported.
  /// \returns the XCOFF config, or an error if unsupported options are set.
  virtual Expected<const XCOFFConfig &> getXCOFFConfig() const = 0;
  /// Return the DXContainer configuration if the common options are supported.
  /// \returns the DXContainer config, or an error if unsupported options are set.
  virtual Expected<const DXContainerConfig &> getDXContainerConfig() const = 0;
};

} // namespace objcopy
} // namespace llvm

#endif // LLVM_OBJCOPY_MULTIFORMATCONFIG_H
