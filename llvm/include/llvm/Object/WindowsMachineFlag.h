//===- WindowsMachineFlag.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Functions for implementing the /machine: flag.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_WINDOWSMACHINEFLAG_H
#define LLVM_OBJECT_WINDOWSMACHINEFLAG_H

#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

class StringRef;
namespace COFF {
enum MachineTypes : unsigned;
}

/// Return a user-readable string for a COFF machine type.
///
/// Supported values are ARMNT, ARM64, AMD64, and I386. Other MachineTypes
/// values must not be passed in.
///
/// \param MT COFF machine type to convert.
/// \return A string naming the machine type (e.g. "x64", "x86", "arm", "arm64").
LLVM_ABI StringRef machineToStr(COFF::MachineTypes MT);

/// Map a \c /machine: flag argument to a COFF machine type.
///
/// Only returns ARMNT, ARM64, AMD64, I386, or IMAGE_FILE_MACHINE_UNKNOWN.
///
/// \param S The \c /machine: argument string to map.
/// \return The matching COFF machine type, or IMAGE_FILE_MACHINE_UNKNOWN.
LLVM_ABI COFF::MachineTypes getMachineType(StringRef S);

/// Map a COFF machine type to a Triple architecture.
///
/// \param machine COFF machine type value (typically a \c MachineTypes
///        enumerator).
/// \return The corresponding Triple architecture, or UnknownArch if the
///         machine type is not recognized.
template <typename T> Triple::ArchType getMachineArchType(T machine) {
  switch (machine) {
  case COFF::IMAGE_FILE_MACHINE_I386:
    return llvm::Triple::ArchType::x86;
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    return llvm::Triple::ArchType::x86_64;
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    return llvm::Triple::ArchType::thumb;
  case COFF::IMAGE_FILE_MACHINE_ARM64:
  case COFF::IMAGE_FILE_MACHINE_ARM64EC:
  case COFF::IMAGE_FILE_MACHINE_ARM64X:
    return llvm::Triple::ArchType::aarch64;
  case COFF::IMAGE_FILE_MACHINE_R4000:
    return llvm::Triple::ArchType::mipsel;
  default:
    return llvm::Triple::ArchType::UnknownArch;
  }
}

} // namespace llvm

#endif
