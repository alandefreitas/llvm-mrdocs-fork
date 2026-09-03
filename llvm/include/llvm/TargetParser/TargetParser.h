//===-- TargetParser - Parser for target features ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise hardware features such as
// FPU/CPU/ARCH names as well as specific support such as HDIV, etc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_TARGETPARSER_H
#define LLVM_TARGETPARSER_TARGETPARSER_H

#include "SubtargetFeature.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {

/// Used to provide key value pairs for subtarget feature bit flags.
struct BasicSubtargetFeatureKV {
  const char *Key;         ///< K-V key string
  unsigned Value;          ///< K-V integer value
  FeatureBitArray Implies; ///< K-V bit mask

  /// Compare routine for std::lower_bound
  /// \param S Key string to compare against this entry's Key.
  /// \returns True if this entry's Key is lexicographically less than \p S.
  bool operator<(StringRef S) const { return StringRef(Key) < S; }
};

/// Used to provide key value pairs for feature and CPU bit flags.
struct BasicSubtargetSubTypeKV {
  const char *Key;         ///< K-V key string
  FeatureBitArray Implies; ///< K-V bit mask

  /// Compare routine for std::lower_bound
  /// \param S Key string to compare against this entry's Key.
  /// \returns True if this entry's Key is lexicographically less than \p S.
  bool operator<(StringRef S) const { return StringRef(Key) < S; }

  /// Compare routine for std::is_sorted.
  /// \param Other Entry whose Key is compared with this entry's Key.
  /// \returns True if this entry's Key is lexicographically less than
  /// \p Other's Key.
  bool operator<(const BasicSubtargetSubTypeKV &Other) const {
    return StringRef(Key) < StringRef(Other.Key);
  }
};

/// Returns the default enabled features for \p CPU, or nullopt if unknown.
///
/// Looks up \p CPU in \p ProcDesc and expands transitively implied features
/// from \p ProcFeatures into a name-to-enabled map.
///
/// \param CPU CPU name to look up.
/// \param ProcDesc Sorted table of CPU/subtype entries and implied features.
/// \param ProcFeatures Feature table used to expand implied feature bits.
/// \returns Map from feature name to true for each feature implied by \p CPU,
/// or \c std::nullopt if \p CPU is empty or not found in \p ProcDesc.
LLVM_ABI std::optional<llvm::StringMap<bool>>
getCPUDefaultTargetFeatures(StringRef CPU,
                            ArrayRef<BasicSubtargetSubTypeKV> ProcDesc,
                            ArrayRef<BasicSubtargetFeatureKV> ProcFeatures);
} // namespace llvm

#endif
