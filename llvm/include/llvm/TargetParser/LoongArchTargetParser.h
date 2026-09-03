//==-- LoongArch64TargetParser - Parser for LoongArch64 features --*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise LoongArch hardware features
// such as CPU/ARCH and extension names.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_LOONGARCHTARGETPARSER_H
#define LLVM_TARGETPARSER_LOONGARCHTARGETPARSER_H

#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <vector>

namespace llvm {
class StringRef;

/// LoongArch CPU, architecture, and extension name parsing helpers.
namespace LoongArch {

/// LoongArch architecture-extension bit flags selectable on a CPU.
enum FeatureKind : uint32_t {
  /// 32-bit ISA is available.
  FK_32BIT = 1 << 0,

  /// 64-bit ISA is available.
  FK_64BIT = 1 << 1,

  /// Single-precision floating-point instructions are available.
  FK_FP32 = 1 << 2,

  /// Double-precision floating-point instructions are available.
  FK_FP64 = 1 << 3,

  /// Loongson SIMD Extension is available.
  FK_LSX = 1 << 4,

  /// Loongson Advanced SIMD Extension is available.
  FK_LASX = 1 << 5,

  /// Loongson Binary Translation Extension is available.
  FK_LBT = 1 << 6,

  /// Loongson Virtualization Extension is available.
  FK_LVZ = 1 << 7,

  /// Allow memory accesses to be unaligned.
  FK_UAL = 1 << 8,

  /// Floating-point approximate reciprocal instructions are available.
  FK_FRECIPE = 1 << 9,

  /// Atomic memory swap and add instructions for byte and half word are
  /// available.
  FK_LAM_BH = 1 << 10,

  /// Atomic memory compare and swap instructions for byte, half word, word and
  /// double word are available.
  FK_LAMCAS = 1 << 11,

  /// Do not generate load-load barrier instructions (dbar 0x700).
  FK_LD_SEQ_SA = 1 << 12,

  /// Assume div.w[u] and mod.w[u] can handle inputs that are not sign-extended.
  FK_DIV32 = 1 << 13,

  /// sc.q is available.
  FK_SCQ = 1 << 14,

  /// 32-bit standard variant is available.
  FK_32S = 1 << 15,
};

/// Named LoongArch feature and its \c FeatureKind bit.
struct FeatureInfo {
  StringRef Name;   ///< Target-feature string, typically with a leading '+'.
  FeatureKind Kind; ///< \c FeatureKind bit flag for this feature.
};

/// LoongArch architecture kind identifiers.
///
/// Values are generated from LoongArchTargetParser.def:
/// - AK_LOONGARCH32: Generic 32-bit LoongArch.
/// - AK_LOONGARCH64: Generic 64-bit LoongArch.
/// - AK_LA464: LA464 microarchitecture.
/// - AK_LA664: LA664 microarchitecture.
enum class ArchKind {
#define LOONGARCH_ARCH(NAME, KIND, FEATURES) KIND,
#include "LoongArchTargetParser.def"
};

/// Named LoongArch architecture and its default feature bitmask.
struct ArchInfo {
  StringRef Name;  ///< Canonical architecture or CPU name.
  ArchKind Kind;   ///< Architecture kind identifier.
  uint32_t Features; ///< Bitmask of \c FeatureKind flags enabled by default.
};

/// Return true if \p Arch is a recognized LoongArch architecture name.
///
/// \param Arch Architecture name to validate.
/// \returns True if \p Arch matches a known architecture.
LLVM_ABI bool isValidArchName(StringRef Arch);
/// Return true if \p Feature is a recognized LoongArch feature name.
///
/// Names must not include a leading '+' or '-'.
///
/// \param Feature Feature name without a +/- prefix.
/// \returns True if \p Feature matches a known feature.
LLVM_ABI bool isValidFeatureName(StringRef Feature);
/// Append +feature strings enabled by architecture name \p Arch.
///
/// Also accepts ISA profile names such as "la64v1.0" and "la32v1.0".
///
/// \param Arch Architecture or ISA profile name.
/// \param Features Vector that receives enabled feature strings.
/// \returns True if \p Arch is recognized; false otherwise.
LLVM_ABI bool getArchFeatures(StringRef Arch, std::vector<StringRef> &Features);
/// Return true if \p TuneCPU is a recognized LoongArch CPU name.
///
/// CPU names currently match the architecture names from \c ArchInfo.
///
/// \param TuneCPU CPU name to validate.
/// \returns True if \p TuneCPU is a known CPU.
LLVM_ABI bool isValidCPUName(StringRef TuneCPU);
/// Append every valid LoongArch CPU name to \p Values.
///
/// \param Values Vector that receives CPU names.
LLVM_ABI void fillValidCPUList(SmallVectorImpl<StringRef> &Values);
/// Return the default LoongArch architecture name for the given bitness.
///
/// \param Is64Bit True to select the 64-bit default; false for 32-bit.
/// \returns "loongarch64" if \p Is64Bit is true, otherwise "loongarch32".
LLVM_ABI StringRef getDefaultArch(bool Is64Bit);

} // namespace LoongArch

} // namespace llvm

#endif // LLVM_TARGETPARSER_LOONGARCHTARGETPARSER_H
