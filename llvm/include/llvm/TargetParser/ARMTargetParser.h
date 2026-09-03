//===-- ARMTargetParser - Parser for ARM target features --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise ARM hardware features
// such as FPU/CPU/ARCH/extensions and specific support such as HWDIV.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_ARMTARGETPARSER_H
#define LLVM_TARGETPARSER_ARMTARGETPARSER_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ARMBuildAttributes.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/ARMTargetParserCommon.h"
#include <vector>

namespace llvm {

class Triple;

namespace ARM {

/// ARM procedure-call ABI kinds selected from a triple or ABI name.
enum ARMABI {
  ARM_ABI_UNKNOWN, ///< Unrecognized or unspecified ABI.
  ARM_ABI_APCS,    ///< APCS / APCS-GNU calling convention.
  ARM_ABI_AAPCS,   ///< AAPCS (ARM EABI) calling convention.
  ARM_ABI_AAPCS16  ///< AAPCS16 (WatchABI) calling convention.
};

/// Architecture-extension bit flags selectable on an ARM CPU.
///
/// This is not the same as the AArch64 extension list.
enum ArchExtKind : uint64_t {
  AEK_INVALID = 0,          ///< Invalid or unrecognized extension.
  AEK_NONE = 1,             ///< No architecture extension.
  AEK_CRC = 1 << 1,         ///< CRC32 instructions.
  AEK_CRYPTO = 1 << 2,      ///< Cryptographic instructions.
  AEK_FP = 1 << 3,          ///< Floating-point instructions.
  AEK_HWDIVTHUMB = 1 << 4,  ///< Hardware divide in Thumb state.
  AEK_HWDIVARM = 1 << 5,    ///< Hardware divide in ARM state.
  AEK_MP = 1 << 6,          ///< Multiprocessing extensions.
  AEK_SIMD = 1 << 7,        ///< Advanced SIMD (NEON) instructions.
  AEK_SEC = 1 << 8,         ///< Security extensions.
  AEK_VIRT = 1 << 9,        ///< Virtualization extensions.
  AEK_DSP = 1 << 10,        ///< DSP instructions.
  AEK_FP16 = 1 << 11,       ///< Half-precision floating-point.
  AEK_RAS = 1 << 12,        ///< Reliability, Availability, and Serviceability.
  AEK_DOTPROD = 1 << 13,    ///< Dot-product instructions.
  AEK_SHA2 = 1 << 14,       ///< SHA2 cryptographic instructions.
  AEK_AES = 1 << 15,        ///< AES cryptographic instructions.
  AEK_FP16FML = 1 << 16,    ///< Half-precision floating-point multiply/add.
  AEK_SB = 1 << 17,         ///< Speculation Barrier instruction.
  AEK_FP_DP = 1 << 18,      ///< Double-precision floating-point.
  AEK_LOB = 1 << 19,        ///< Low Overhead Branch extension.
  AEK_BF16 = 1 << 20,       ///< BFloat16 instructions.
  AEK_I8MM = 1 << 21,       ///< Int8 matrix multiply instructions.
  AEK_CDECP0 = 1 << 22,     ///< Custom Datapath Extension coprocessor 0.
  AEK_CDECP1 = 1 << 23,     ///< Custom Datapath Extension coprocessor 1.
  AEK_CDECP2 = 1 << 24,     ///< Custom Datapath Extension coprocessor 2.
  AEK_CDECP3 = 1 << 25,     ///< Custom Datapath Extension coprocessor 3.
  AEK_CDECP4 = 1 << 26,     ///< Custom Datapath Extension coprocessor 4.
  AEK_CDECP5 = 1 << 27,     ///< Custom Datapath Extension coprocessor 5.
  AEK_CDECP6 = 1 << 28,     ///< Custom Datapath Extension coprocessor 6.
  AEK_CDECP7 = 1 << 29,     ///< Custom Datapath Extension coprocessor 7.
  AEK_PACBTI = 1 << 30,     ///< Pointer Authentication and Branch Target ID.
  AEK_MVE = 1ULL << 31,     ///< M-Profile Vector Extension.
  // Unsupported extensions.
  AEK_OS = 1ULL << 59,      ///< Operating-system extension (unsupported).
  AEK_IWMMXT = 1ULL << 60,  ///< Intel Wireless MMX (unsupported).
  AEK_IWMMXT2 = 1ULL << 61, ///< Intel Wireless MMX2 (unsupported).
  AEK_MAVERICK = 1ULL << 62, ///< Cirrus Maverick FPU (unsupported).
  AEK_XSCALE = 1ULL << 63,  ///< Intel XScale (unsupported).
};

/// Named ARM architecture extension and its +/- feature strings.
struct ExtName {
  StringRef Name;       ///< Canonical extension name.
  uint64_t ID;          ///< \c ArchExtKind identifier for this extension.
  StringRef Feature;    ///< +feature string, or empty if none.
  StringRef NegFeature; ///< -feature string, or empty if none.
};

/// Table of ARM architecture-extension names and feature strings.
constexpr ExtName ARCHExtNames[] = {
#define ARM_ARCH_EXT_NAME(NAME, ID, FEATURE, NEGFEATURE)                       \
  {NAME, ID, FEATURE, NEGFEATURE},
#include "ARMTargetParser.def"
};

/// Named hardware-divide spelling and its \c ArchExtKind bitmask.
constexpr struct {
  StringRef Name; ///< Canonical hardware-divide name.
  uint64_t ID;    ///< Matching \c ArchExtKind bits (use getHWDivFeatures).
} HWDivNames[] = {
#define ARM_HW_DIV_NAME(NAME, ID) {NAME, ID},
#include "ARMTargetParser.def"
};

/// ARM architecture kind identifiers.
///
/// Values are generated from ARMTargetParser.def, including:
/// - INVALID: Unrecognized architecture.
/// - ARMV4 / ARMV4T: ARMv4 / ARMv4T.
/// - ARMV5T / ARMV5TE / ARMV5TEJ: ARMv5 variants.
/// - ARMV6 / ARMV6K / ARMV6T2 / ARMV6KZ / ARMV6M: ARMv6 variants.
/// - ARMV7A / ARMV7VE / ARMV7R / ARMV7M / ARMV7EM / ARMV7S / ARMV7K: ARMv7.
/// - ARMV8A through ARMV8_9A, ARMV8R, ARMV8MBaseline / Mainline,
///   ARMV8_1MMainline: ARMv8 variants.
/// - ARMV9A through ARMV9_7A: ARMv9-A variants.
/// - IWMMXT / IWMMXT2 / XSCALE: legacy Intel/XScale architectures.
enum class ArchKind {
#define ARM_ARCH(NAME, ID, CPU_ATTR, ARCH_FEATURE, ARCH_ATTR, ARCH_FPU,        \
                 ARCH_BASE_EXT)                                                \
  ID,
#include "ARMTargetParser.def"
};

/// Named ARM CPU, its architecture, default status, and extra extensions.
///
/// The same CPU can appear for multiple arches and can be default on multiple
/// arches. When finding the Arch for a CPU, first-found prevails; sort entries
/// accordingly. When this becomes table-generated, two tables may be needed.
struct CpuNames {
  StringRef Name;           ///< Canonical CPU name.
  ArchKind ArchID;          ///< Architecture kind for this CPU.
  bool Default;             ///< True if \ref Name is the default CPU for \ref ArchID.
  uint64_t DefaultExtensions; ///< Extra default \c ArchExtKind bits for this CPU.
};

/// Table of ARM CPU names and their architectures.
constexpr CpuNames CPUNames[] = {
#define ARM_CPU_NAME(NAME, ID, DEFAULT_FPU, IS_DEFAULT, DEFAULT_EXT)           \
  {NAME, ARM::ArchKind::ID, IS_DEFAULT, DEFAULT_EXT},
#include "ARMTargetParser.def"
};

/// ARM floating-point unit identifiers.
///
/// Values other than \c FK_LAST are generated from ARMTargetParser.def
/// (for example FK_INVALID, FK_NONE, FK_VFPV3, FK_NEON, FK_SOFTVFP).
enum FPUKind {
#define ARM_FPU(NAME, KIND, VERSION, NEON_SUPPORT, RESTRICTION) KIND,
#include "ARMTargetParser.def"
  FK_LAST ///< Sentinel one past the last valid FPU kind.
};

/// ARM floating-point unit version.
enum class FPUVersion {
  NONE,           ///< No FPU, or an unrecognized FPU kind.
  VFPV2,          ///< VFPv2.
  VFPV3,          ///< VFPv3.
  VFPV3_FP16,     ///< VFPv3 with half-precision.
  VFPV4,          ///< VFPv4.
  VFPV5,          ///< VFPv5.
  VFPV5_FULLFP16, ///< VFPv5 with full half-precision.
};

/// Register and precision restriction implied by an FPU name.
enum class FPURestriction {
  None = 0, ///< No restriction.
  D16,      ///< Only 16 D registers.
  SP_D16    ///< Only single-precision instructions, with 16 D registers.
};

/// Return true if \p restriction allows double-precision floating-point.
///
/// \param restriction FPU restriction to query.
/// \returns True unless \p restriction is \c FPURestriction::SP_D16.
inline bool isDoublePrecision(const FPURestriction restriction) {
  return restriction != FPURestriction::SP_D16;
}

/// Return true if \p restriction allows the full 32 D-register file.
///
/// \param restriction FPU restriction to query.
/// \returns True when \p restriction is \c FPURestriction::None.
inline bool has32Regs(const FPURestriction restriction) {
  return restriction == FPURestriction::None;
}

/// Level of Neon (Advanced SIMD) support implied by an FPU name.
enum class NeonSupportLevel {
  None = 0, ///< No Neon.
  Neon,     ///< Neon.
  Crypto    ///< Neon with Crypto.
};

/// ARM architecture profile (Application, Real-time, or Microcontroller).
enum class ProfileKind {
  INVALID = 0, ///< Unknown or invalid profile.
  A,           ///< Application profile.
  R,           ///< Real-time profile.
  M            ///< Microcontroller profile.
};

/// Named ARM FPU kind and the architectural features it implies.
///
/// Table entries must appear in \c ARM::FPUKind order for correct indexing.
/// Use \c getFPUSynonym for alternate spellings and \c getFPUFeatures for
/// feature strings.
struct FPUName {
  StringRef Name;                 ///< Canonical FPU name.
  FPUKind ID;                     ///< \c FPUKind identifier for this FPU.
  FPUVersion FPUVer;              ///< FPU version associated with this kind.
  NeonSupportLevel NeonSupport;   ///< Neon support level for this FPU.
  FPURestriction Restriction;     ///< Register / precision restriction.
};

/// Table of canonical ARM FPU names and their architectural features.
static constexpr FPUName FPUNames[] = {
#define ARM_FPU(NAME, KIND, VERSION, NEON_SUPPORT, RESTRICTION)                \
  {NAME, KIND, VERSION, NEON_SUPPORT, RESTRICTION},
#include "llvm/TargetParser/ARMTargetParser.def"
};

/// Named ARM architecture and its build-attribute and default-FPU fields.
///
/// This table also provides the build attribute fields for CPU arch and Arch
/// ID, according to the Addenda to the ARM ABI, chapters 2.4 and 2.3.5.2
/// respectively. Use \c getArchSynonym for alternate spellings.
///
/// FIXME: SubArch values were simplified to fit into the expectations of the
/// triples and are not conforming with their official names. Check to see if
/// the expectation should be changed.
struct ArchNames {
  StringRef Name;                      ///< Canonical architecture name.
  StringRef CPUAttr;                   ///< CPU class in build attributes.
  StringRef ArchFeature;               ///< +subarch feature string (with leading '+').
  FPUKind DefaultFPU;                  ///< Default FPU for this architecture.
  uint64_t ArchBaseExtensions;         ///< Base \c ArchExtKind bits for this arch.
  ArchKind ID;                         ///< Architecture kind identifier.
  ARMBuildAttrs::CPUArch ArchAttr;     ///< Arch ID in build attributes.

