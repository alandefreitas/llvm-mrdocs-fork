//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a target parser to recognise Xtensa hardware features.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_XTENSATARGETPARSER_H
#define LLVM_TARGETPARSER_XTENSATARGETPARSER_H

#include "llvm/TargetParser/Triple.h"

namespace llvm {
class StringRef;

/// Xtensa CPU and hardware-feature name parsing helpers.
namespace Xtensa {

/// Xtensa CPU kind identifiers.
///
/// Values are generated from XtensaTargetParser.def:
/// - CK_INVALID: Unrecognized CPU.
/// - CK_GENERIC: Generic Xtensa CPU with no optional features.
/// - CK_ESP8266: Espressif ESP8266.
/// - CK_ESP32: Espressif ESP32.
/// - CK_ESP32S2: Espressif ESP32-S2.
enum CPUKind : unsigned {
#define XTENSA_CPU(ENUM, NAME, FEATURES) CK_##ENUM,
#include "XtensaTargetParser.def"
};

/// Xtensa hardware-feature bit flags selectable on a CPU.
enum XtensaFeatureKind : uint64_t {
  XF_INVALID = 0,                   ///< Invalid or unrecognized feature set.
  XF_NONE = 1,                      ///< No optional hardware features.
  XF_FP = 1 << 1,                   ///< Single-precision floating-point.
  XF_WINDOWED = 1 << 2,             ///< Windowed register option.
  XF_BOOLEAN = 1 << 3,              ///< Boolean register option.
  XF_DENSITY = 1 << 4,              ///< Code density option.
  XF_LOOP = 1 << 5,                 ///< Zero-overhead loop option.
  XF_SEXT = 1 << 6,                 ///< Sign-extend instructions.
  XF_NSA = 1 << 7,                  ///< Normalize-shift-amount instructions.
  XF_CLAMPS = 1 << 8,               ///< Clamp instructions.
  XF_MINMAX = 1 << 9,               ///< Min/max instructions.
  XF_MAC16 = 1 << 10,               ///< 16-bit multiply-accumulate option.
  XF_MUL32 = 1 << 11,               ///< 32-bit multiply option.
  XF_MUL32HIGH = 1 << 12,           ///< 32-bit multiply high option.
  XF_DIV32 = 1 << 13,               ///< 32-bit divide option.
  XF_MUL16 = 1 << 14,               ///< 16-bit multiply option.
  XF_DFPACCEL = 1 << 15,            ///< Double-precision FP accelerator.
  XF_S32C1I = 1 << 16,              ///< 32-bit compare-and-swap instruction.
  XF_THREADPTR = 1 << 17,           ///< Thread-pointer register.
  XF_EXTENDEDL32R = 1 << 18,        ///< Extended L32R instruction.
  XF_DATACACHE = 1 << 19,           ///< Data cache option.
  XF_DEBUG = 1 << 20,               ///< Debug option.
  XF_EXCEPTION = 1 << 21,           ///< Exception handling option.
  XF_HIGHPRIINTERRUPTS = 1 << 22,   ///< High-priority interrupts.
  XF_HIGHPRIINTERRUPTSLEVEL3 = 1 << 23, ///< High-priority interrupts level 3.
  XF_HIGHPRIINTERRUPTSLEVEL4 = 1 << 24, ///< High-priority interrupts level 4.
  XF_HIGHPRIINTERRUPTSLEVEL5 = 1 << 25, ///< High-priority interrupts level 5.
  XF_HIGHPRIINTERRUPTSLEVEL6 = 1 << 26, ///< High-priority interrupts level 6.
  XF_HIGHPRIINTERRUPTSLEVEL7 = 1 << 27, ///< High-priority interrupts level 7.
  XF_COPROCESSOR = 1 << 28,         ///< Coprocessor option.
  XF_INTERRUPT = 1 << 29,           ///< Interrupt option.
  XF_RVECTOR = 1 << 30,             ///< Relocatable vector option.
  XF_TIMERS1 = 1ULL << 31,          ///< One timer.
  XF_TIMERS2 = 1ULL << 32,          ///< Two timers.
  XF_TIMERS3 = 1ULL << 33,          ///< Three timers.
  XF_PRID = 1ULL << 34,             ///< Processor ID register.
  XF_REGPROTECT = 1ULL << 35,       ///< Region protection option.
  XF_MISCSR = 1ULL << 36,           ///< Miscellaneous special registers.
  XF_ESP32S2OPS = 1ULL << 37        ///< ESP32-S2-specific operations.
};

/// Parse \p CPU into an Xtensa \c CPUKind.
///
/// Resolves aliases via \c getBaseName before matching.
///
/// \param CPU CPU name to parse.
/// \returns Matching \c CPUKind, or \c CK_INVALID if unrecognized.
LLVM_ABI CPUKind parseCPUKind(StringRef CPU);
/// Return the canonical CPU name for \p CPU, resolving aliases.
///
/// \param CPU CPU name, possibly an alias.
/// \returns Canonical base name, or \p CPU unchanged if it is not an alias.
LLVM_ABI StringRef getBaseName(StringRef CPU);
/// Append the +feature strings enabled by CPU name \p CPU to \p Features.
///
/// Resolves aliases via \c getBaseName. \p CPU must be a recognized CPU.
///
/// \param CPU CPU name whose features are requested.
/// \param Features Vector that receives enabled feature names.
LLVM_ABI void getCPUFeatures(StringRef CPU,
                             SmallVectorImpl<StringRef> &Features);
/// Append every valid Xtensa CPU name (and aliases) to \p Values.
///
/// \param Values Vector that receives CPU names.
LLVM_ABI void fillValidCPUList(SmallVectorImpl<StringRef> &Values);

} // namespace Xtensa
} // namespace llvm

#endif // LLVM_SUPPORT_XTENSATARGETPARSER_H
