//===-- CSKYTargetParser - Parser for CSKY target features --------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise CSKY hardware features
// such as FPU/CPU/ARCH/extensions and specific support such as HWDIV.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_CSKYTARGETPARSER_H
#define LLVM_TARGETPARSER_CSKYTARGETPARSER_H

#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <vector>

namespace llvm {
class StringRef;

/// CSKY CPU, architecture, FPU, and extension name parsing helpers.
namespace CSKY {

/// CSKY architecture-extension bit flags selectable on a CPU.
enum ArchExtKind : uint64_t {
  AEK_INVALID = 0,           ///< Invalid or unrecognized extension.
  AEK_NONE = 1,              ///< No architecture extension.
  AEK_FPUV2SF = 1 << 1,      ///< FPUv2 single-precision instructions.
  AEK_FPUV2DF = 1 << 2,      ///< FPUv2 double-precision instructions.
  AEK_FDIVDU = 1 << 3,       ///< Floating-point divide instructions.
  AEK_FPUV3HI = 1 << 4,      ///< FPUv3 half-word conversion instructions.
  AEK_FPUV3HF = 1 << 5,      ///< FPUv3 half-precision instructions.
  AEK_FPUV3SF = 1 << 6,      ///< FPUv3 single-precision instructions.
  AEK_FPUV3DF = 1 << 7,      ///< FPUv3 double-precision instructions.
  AEK_FLOATE1 = 1 << 8,      ///< CSKY floate1 floating-point instructions.
  AEK_FLOAT1E2 = 1 << 9,     ///< CSKY float1e2 floating-point instructions.
  AEK_FLOAT1E3 = 1 << 10,    ///< CSKY float1e3 floating-point instructions.
  AEK_FLOAT3E4 = 1 << 11,    ///< CSKY float3e4 floating-point instructions.
  AEK_FLOAT7E60 = 1 << 12,   ///< CSKY float7e60 floating-point instructions.
  AEK_HWDIV = 1 << 13,       ///< Hardware divide instructions.
  AEK_STLD = 1 << 14,        ///< Multiple load/store instructions.
  AEK_PUSHPOP = 1 << 15,     ///< Push/pop instructions.
  AEK_EDSP = 1 << 16,        ///< Enhanced DSP instructions.
  AEK_DSP1E2 = 1 << 17,      ///< CSKY dsp1e2 instructions.
  AEK_DSPE60 = 1 << 18,      ///< CSKY dspe60 instructions.
  AEK_DSPV2 = 1 << 19,       ///< DSP V2.0 instructions.
  AEK_DSPSILAN = 1 << 20,    ///< DSP Silan instructions.
  AEK_ELRW = 1 << 21,        ///< Extended LRW instruction.
  AEK_TRUST = 1 << 22,       ///< Trust (security) instructions.
  AEK_JAVA = 1 << 23,        ///< Java acceleration instructions.
  AEK_CACHE = 1 << 24,       ///< Cache instructions.
  AEK_NVIC = 1 << 25,        ///< Nested vectored interrupt controller.
  AEK_DOLOOP = 1 << 26,      ///< Hardware do-loop instructions.
  AEK_HIGHREG = 1 << 27,     ///< High registers r16-r31.
  AEK_SMART = 1 << 28,       ///< CPU Smart Mode.
  AEK_VDSP2E3 = 1 << 29,     ///< CSKY vdsp2e3 instructions.
  AEK_VDSP2E60F = 1 << 30,   ///< CSKY vdsp2e60f instructions.
  AEK_VDSPV2 = 1ULL << 31,   ///< Vector DSP v2 instructions.
  AEK_HARDTP = 1ULL << 32,   ///< Hardware TLS pointer register.
  AEK_SOFTTP = 1ULL << 33,   ///< Software TLS pointer (no hardware TP).
  AEK_ISTACK = 1ULL << 34,   ///< Interrupt attribute / interrupt stack.
  AEK_CONSTPOOL = 1ULL << 35, ///< Compiler-emitted constant pool.
  AEK_STACKSIZE = 1ULL << 36, ///< Emit stack-size information.
  AEK_CCRT = 1ULL << 37,     ///< CSKY compiler runtime.
  AEK_VDSPV1 = 1ULL << 38,   ///< 128-bit vector DSP v1 instructions.
  AEK_E1 = 1ULL << 39,       ///< CSKY e1 ISA instructions.
  AEK_E2 = 1ULL << 40,       ///< CSKY e2 ISA instructions.
  AEK_2E3 = 1ULL << 41,      ///< CSKY 2e3 ISA instructions.
  AEK_MP = 1ULL << 42,       ///< CSKY multiprocessor instructions.
  AEK_3E3R1 = 1ULL << 43,    ///< CSKY 3e3r1 ISA instructions.
  AEK_3E3R2 = 1ULL << 44,    ///< CSKY 3e3r2 ISA instructions.
  AEK_3E3R3 = 1ULL << 45,    ///< CSKY 3e3r3 ISA instructions.
  AEK_3E7 = 1ULL << 46,      ///< CSKY 3e7 ISA instructions.
  AEK_MP1E2 = 1ULL << 47,    ///< CSKY mp1e2 multiprocessor instructions.
  AEK_7E10 = 1ULL << 48,     ///< CSKY 7e10 ISA instructions.
  AEK_10E60 = 1ULL << 49     ///< CSKY 10e60 ISA instructions.
};

/// Combined architecture-extension bitmasks for CSKY ISA generations.
enum MultiArchExtKind : uint64_t {
  /// E1 ISA generation (e1 + extended LRW).
  MAEK_E1 = CSKY::AEK_E1 | CSKY::AEK_ELRW,
  /// E2 ISA generation (e2 plus \c MAEK_E1).
  MAEK_E2 = CSKY::AEK_E2 | CSKY::MAEK_E1,
  /// 2E3 ISA generation (2e3 plus \c MAEK_E2).
  MAEK_2E3 = CSKY::AEK_2E3 | CSKY::MAEK_E2,
  /// Multiprocessor bitmask (mp plus \c MAEK_2E3).
  MAEK_MP = CSKY::AEK_MP | CSKY::MAEK_2E3,
  /// 3e3r1 ISA revision bitmask.
  MAEK_3E3R1 = CSKY::AEK_3E3R1,
  /// 3e3r2 ISA revision (3e3r1 + 3e3r2 + doloop).
  MAEK_3E3R2 = CSKY::AEK_3E3R1 | CSKY::AEK_3E3R2 | CSKY::AEK_DOLOOP,
  /// 3E7 ISA generation (3e7 plus \c MAEK_2E3).
  MAEK_3E7 = CSKY::AEK_3E7 | CSKY::MAEK_2E3,
  /// mp1e2 bitmask (mp1e2 plus \c MAEK_3E7).
  MAEK_MP1E2 = CSKY::AEK_MP1E2 | CSKY::MAEK_3E7,
  /// 7E10 ISA generation (7e10 plus \c MAEK_3E7).
  MAEK_7E10 = CSKY::AEK_7E10 | CSKY::MAEK_3E7,
  /// 10E60 ISA generation (10e60 plus \c MAEK_7E10).
  MAEK_10E60 = CSKY::AEK_10E60 | CSKY::MAEK_7E10,
};

/// CSKY floating-point unit identifiers.
///
/// Values other than \c FK_LAST are generated from CSKYTargetParser.def:
/// - FK_INVALID: Unrecognized FPU.
/// - FK_AUTO: Automatically selected FPU.
/// - FK_FPV2: FPUv2 single and double precision.
/// - FK_FPV2_DIVD: FPUv2 with floating-point divide.
/// - FK_FPV2_SF: FPUv2 single-precision only.
/// - FK_FPV3: Full FPUv3 (half, single, and double).
/// - FK_FPV3_HF: FPUv3 half-precision.
/// - FK_FPV3_HSF: FPUv3 half and single precision.
/// - FK_FPV3_SDF: FPUv3 single and double precision.
enum CSKYFPUKind {
#define CSKY_FPU(NAME, KIND, VERSION) KIND,
#include "CSKYTargetParser.def"
  FK_LAST ///< Sentinel one past the last valid FPU kind.
};

/// CSKY floating-point unit version.
enum class FPUVersion {
  NONE, ///< No FPU, or an unrecognized FPU kind.
  FPV2, ///< Floating-point version 2.
  FPV3, ///< Floating-point version 3.
};

/// CSKY architecture kind identifiers.
///
/// Values are generated from CSKYTargetParser.def:
/// - INVALID: Unrecognized architecture.
/// - CK801: CK801 architecture.
/// - CK802: CK802 architecture.
/// - CK803: CK803 architecture.
/// - CK803S: CK803S architecture.
/// - CK804: CK804 architecture.
/// - CK805: CK805 architecture.
/// - CK807: CK807 architecture.
/// - CK810: CK810 architecture.
/// - CK810V: CK810 with vector DSP.
/// - CK860: CK860 architecture.
/// - CK860V: CK860 with vector DSP.
enum class ArchKind {
#define CSKY_ARCH(NAME, ID, ARCH_BASE_EXT) ID,
#include "CSKYTargetParser.def"
};

/// Named CSKY architecture extension and its +/- feature strings.
// FIXME: TableGen this.
struct ExtName {
  const char *NameCStr; ///< NUL-terminated extension name.
  size_t NameLength;    ///< Length of \ref NameCStr excluding the terminator.
  uint64_t ID;          ///< \c ArchExtKind identifier for this extension.
  const char *Feature;  ///< +feature string, or null if none.
  const char *NegFeature; ///< -feature string, or null if none.

