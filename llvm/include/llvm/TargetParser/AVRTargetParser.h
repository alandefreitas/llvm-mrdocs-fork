//===-- AVRTargetParser - Parser for AVR target features ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a target parser to recognise AVR hardware features.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_AVRTARGETPARSER_H
#define LLVM_TARGETPARSER_AVRTARGETPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <string>

namespace llvm {

/// AVR architecture feature parsing helpers.
namespace AVR {

/// Maps an ELF EF_AVR_ARCH_* e_flags value to an AVR feature-set name.
///
/// \param EFlag AVR architecture flag from the ELF header (typically masked
///        with EF_AVR_ARCH_MASK).
/// \returns The corresponding feature-set name (e.g. "avr5", "xmega1"), or an
///          Error if \p EFlag is unrecognized.
LLVM_ABI Expected<std::string> getFeatureSetFromEFlag(const unsigned EFlag);

} // namespace AVR
} // namespace llvm
#endif
