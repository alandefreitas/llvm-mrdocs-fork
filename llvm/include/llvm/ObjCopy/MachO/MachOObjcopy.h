//===- MachOObjcopy.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJCOPY_MACHO_MACHOOBJCOPY_H
#define LLVM_OBJCOPY_MACHO_MACHOOBJCOPY_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class Error;
class raw_ostream;

namespace object {
class MachOObjectFile;
class MachOUniversalBinary;
} // end namespace object

namespace objcopy {
struct CommonConfig;
struct MachOConfig;
class MultiFormatConfig;

/// Mach-O-specific object-file copying and stripping operations.
namespace macho {
/// Apply the transformations described by \p Config and \p MachOConfig to
/// \p In and writes the result into \p Out.
/// \param Config Common objcopy configuration options.
/// \param MachOConfig Mach-O-specific configuration options.
/// \param In Input Mach-O object file to transform.
/// \param Out Output stream to write the transformed binary to.
/// \returns any Error encountered whilst performing the operation.
LLVM_ABI Error executeObjcopyOnBinary(const CommonConfig &Config,
                                      const MachOConfig &MachOConfig,
                                      object::MachOObjectFile &In,
                                      raw_ostream &Out);

/// Apply the transformations described by \p Config to \p In and writes the
/// result into \p Out.
/// \param Config Multi-format objcopy configuration options.
/// \param In Input Mach-O universal binary to transform.
/// \param Out Output stream to write the transformed binary to.
/// \returns any Error encountered whilst performing the operation.
LLVM_ABI Error executeObjcopyOnMachOUniversalBinary(
    const MultiFormatConfig &Config, const object::MachOUniversalBinary &In,
    raw_ostream &Out);

} // end namespace macho
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_OBJCOPY_MACHO_MACHOOBJCOPY_H
