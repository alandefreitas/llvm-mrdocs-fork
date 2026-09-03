//===- llvm/TextAPI/Platform.h - Platform -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the Platforms supported by Tapi and helpers.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TEXTAPI_PLATFORM_H
#define LLVM_TEXTAPI_PLATFORM_H

#include "llvm/ADT/SmallSet.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/VersionTuple.h"

namespace llvm {
namespace MachO {

/// Set of Mach-O platform types.
using PlatformSet = SmallSet<PlatformType, 3>;

/// Set of Mach-O platform and minimum deployment version pairs.
using PlatformVersionSet = SmallSet<std::pair<PlatformType, VersionTuple>, 3>;

/// Map a platform to its device or simulator variant.
///
/// \param Platform The platform to map.
/// \param WantSim Whether to select the simulator variant when available.
/// \return The mapped platform type.
LLVM_ABI PlatformType mapToPlatformType(PlatformType Platform, bool WantSim);

/// Map a target triple to a Mach-O platform type.
///
/// \param Target The target triple to map.
/// \return The Mach-O platform type for \p Target.
LLVM_ABI PlatformType mapToPlatformType(const Triple &Target);

/// Map a list of target triples to the set of platforms they represent.
///
/// \param Targets The target triples to map.
/// \return The set of platforms corresponding to \p Targets.
LLVM_ABI PlatformSet mapToPlatformSet(ArrayRef<Triple> Targets);

/// Convert a platform type to its marketing name.
///
/// \param Platform The platform to convert.
/// \return The marketing name for \p Platform.
LLVM_ABI StringRef getPlatformName(PlatformType Platform);

/// Convert a platform name to a platform type.
///
/// \param Name The platform name to convert.
/// \return The platform type matching \p Name.
LLVM_ABI PlatformType getPlatformFromName(StringRef Name);

/// Build an OS and environment name string for a platform.
///
/// \param Platform The platform to describe.
/// \param Version Optional version string appended after the OS name.
/// \return The OS and environment name string.
LLVM_ABI std::string getOSAndEnvironmentName(PlatformType Platform,
                                             std::string Version = "");

/// Map a triple to a supported OS version for TextAPI.
///
/// \param Triple The target triple whose OS version is mapped.
/// \return The supported OS version for \p Triple.
LLVM_ABI VersionTuple mapToSupportedOSVersion(const Triple &Triple);

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_PLATFORM_H