  /// Return the architecture feature name without the leading "+".
  ///
  /// \returns Architecture feature name without the leading "+".
  StringRef getSubArch() const { return ArchFeature.substr(1); }
};

/// Table of canonical ARM architecture names and build attributes.
static constexpr ArchNames ARMArchNames[] = {
#define ARM_ARCH(NAME, ID, CPU_ATTR, ARCH_FEATURE, ARCH_ATTR, ARCH_FPU,        \
                 ARCH_BASE_EXT)                                                \
  {NAME,          CPU_ATTR,     ARCH_FEATURE, ARCH_FPU,                        \
   ARCH_BASE_EXT, ArchKind::ID, ARCH_ATTR},
#include "llvm/TargetParser/ARMTargetParser.def"
};

/// Decrement \p Kind within the contiguous ARMv8-A / ARMv9-A sequence.
///
/// Only valid for kinds in the ARMV8A..ARMV9_3A range; otherwise sets
/// \p Kind to \c ArchKind::INVALID.
///
/// \param Kind Architecture kind to decrement in place.
/// \returns Reference to the updated \p Kind.
inline ArchKind &operator--(ArchKind &Kind) {
  assert((Kind >= ArchKind::ARMV8A && Kind <= ArchKind::ARMV9_3A) &&
         "We only expect operator-- to be called with ARMV8/V9");
  if (Kind == ArchKind::INVALID || Kind == ArchKind::ARMV8A ||
      Kind == ArchKind::ARMV8_1A || Kind == ArchKind::ARMV9A ||
      Kind == ArchKind::ARMV8R)
    Kind = ArchKind::INVALID;
  else {
    unsigned KindAsInteger = static_cast<unsigned>(Kind);
    Kind = static_cast<ArchKind>(--KindAsInteger);
  }
  return Kind;
}