  /// Return the extension name as a StringRef.
  ///
  /// \returns Extension name.
  StringRef getName() const { return StringRef(NameCStr, NameLength); }
};

/// Table of CSKY architecture-extension names and feature strings.
const CSKY::ExtName CSKYARCHExtNames[] = {
#define CSKY_ARCH_EXT_NAME(NAME, ID, FEATURE, NEGFEATURE)                      \
  {NAME, sizeof(NAME) - 1, ID, FEATURE, NEGFEATURE},
#include "CSKYTargetParser.def"
};

/// Named CSKY CPU, its architecture, and its extra default extensions.
template <typename T> struct CpuNames {
  const char *NameCStr; ///< NUL-terminated CPU name.
  size_t NameLength;    ///< Length of \ref NameCStr excluding the terminator.
  T ArchID;             ///< Architecture kind for this CPU.
  uint64_t defaultExt;  ///< Extra default \c ArchExtKind bits for this CPU.

  /// Return the CPU name as a StringRef.
  ///
  /// \returns CPU name.
  StringRef getName() const { return StringRef(NameCStr, NameLength); }
};
/// Table of CSKY CPU names and their architectures.
const CpuNames<CSKY::ArchKind> CPUNames[] = {
#define CSKY_CPU_NAME(NAME, ARCH_ID, DEFAULT_EXT)                              \
  {NAME, sizeof(NAME) - 1, CSKY::ArchKind::ARCH_ID, DEFAULT_EXT},
#include "llvm/TargetParser/CSKYTargetParser.def"
};

/// Named CSKY FPU kind, its identifier, and its FPU version.
///
/// Table entries must appear in \c CSKYFPUKind order for correct indexing.
// FIXME: TableGen this.
struct FPUName {
  const char *NameCStr; ///< NUL-terminated FPU name.
  size_t NameLength;    ///< Length of \ref NameCStr excluding the terminator.
  CSKYFPUKind ID;       ///< \c CSKYFPUKind identifier for this FPU.
  FPUVersion FPUVer;    ///< FPU version associated with this kind.

