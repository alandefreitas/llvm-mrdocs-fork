//===-- X86TargetParser - Parser for X86 features ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise X86 hardware features.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_X86TARGETPARSER_H
#define LLVM_TARGETPARSER_X86TARGETPARSER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Compiler.h"
#include <array>

namespace llvm {
template <typename T> class SmallVectorImpl;
class StringRef;

/// X86 CPU name, feature, and tune-feature parsing helpers.
namespace X86 {

/// X86 CPU vendor identifiers used by \c __builtin_cpu_is and related ABI
/// checks.
///
/// Enumerators are generated from \c X86TargetParser.def via \c X86_VENDOR.
enum ProcessorVendors : unsigned {
#define X86_VENDOR(ENUM, STRING, ABI_VALUE) ENUM,
#include "llvm/TargetParser/X86TargetParser.def"
  CPU_VENDOR_MAX ///< Sentinel one past the last vendor enumerator.
};

/// X86 CPU type identifiers used by \c __builtin_cpu_is and related ABI checks.
///
/// Enumerators are generated from \c X86TargetParser.def via \c X86_CPU_TYPE.
enum ProcessorTypes : unsigned {
#define X86_CPU_TYPE(ENUM, STRING, ABI_VALUE) ENUM,
#include "llvm/TargetParser/X86TargetParser.def"
  CPU_TYPE_MAX ///< Sentinel one past the last CPU type enumerator.
};

/// X86 CPU subtype identifiers used by \c __builtin_cpu_is and related ABI
/// checks.
///
/// Enumerators are generated from \c X86TargetParser.def via
/// \c X86_CPU_SUBTYPE.
enum ProcessorSubtypes : unsigned {
#define X86_CPU_SUBTYPE(ENUM, STRING, ABI_VALUE) ENUM,
#include "llvm/TargetParser/X86TargetParser.def"
  CPU_SUBTYPE_MAX ///< Sentinel one past the last CPU subtype enumerator.
};

/// X86 processor feature and microarchitecture-level enumerators.
///
/// Feature enumerators are generated from \c X86TargetParser.def via
/// \c X86_FEATURE; microarchitecture levels follow \c CPU_FEATURE_MAX via
/// \c X86_MICROARCH_LEVEL.
enum ProcessorFeatures {
#define X86_FEATURE(ENUM, STRING) FEATURE_##ENUM,
#include "llvm/TargetParser/X86TargetParser.def"
  CPU_FEATURE_MAX, ///< Sentinel one past the last feature enumerator.
#define X86_MICROARCH_LEVEL(ENUM, STR, PRIORITY, ABI_VALUE) FEATURE_##ENUM,
#include "llvm/TargetParser/X86TargetParser.def"
};

/// X86 CPU kinds recognized by \c -march / \c -mtune and related parsers.
enum CPUKind {
  CK_None,            ///< No CPU / unrecognized name.
  CK_i386,            ///< Generic i386.
  CK_i486,            ///< Intel 80486.
  CK_WinChipC6,       ///< IDT WinChip C6.
  CK_WinChip2,        ///< IDT WinChip 2.
  CK_C3,              ///< VIA C3.
  CK_i586,            ///< Generic i586 / Pentium-class.
  CK_Pentium,         ///< Intel Pentium.
  CK_PentiumMMX,      ///< Intel Pentium MMX.
  CK_PentiumPro,      ///< Intel Pentium Pro.
  CK_i686,            ///< Generic i686.
  CK_Pentium2,        ///< Intel Pentium II.
  CK_Pentium3,        ///< Intel Pentium III.
  CK_PentiumM,        ///< Intel Pentium M.
  CK_C3_2,            ///< VIA C3-2 (Nehemiah).
  CK_Yonah,           ///< Intel Yonah (Core Duo / Solo).
  CK_Pentium4,        ///< Intel Pentium 4.
  CK_Prescott,        ///< Intel Prescott (Pentium 4).
  CK_Nocona,          ///< Intel Nocona.
  CK_Core2,           ///< Intel Core 2.
  CK_Penryn,          ///< Intel Penryn.
  CK_Bonnell,         ///< Intel Bonnell (Atom).
  CK_Silvermont,      ///< Intel Silvermont (Atom).
  CK_Goldmont,        ///< Intel Goldmont (Atom).
  CK_GoldmontPlus,    ///< Intel Goldmont Plus (Atom).
  CK_Tremont,         ///< Intel Tremont (Atom).
  CK_Gracemont,       ///< Intel Gracemont (Atom).
  CK_Nehalem,         ///< Intel Nehalem.
  CK_Westmere,        ///< Intel Westmere.
  CK_SandyBridge,     ///< Intel Sandy Bridge.
  CK_IvyBridge,       ///< Intel Ivy Bridge.
  CK_Haswell,         ///< Intel Haswell.
  CK_Broadwell,       ///< Intel Broadwell.
  CK_SkylakeClient,   ///< Intel Skylake (client).
  CK_SkylakeServer,   ///< Intel Skylake (server).
  CK_Cascadelake,     ///< Intel Cascade Lake.
  CK_Cooperlake,      ///< Intel Cooper Lake.
  CK_Cannonlake,      ///< Intel Cannon Lake.
  CK_IcelakeClient,   ///< Intel Ice Lake (client).
  CK_Rocketlake,      ///< Intel Rocket Lake.
  CK_IcelakeServer,   ///< Intel Ice Lake (server).
  CK_Tigerlake,       ///< Intel Tiger Lake.
  CK_SapphireRapids,  ///< Intel Sapphire Rapids.
  CK_Alderlake,       ///< Intel Alder Lake.
  CK_Raptorlake,      ///< Intel Raptor Lake.
  CK_Meteorlake,      ///< Intel Meteor Lake.
  CK_Arrowlake,       ///< Intel Arrow Lake.
  CK_ArrowlakeS,      ///< Intel Arrow Lake S.
  CK_Lunarlake,       ///< Intel Lunar Lake.
  CK_Pantherlake,     ///< Intel Panther Lake.
  CK_Wildcatlake,      ///< Intel Wildcat Lake.
  CK_Novalake,        ///< Intel Nova Lake.
  CK_Sierraforest,     ///< Intel Sierra Forest.
  CK_Grandridge,       ///< Intel Grand Ridge.
  CK_Graniterapids,    ///< Intel Granite Rapids.
  CK_GraniterapidsD,   ///< Intel Granite Rapids D.
  CK_Emeraldrapids,    ///< Intel Emerald Rapids.
  CK_Clearwaterforest, ///< Intel Clearwater Forest.
  CK_Diamondrapids,    ///< Intel Diamond Rapids.
  CK_KNL,             ///< Intel Knights Landing.
  CK_KNM,             ///< Intel Knights Mill.
  CK_Lakemont,        ///< Intel Lakemont.
  CK_K6,              ///< AMD K6.
  CK_K6_2,            ///< AMD K6-2.
  CK_K6_3,            ///< AMD K6-III.
  CK_Athlon,          ///< AMD Athlon.
  CK_AthlonXP,        ///< AMD Athlon XP.
  CK_K8,              ///< AMD K8 (Athlon 64).
  CK_K8SSE3,          ///< AMD K8 with SSE3.
  CK_AMDFAM10,        ///< AMD Family 10h (Barcelona and related).
  CK_BTVER1,          ///< AMD Bobcat (btver1).
  CK_BTVER2,          ///< AMD Jaguar (btver2).
  CK_BDVER1,          ///< AMD Bulldozer (bdver1).
  CK_BDVER2,          ///< AMD Piledriver (bdver2).
  CK_BDVER3,          ///< AMD Steamroller (bdver3).
  CK_BDVER4,          ///< AMD Excavator (bdver4).
  CK_ZNVER1,          ///< AMD Zen 1 (znver1).
  CK_ZNVER2,          ///< AMD Zen 2 (znver2).
  CK_ZNVER3,          ///< AMD Zen 3 (znver3).
  CK_ZNVER4,          ///< AMD Zen 4 (znver4).
  CK_ZNVER5,          ///< AMD Zen 5 (znver5).
  CK_ZNVER6,          ///< AMD Zen 6 (znver6).
  CK_C86_4G_M4,       ///< Centaur C86 4G M4.
  CK_C86_4G_M6,       ///< Centaur C86 4G M6.
  CK_C86_4G_M7,       ///< Centaur C86 4G M7.
  CK_C86_4G_M8,       ///< Centaur C86 4G M8.
  CK_x86_64,          ///< Generic x86-64 baseline microarchitecture level.
  CK_x86_64_v2,       ///< x86-64-v2 microarchitecture level.
  CK_x86_64_v3,       ///< x86-64-v3 microarchitecture level.
  CK_x86_64_v4,       ///< x86-64-v4 microarchitecture level.
  CK_Geode,           ///< AMD Geode.
};

/// Parse \p CPU string into a CPUKind. Will only accept 64-bit capable CPUs if
/// \p Only64Bit is true.
///
/// \param CPU CPU name to parse.
/// \param Only64Bit If true, reject CPUs that are not 64-bit capable.
/// \returns Matching CPUKind, or \c CK_None if unrecognized.
LLVM_ABI CPUKind parseArchX86(StringRef CPU, bool Only64Bit = false);
/// Parse \p CPU string as an \c -mtune CPU name into a CPUKind.
///
/// \param CPU Tune CPU name to parse.
/// \param Only64Bit If true, reject CPUs that are not 64-bit capable.
/// \returns Matching CPUKind, or \c CK_None if unrecognized or not tuneable.
LLVM_ABI CPUKind parseTuneCPU(StringRef CPU, bool Only64Bit = false);

/// Provide a list of valid CPU names. If \p Only64Bit is true, the list will
/// only contain 64-bit capable CPUs.
///
/// \param Values Vector that receives valid \c -march CPU names.
/// \param Only64Bit If true, only include 64-bit capable CPUs.
LLVM_ABI void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values,
                                   bool Only64Bit = false);
/// Provide a list of valid \c -mtune names.
///
/// \param Values Vector that receives valid \c -mtune CPU names.
/// \param Only64Bit If true, only include 64-bit capable CPUs.
LLVM_ABI void fillValidTuneCPUList(SmallVectorImpl<StringRef> &Values,
                                   bool Only64Bit = false);

/// Get the key feature prioritizing target multiversioning.
///
/// \param Kind CPU whose key feature is requested.
/// \returns Key \c ProcessorFeatures enumerator for \p Kind.
LLVM_ABI ProcessorFeatures getKeyFeature(CPUKind Kind);

/// Fill in the features that \p CPU supports into \p Features.
///
/// "+" will be appended in front of each feature if NeedPlus is true.
///
/// \param CPU CPU name whose features are requested.
/// \param Features Vector that receives feature strings.
/// \param NeedPlus If true, prefix each feature with '+'.
LLVM_ABI void getFeaturesForCPU(StringRef CPU,
                                SmallVectorImpl<StringRef> &Features,
                                bool NeedPlus = false);

/// Set or clear entries in \p Features that are implied to be enabled/disabled
/// by the provided \p Feature.
///
/// \param Feature Feature name whose implications are applied.
/// \param Enabled True to enable implied features; false to disable them.
/// \param Features Map of feature names to enabled/disabled state to update.
LLVM_ABI void updateImpliedFeatures(StringRef Feature, bool Enabled,
                                    StringMap<bool> &Features);

/// Return the function-multiversion mangling character for CPU \p Name.
///
/// \param Name CPU name that supports \c cpu_dispatch / \c cpu_specific.
/// \returns Single-character mangling used in the dispatch resolver.
LLVM_ABI char getCPUDispatchMangling(StringRef Name);
/// Return true if \p Name is a valid \c cpu_specific / \c cpu_dispatch CPU
/// name.
///
/// \param Name CPU name to validate.
/// \returns True if \p Name is a known processor supporting CPU dispatch.
LLVM_ABI bool validateCPUSpecificCPUDispatch(StringRef Name);
/// Build the 128-bit feature mask for \c __builtin_cpu_supports from feature
/// names.
///
/// \param FeatureStrs Feature name strings to encode into the mask.
/// \returns Four 32-bit words forming the ABI feature bitset.
LLVM_ABI std::array<uint32_t, 4>
getCpuSupportsMask(ArrayRef<StringRef> FeatureStrs);
/// Return the function-multiversion priority of feature \p Feat.
///
/// \param Feat Processor feature whose priority is requested.
/// \returns Priority value used when ranking multiversion candidates.
LLVM_ABI unsigned getFeaturePriority(ProcessorFeatures Feat);

} // namespace X86
} // namespace llvm

#endif
