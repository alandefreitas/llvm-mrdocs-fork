//===- llvm/TextAPI/Architecture.h - Architecture ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the architecture enum and helper methods.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_ARCHITECTURE_H
#define LLVM_TEXTAPI_ARCHITECTURE_H

#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <utility>

namespace llvm {
class raw_ostream;
class StringRef;
class Triple;

namespace MachO {

/// Defines the architecture slices that are supported by Text-based Stub files.
enum Architecture : uint8_t {
#define ARCHINFO(Arch, Name, Type, SubType, NumBits) AK_##Arch,
#include "llvm/TextAPI/Architecture.def"
#undef ARCHINFO
  /// Unknown or unsupported architecture; must remain last.
  AK_unknown,
};

/// Convert a CPU Type and Subtype pair to an architecture slice.
///
/// \param CPUType The Mach-O CPU type.
/// \param CPUSubType The Mach-O CPU subtype.
/// \return The matching architecture, or AK_unknown if unsupported.
LLVM_ABI Architecture getArchitectureFromCpuType(uint32_t CPUType,
                                                 uint32_t CPUSubType);

/// Convert a name to an architecture slice.
///
/// \param Name The architecture name.
/// \return The matching architecture, or AK_unknown if unrecognized.
LLVM_ABI Architecture getArchitectureFromName(StringRef Name);

/// Convert an architecture slice to a string.
///
/// \param Arch The architecture to convert.
/// \return The architecture name as a string.
LLVM_ABI StringRef getArchitectureName(Architecture Arch);

/// Convert an architecture slice to a CPU Type and Subtype pair.
///
/// \param Arch The architecture to convert.
/// \return The Mach-O CPU type and subtype for \p Arch.
LLVM_ABI std::pair<uint32_t, uint32_t>
getCPUTypeFromArchitecture(Architecture Arch);

/// Convert a target to an architecture slice.
///
/// \param Target The target triple to map.
/// \return The architecture corresponding to \p Target.
LLVM_ABI Architecture mapToArchitecture(const llvm::Triple &Target);

/// Check if architecture is 64 bit.
///
/// \param Arch The architecture to check.
/// \return True if \p Arch is a 64-bit architecture.
LLVM_ABI bool is64Bit(Architecture Arch);

/// Write the architecture name to an output stream.
///
/// \param OS The output stream.
/// \param Arch The architecture to print.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, Architecture Arch);

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_ARCHITECTURE_H
