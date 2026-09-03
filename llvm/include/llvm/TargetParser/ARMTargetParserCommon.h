//===---------------- ARMTargetParserCommon ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Code that is common to ARMTargetParser and AArch64TargetParser.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_ARMTARGETPARSERCOMMON_H
#define LLVM_TARGETPARSER_ARMTARGETPARSERCOMMON_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
namespace ARM {

/// Instruction-set architecture kind inferred from an architecture name.
enum class ISAKind {
  INVALID = 0, ///< Unrecognized or unspecified ISA.
  ARM,         ///< A32 / ARM state.
  THUMB,       ///< T32 / Thumb state.
  AARCH64      ///< AArch64 / A64.
};

/// Endianness inferred from an architecture name.
enum class EndianKind {
  INVALID = 0, ///< Unrecognized or unspecified endianness.
  LITTLE,      ///< Little-endian.
  BIG          ///< Big-endian.
};

/// Converts e.g. "armv8" -> "armv8-a"
///
/// \param Arch Architecture name or synonym to normalize.
/// \returns Normalized architecture synonym for \p Arch.
LLVM_ABI StringRef getArchSynonym(StringRef Arch);

/// Return the canonical architecture substring from an architecture name.
///
/// MArch is expected to be of the form (arm|thumb)?(eb)?(v.+)?(eb)?, but
/// (iwmmxt|xscale)(eb)? is also permitted. If the former, return
/// "v.+", if the latter, return unmodified string, minus 'eb'.
/// If invalid, return empty string.
///
/// \param Arch Full architecture name such as "armv7eb" or "xscale".
/// \returns Canonical arch substring, or an empty string if \p Arch is invalid.
LLVM_ABI StringRef getCanonicalArchName(StringRef Arch);

/// Parse the ISA kind from an architecture name prefix.
///
/// \param Arch Architecture name such as "arm", "thumb", or "aarch64".
/// \returns Matching \c ISAKind, or \c ISAKind::INVALID if unrecognized.
LLVM_ABI ISAKind parseArchISA(StringRef Arch);

/// Parse the endianness from an architecture name.
///
/// \param Arch Architecture name such as "armeb" or "aarch64_be".
/// \returns Matching \c EndianKind, or \c EndianKind::INVALID if unrecognized.
LLVM_ABI EndianKind parseArchEndian(StringRef Arch);

/// Parsed result of a branch-protection specification string.
struct ParsedBranchProtection {
  StringRef Scope;              ///< PAC-RET scope ("none", "non-leaf", or "all").
  StringRef Key;                ///< PAC key ("a_key" or "b_key").
  bool BranchTargetEnforcement; ///< True if Branch Target Identification (BTI) is enabled.
  bool BranchProtectionPAuthLR; ///< True if PC-relative PAC-RET (PAuthLR) is enabled.
  bool GuardedControlStack;     ///< True if Guarded Control Stack (GCS) is enabled.
};

/// Parse a branch-protection specification into \p PBP.
///
/// Spec has the form \c standard, \c none, or a '+' separated list of
/// \c bti, \c pac-ret[+b-key,+leaf,+pc]*, and \c gcs.
///
/// \param Spec Branch-protection specification string.
/// \param PBP On success, receives the parsed options.
/// \param Err On failure, receives the erroneous token from \p Spec.
/// \param Triple Target triple used for defaults (e.g. Windows AArch64 key).
/// \param EnablePAuthLR When true, \c standard enables PAuthLR.
/// \returns True on success; false if \p Spec contains an unknown option.
LLVM_ABI bool parseBranchProtection(StringRef Spec, ParsedBranchProtection &PBP,
                                    StringRef &Err, const llvm::Triple &Triple,
                                    bool EnablePAuthLR = false);

} // namespace ARM
} // namespace llvm
#endif
