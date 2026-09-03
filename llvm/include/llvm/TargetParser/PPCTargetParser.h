//===---- PPCTargetParser - Parser for target features ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise hardware features
// for PPC CPUs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_PPCTARGETPARSER_H
#define LLVM_TARGETPARSER_PPCTARGETPARSER_H

#include "TargetParser.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
/// PowerPC CPU name and target-feature parsing helpers.
namespace PPC {
/// Return true if \p CPU is a recognized PowerPC CPU name.
///
/// \param CPU CPU name to validate.
/// \returns True if \p CPU matches a known PowerPC CPU.
LLVM_ABI bool isValidCPU(StringRef CPU);
/// Append every valid PowerPC CPU name to \p Values.
///
/// \param Values Vector that receives CPU names.
LLVM_ABI void fillValidCPUList(SmallVectorImpl<StringRef> &Values);
/// Append every valid PowerPC tune CPU name to \p Values.
///
/// \param Values Vector that receives tune CPU names.
LLVM_ABI void fillValidTuneCPUList(SmallVectorImpl<StringRef> &Values);

/// Return the normalized PowerPC target CPU name for \p T.
///
/// If \p CPUName is empty or generic, return the default CPU name.
/// If \p CPUName is not empty or generic, return the normalized CPU name.
///
/// \param T Target triple used to select the default CPU.
/// \param CPUName Optional CPU name to normalize; empty selects the default.
/// \returns Normalized target CPU name.
LLVM_ABI StringRef getNormalizedPPCTargetCPU(const Triple &T,
                                             StringRef CPUName = "");

/// Return the normalized PowerPC tune CPU name for \p T.
///
/// \param T Target triple used to select the default tune CPU.
/// \param CPUName Optional CPU name to normalize; empty selects the default.
/// \returns Normalized tune CPU name.
LLVM_ABI StringRef getNormalizedPPCTuneCPU(const Triple &T,
                                           StringRef CPUName = "");

/// Return a canonical PowerPC CPU name for aliases such as pwr10 and power10.
///
/// \param CPUName CPU name that may be an alias.
/// \returns Normalized CPU name.
LLVM_ABI StringRef normalizeCPUName(StringRef CPUName);

/// Return the default target features for PowerPC CPU \p CPUName on triple \p T.
///
/// \param T Target triple.
/// \param CPUName CPU name whose default features are requested.
/// \returns Optional map of feature name to enabled/disabled, or nullopt if
/// none.
LLVM_ABI std::optional<llvm::StringMap<bool>>
getPPCDefaultTargetFeatures(const Triple &T, StringRef CPUName);

/// Return true if \p Name is a recognized PowerPC feature name.
///
/// \param Name Feature name to validate.
/// \returns True if \p Name matches a known feature.
LLVM_ABI bool isValidFeatureName(StringRef Name);
} // namespace PPC
} // namespace llvm

#endif