  /// Return the FPU name as a StringRef.
  ///
  /// \returns FPU name.
  StringRef getName() const { return StringRef(NameCStr, NameLength); }
};

static const FPUName FPUNames[] = {
#define CSKY_FPU(NAME, KIND, VERSION) {NAME, sizeof(NAME) - 1, KIND, VERSION},
#include "llvm/TargetParser/CSKYTargetParser.def"
};

/// Named CSKY architecture and its base extension bitmask.
template <typename T> struct ArchNames {
  const char *NameCStr; ///< NUL-terminated architecture name.
  size_t NameLength;    ///< Length of \ref NameCStr excluding the terminator.
  T ID;                 ///< Architecture kind identifier.
  uint64_t archBaseExt; ///< Base \c ArchExtKind bits for this architecture.
  /// Return the architecture name as a StringRef.
  ///
  /// \returns Architecture name.
  StringRef getName() const { return StringRef(NameCStr, NameLength); }
};
/// Table of canonical CSKY architecture names.
const ArchNames<CSKY::ArchKind> ARCHNames[] = {
#define CSKY_ARCH(NAME, ID, ARCH_BASE_EXT)                                     \
  {NAME, sizeof(NAME) - 1, CSKY::ArchKind::ID, ARCH_BASE_EXT},
#include "llvm/TargetParser/CSKYTargetParser.def"
};

/// Return the canonical architecture name for \p AK.
///
/// \param AK Architecture kind to look up.
/// \returns Architecture name, such as "ck803".
LLVM_ABI StringRef getArchName(ArchKind AK);
/// Return the default CPU name for architecture \p Arch.
///
/// The default CPU name matches the architecture name when \p Arch is valid.
///
/// \param Arch Architecture name to look up.
/// \returns The architecture name itself, or an empty string if unrecognized.
LLVM_ABI StringRef getDefaultCPU(StringRef Arch);
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
/// \returns Target-feature string such as "+hwdiv", or empty if unknown.
LLVM_ABI StringRef getArchExtFeature(StringRef ArchExt);
/// Return the default extension bitmask for CPU name \p CPU.
///
/// Combines the CPU's architecture base extensions with its extra defaults.
///
/// \param CPU CPU name to look up.
/// \returns Combined \c ArchExtKind bitmask, or \c AEK_INVALID if unknown.
LLVM_ABI uint64_t getDefaultExtensions(StringRef CPU);
/// Append +feature strings for each bit set in \p Extensions.
///
/// \param Extensions Bitmask of \c ArchExtKind flags.
/// \param Features Vector that receives enabled feature strings.
/// \returns False if \p Extensions is \c AEK_INVALID; true otherwise.
LLVM_ABI bool getExtensionFeatures(uint64_t Extensions,
                                   std::vector<StringRef> &Features);

// Information by ID
/// Return the FPU name for identifier \p FPUKind.
///
/// \param FPUKind FPU kind from \c CSKYFPUKind.
/// \returns FPU name, or an empty string if \p FPUKind is out of range.
LLVM_ABI StringRef getFPUName(unsigned FPUKind);
/// Return the FPU version for identifier \p FPUKind.
///
/// \param FPUKind FPU kind from \c CSKYFPUKind.
/// \returns Matching \c FPUVersion, or \c FPUVersion::NONE if out of range.
LLVM_ABI FPUVersion getFPUVersion(unsigned FPUKind);

/// Append +feature strings enabled by FPU kind \p Kind.
///
/// \param Kind FPU kind to expand.
/// \param Features Vector that receives enabled FPU feature strings.
/// \returns False if \p Kind is invalid or \c FK_LAST; true otherwise.
LLVM_ABI bool getFPUFeatures(CSKYFPUKind Kind,
                             std::vector<StringRef> &Features);

// Parser
/// Parse architecture name \p Arch into an \c ArchKind.
///
/// \param Arch Architecture name such as "ck803".
/// \returns Matching \c ArchKind, or \c ArchKind::INVALID if unrecognized.
LLVM_ABI ArchKind parseArch(StringRef Arch);
/// Parse CPU name \p CPU into its \c ArchKind.
///
/// \param CPU CPU name such as "ck803f".
/// \returns Architecture of that CPU, or \c ArchKind::INVALID if unrecognized.
LLVM_ABI ArchKind parseCPUArch(StringRef CPU);
/// Parse architecture-extension name \p ArchExt into its bitmask.
///
/// \param ArchExt Extension name such as "hwdiv".
/// \returns Matching \c ArchExtKind bit, or \c AEK_INVALID if unrecognized.
LLVM_ABI uint64_t parseArchExt(StringRef ArchExt);
/// Append every valid CSKY CPU name to \p Values.
///
/// Skips the invalid sentinel entry.
///
/// \param Values Vector that receives CPU names.
LLVM_ABI void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values);

} // namespace CSKY

} // namespace llvm

#endif
