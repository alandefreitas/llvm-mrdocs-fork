//===-- NVPTXTargetParser.h - Parser for NVPTX target ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_NVPTXTARGETPARSER_H
#define LLVM_TARGETPARSER_NVPTXTARGETPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {
/// NVPTX architecture and GPU name parsing helpers.
namespace NVPTX {

/// GPU kinds supported by the NVPTX target.
enum GPUKind : uint8_t {
  /// Unspecified or unrecognized GPU kind.
  GK_NONE = 0,
/// Expand one NVPTX GPU enumerator from NVPTXTargetParser.def.
///
/// \param NAME Canonical processor name string, e.g. "sm_90".
/// \param KIND GPUKind enumerator suffix; the enumerator is GK_<KIND>.
/// \param VIRTUAL Virtual (compute_) arch name string, e.g. "compute_90".
/// \param SM_ID Numeric compute-capability id (sm_90 -> 900).
/// \param MIN_VER Earliest supporting CudaVersion.
/// \param MAX_VER Latest supporting CudaVersion.
/// \param SUFFIX Arch suffix class: NONE, ACCELERATED, or FAMILY.
#define NVPTX_GPU(NAME, KIND, VIRTUAL, SM_ID, MIN_VER, MAX_VER, SUFFIX)        \
  GK_##KIND,
#include "llvm/TargetParser/NVPTXTargetParser.def"

  /// Alias for the last GPUKind. Keep in sync with the final .def row.
  ///
  /// FIXME: Should be generated once the GPU list moves to TableGen.
  GK_LAST = GK_SM_121f,
};

/// Suffix class of an NVPTX architecture name. Enumerator spellings match the
/// SUFFIX column tokens in NVPTXTargetParser.def.
enum class ArchSuffix {
  NONE,        ///< No special suffix (e.g. sm_90).
  ACCELERATED, ///< Accelerated variant suffix 'a' (e.g. sm_90a).
  FAMILY,      ///< Family-specific variant suffix 'f' (e.g. sm_90f).
};

/// Parse \p CPU (e.g. "sm_90") into a GPUKind, or GK_NONE if unrecognized.
///
/// \param CPU Processor or architecture name to parse.
/// \returns Matching \c GPUKind, or \c GK_NONE if unrecognized.
LLVM_ABI GPUKind parseArch(StringRef CPU);

/// Return the canonical processor name (e.g. "sm_90") for \p Kind, or "" if
/// \p Kind is GK_NONE.
///
/// \param Kind GPU kind to look up.
/// \returns Canonical processor name, or an empty string if \p Kind is
/// \c GK_NONE.
LLVM_ABI StringRef getArchName(GPUKind Kind);

/// Return the virtual (compute_) arch name (e.g. "compute_90") for \p Kind, or
/// "" if \p Kind is GK_NONE.
///
/// \param Kind GPU kind to look up.
/// \returns Virtual architecture name, or an empty string if \p Kind is
/// \c GK_NONE.
LLVM_ABI StringRef getVirtualArch(GPUKind Kind);

/// Return the numeric compute-capability id (e.g. sm_90 -> 900) for \p Kind, or
/// 0 if \p Kind is GK_NONE.
///
/// \param Kind GPU kind to look up.
/// \returns Numeric SM version id, or 0 if \p Kind is \c GK_NONE.
LLVM_ABI unsigned getSmVersion(GPUKind Kind);

/// Return the suffix class of \p Kind.
///
/// \param Kind GPU kind to look up.
/// \returns Suffix class of \p Kind.
LLVM_ABI ArchSuffix getArchSuffix(GPUKind Kind);

/// Whether \p Kind is an accelerated variant (e.g. sm_90a).
///
/// \param Kind GPU kind to test.
/// \returns True if \p Kind is an accelerated variant.
inline bool isAcceleratedArch(GPUKind Kind) {
  return getArchSuffix(Kind) == ArchSuffix::ACCELERATED;
}

/// Whether \p Kind is a family-specific variant (e.g. sm_90f) or accelerated.
///
/// \param Kind GPU kind to test.
/// \returns True if \p Kind is family-specific or accelerated.
inline bool isFamilySpecificArch(GPUKind Kind) {
  ArchSuffix S = getArchSuffix(Kind);
  return S == ArchSuffix::FAMILY || S == ArchSuffix::ACCELERATED;
}

/// Whether \p Kind supports unified addressing. Unified addressing was
/// introduced with the Pascal generation (sm_60).
///
/// \param Kind GPU kind to test.
/// \returns True if \p Kind supports unified addressing (sm_60 or later).
inline bool supportsUnifiedAddressing(GPUKind Kind) {
  return getSmVersion(Kind) >= 600;
}

} // namespace NVPTX
} // namespace llvm

#endif // LLVM_TARGETPARSER_NVPTXTARGETPARSER_H