// Information by ID
/// Return the canonical FPU name for identifier \p FPUKind.
///
/// \param FPUKind FPU kind from \c FPUKind.
/// \returns FPU name, or an empty string if \p FPUKind is out of range.
LLVM_ABI StringRef getFPUName(FPUKind FPUKind);
/// Return the FPU version for identifier \p FPUKind.
///
/// \param FPUKind FPU kind from \c FPUKind.
/// \returns Matching \c FPUVersion, or \c FPUVersion::NONE if out of range.
LLVM_ABI FPUVersion getFPUVersion(FPUKind FPUKind);
/// Return the Neon support level for identifier \p FPUKind.
///
/// \param FPUKind FPU kind from \c FPUKind.
/// \returns Matching \c NeonSupportLevel, or \c None if out of range.
LLVM_ABI NeonSupportLevel getFPUNeonSupportLevel(FPUKind FPUKind);
/// Return the FPU restriction for identifier \p FPUKind.
///
/// \param FPUKind FPU kind from \c FPUKind.
/// \returns Matching \c FPURestriction, or \c None if out of range.
LLVM_ABI FPURestriction getFPURestriction(FPUKind FPUKind);

/// Append +feature / -feature strings enabled by FPU kind \p FPUKind.
///
/// \param FPUKind FPU kind to expand.
/// \param Features Vector that receives FPU feature strings.
/// \returns False if \p FPUKind is invalid or \c FK_LAST; true otherwise.
LLVM_ABI bool getFPUFeatures(FPUKind FPUKind, std::vector<StringRef> &Features);
/// Append +/-hwdiv feature strings for hardware-divide bitmask \p HWDivKind.
///
/// \param HWDivKind Bitmask of \c AEK_HWDIVARM / \c AEK_HWDIVTHUMB.
/// \param Features Vector that receives hwdiv feature strings.
/// \returns False if \p HWDivKind is \c AEK_INVALID; true otherwise.
LLVM_ABI bool getHWDivFeatures(uint64_t HWDivKind,
                               std::vector<StringRef> &Features);
/// Append +feature / -feature strings for each bit in \p Extensions.
///
/// Also appends hardware-divide features via \c getHWDivFeatures.
///
/// \param Extensions Bitmask of \c ArchExtKind flags.
/// \param Features Vector that receives extension feature strings.
/// \returns False if \p Extensions is \c AEK_INVALID; true otherwise.
LLVM_ABI bool getExtensionFeatures(uint64_t Extensions,
                                   std::vector<StringRef> &Features);

/// Return the canonical architecture name for \p AK.
///
/// \param AK Architecture kind to look up.
/// \returns Architecture name such as "armv7-a".
LLVM_ABI StringRef getArchName(ArchKind AK);
/// Return the ARM build-attribute CPUArch value for \p AK.
///
/// \param AK Architecture kind to look up.
/// \returns \c ARMBuildAttrs::CPUArch enumerator as an unsigned value.
LLVM_ABI unsigned getArchAttr(ArchKind AK);
/// Return the build-attribute CPU class string for \p AK.
///
/// \param AK Architecture kind to look up.
/// \returns CPU attribute such as "7-A".
LLVM_ABI StringRef getCPUAttr(ArchKind AK);
/// Return the sub-architecture feature name for \p AK without a leading "+".
///
/// \param AK Architecture kind to look up.
/// \returns Sub-architecture string such as "v7".
LLVM_ABI StringRef getSubArch(ArchKind AK);
/// Return the architecture-extension name for bitmask \p ArchExtKind.
///
/// \param ArchExtKind Extension identifier from \c ArchExtKind.
/// \returns Extension name, or an empty string if unrecognized.
LLVM_ABI StringRef getArchExtName(uint64_t ArchExtKind);
/// Return the +feature or -feature string for extension name \p ArchExt.
///
/// A leading "no" on \p ArchExt selects the negated feature string.
///
/// \param ArchExt Extension name, optionally prefixed with "no".
/// \returns Target-feature string such as "+crc", or empty if unknown.
LLVM_ABI StringRef getArchExtFeature(StringRef ArchExt);
/// Append feature strings for architecture extension \p ArchExt.
///
/// For "fp" / "fp.dp", also updates \p ArgFPUKind to the implied FPU.
/// A leading "no" on \p ArchExt selects negated features.
///
/// \param CPU CPU name used when resolving default FPUs; empty means "generic".
/// \param AK Architecture kind used with \p CPU for default FPU lookup.
/// \param ArchExt Extension name, optionally prefixed with "no".
/// \param Features Vector that receives appended feature strings.
/// \param ArgFPUKind In/out FPU kind updated when enabling or disabling FP.
/// \returns True if features were appended or an FPU update succeeded.
LLVM_ABI bool appendArchExtFeatures(StringRef CPU, ARM::ArchKind AK,
                                    StringRef ArchExt,
                                    std::vector<StringRef> &Features,
                                    FPUKind &ArgFPUKind);
/// Map an ARMv9-A kind in ARMV9A..ARMV9_3A to the corresponding ARMv8-A kind.
///
/// \param AK Architecture kind to convert.
/// \returns Matching ARMv8.5-A..ARMv8.8-A kind, or \c INVALID if out of range.
LLVM_ABI ArchKind convertV9toV8(ArchKind AK);

// Information by Name
/// Return the default FPU for CPU \p CPU under architecture \p AK.
///
/// When \p CPU is "generic", returns the architecture's default FPU.
///
/// \param CPU CPU name, or "generic".
/// \param AK Architecture kind used when \p CPU is "generic".
/// \returns Matching \c FPUKind, or \c FK_INVALID if unrecognized.
LLVM_ABI FPUKind getDefaultFPU(StringRef CPU, ArchKind AK);
/// Return the default extension bitmask for CPU \p CPU under architecture \p AK.
///
/// When \p CPU is "generic", returns the architecture's base extensions.
/// Otherwise combines the CPU's architecture base extensions with its extras.
///
/// \param CPU CPU name, or "generic".
/// \param AK Architecture kind used when \p CPU is "generic".
/// \returns Combined \c ArchExtKind bitmask, or \c AEK_INVALID if unknown.
LLVM_ABI uint64_t getDefaultExtensions(StringRef CPU, ArchKind AK);
/// Return the default CPU name for architecture \p Arch.
///
/// \param Arch Architecture name to look up.
/// \returns Default CPU name, "generic" if none is marked default, or empty
///          if \p Arch is unrecognized.
LLVM_ABI StringRef getDefaultCPU(StringRef Arch);
/// Return the canonical FPU name for spelling \p FPU.
///
/// Maps known aliases (for example "vfp3" to "vfpv3"); unsupported names map
/// to "invalid". Unrecognized spellings are returned unchanged.
///
/// \param FPU FPU name or synonym.
/// \returns Canonical FPU name, "invalid", or \p FPU unchanged.
LLVM_ABI StringRef getFPUSynonym(StringRef FPU);

// Parser
/// Parse hardware-divide name \p HWDiv into its extension bitmask.
///
/// \param HWDiv Hardware-divide spelling such as "thumb" or "arm,thumb".
/// \returns Matching \c ArchExtKind bits, or \c AEK_INVALID if unrecognized.
LLVM_ABI uint64_t parseHWDiv(StringRef HWDiv);
/// Parse FPU name \p FPU into an \c FPUKind.
///
/// \param FPU FPU name or synonym.
/// \returns Matching \c FPUKind, or \c FK_INVALID if unrecognized.
LLVM_ABI FPUKind parseFPU(StringRef FPU);
/// Parse architecture name \p Arch into an \c ArchKind.
///
/// Allows a partial match after canonicalization (for example "v7a" matches
/// "armv7-a").
///
/// \param Arch Architecture name such as "armv7-a".
/// \returns Matching \c ArchKind, or \c ArchKind::INVALID if unrecognized.
LLVM_ABI ArchKind parseArch(StringRef Arch);
/// Parse architecture-extension name \p ArchExt into its bitmask.
///
/// \param ArchExt Extension name such as "crc".
/// \returns Matching \c ArchExtKind bit, or \c AEK_INVALID if unrecognized.
LLVM_ABI uint64_t parseArchExt(StringRef ArchExt);
/// Parse CPU name \p CPU into its \c ArchKind.
///
/// \param CPU CPU name such as "cortex-a53".
/// \returns Architecture of that CPU, or \c ArchKind::INVALID if unrecognized.
LLVM_ABI ArchKind parseCPUArch(StringRef CPU);
/// Parse architecture name \p Arch into its \c ProfileKind.
///
/// \param Arch Architecture name such as "armv7-m".
/// \returns Matching profile, or \c ProfileKind::INVALID if none applies.
LLVM_ABI ProfileKind parseArchProfile(StringRef Arch);
/// Parse architecture name \p Arch into its major ISA version number.
///
/// \param Arch Architecture name such as "armv8-a".
/// \returns Major version (for example 7 or 8), or 0 if unrecognized.
LLVM_ABI unsigned parseArchVersion(StringRef Arch);

/// Append every valid ARM CPU name to \p Values.
///
/// Skips entries whose architecture is \c ArchKind::INVALID.
///
/// \param Values Vector that receives CPU names.
LLVM_ABI void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values);
/// Return the default ABI name string for triple \p TT.
///
/// \param TT Target triple whose OS and environment select the ABI.
/// \returns ABI name such as "aapcs", "aapcs-linux", "aapcs16", or "apcs-gnu".
LLVM_ABI LLVM_READONLY StringRef computeDefaultTargetABI(const Triple &TT);
/// Return the \c ARMABI enumerator for triple \p TT and optional name \p ABIName.
///
/// When \p ABIName is empty, uses \c computeDefaultTargetABI(\p TT).
///
/// \param TT Target triple used when \p ABIName is empty.
/// \param ABIName Optional ABI name such as "aapcs" or "apcs-gnu".
/// \returns Matching \c ARMABI, or \c ARM_ABI_UNKNOWN if unrecognized.
LLVM_ABI LLVM_READONLY ARMABI computeTargetABI(const Triple &TT,
                                               StringRef ABIName = "");

/// Get the (LLVM) name of the minimum ARM CPU for the arch we are targeting.
///
/// \param Triple Target triple whose OS and environment may force a default.
/// \param MArch Architecture name (e.g., "armv7s"). If it is an empty string
///        then the triple's arch name is used.
/// \returns LLVM CPU name for the architecture, or an empty string if none
///          applies.
LLVM_ABI StringRef getARMCPUForArch(const llvm::Triple &Triple,
                                    StringRef MArch = {});

/// Print a table of -march extensions that have a non-empty feature string.
///
/// \param DescMap Optional map from extension name to human-readable description.
LLVM_ABI void PrintSupportedExtensions(StringMap<StringRef> DescMap);

} // namespace ARM
} // namespace llvm

#endif
