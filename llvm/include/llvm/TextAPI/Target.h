//===- llvm/TextAPI/Target.h - TAPI Target ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_TARGET_H
#define LLVM_TEXTAPI_TARGET_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/TextAPI/Platform.h"

namespace llvm {

class Triple;

namespace MachO {

/// TextAPI target combining architecture, platform, and deployment version.
///
/// This is similar to a llvm Triple, but the triple doesn't have all the
/// information we need. For example there is no enum value for x86_64h. The
/// only way to get that information is to parse the triple string.
class Target {
public:
  /// Construct an empty target with unknown architecture and platform.
  Target() = default;

  /// Construct a target from architecture, platform, and optional deployment.
  ///
  /// \param Arch The architecture slice.
  /// \param Platform The platform type.
  /// \param MinDeployment The minimum deployment version, if any.
  Target(Architecture Arch, PlatformType Platform,
         VersionTuple MinDeployment = {})
      : Arch(Arch), Platform(Platform), MinDeployment(MinDeployment) {}

  /// Construct a target by mapping fields from an LLVM triple.
  ///
  /// \param Triple The LLVM triple to map from.
  explicit Target(const llvm::Triple &Triple)
      : Arch(mapToArchitecture(Triple)), Platform(mapToPlatformType(Triple)),
        MinDeployment(mapToSupportedOSVersion(Triple)) {}

  /// Create a target by parsing a target string.
  ///
  /// \param Target The target string to parse.
  /// \return The parsed target, or an error on failure.
  LLVM_ABI static llvm::Expected<Target> create(StringRef Target);

  /// Return whether this target has a known architecture and platform.
  ///
  /// \return True if architecture and platform are both known.
  bool isValid() const {
    return Arch != AK_unknown && Platform != PLATFORM_UNKNOWN;
  }

  /// Convert this target to a string representation.
  ///
  /// \return A string describing the target.
  LLVM_ABI operator std::string() const;

  /// Architecture slice of this target.
  Architecture Arch;
  /// Platform of this target.
  PlatformType Platform;
  /// Minimum deployment version for this target.
  VersionTuple MinDeployment;
};

/// Compare two targets for equality of architecture and platform.
///
/// In most cases the deployment version is not useful to compare.
///
/// \param LHS The left-hand target.
/// \param RHS The right-hand target.
/// \return True if architecture and platform match.
inline bool operator==(const Target &LHS, const Target &RHS) {
  // In most cases the deployment version is not useful to compare.
  return std::tie(LHS.Arch, LHS.Platform) == std::tie(RHS.Arch, RHS.Platform);
}

/// Compare two targets for inequality of architecture or platform.
///
/// \param LHS The left-hand target.
/// \param RHS The right-hand target.
/// \return True if architecture or platform differ.
inline bool operator!=(const Target &LHS, const Target &RHS) {
  return !(LHS == RHS);
}

/// Order two targets by architecture and platform.
///
/// In most cases the deployment version is not useful to compare.
///
/// \param LHS The left-hand target.
/// \param RHS The right-hand target.
/// \return True if \p LHS is ordered before \p RHS.
inline bool operator<(const Target &LHS, const Target &RHS) {
  // In most cases the deployment version is not useful to compare.
  return std::tie(LHS.Arch, LHS.Platform) < std::tie(RHS.Arch, RHS.Platform);
}

/// Compare a target's architecture against an architecture value.
///
/// \param LHS The target whose architecture is compared.
/// \param RHS The architecture to compare against.
/// \return True if the target's architecture equals \p RHS.
inline bool operator==(const Target &LHS, const Architecture &RHS) {
  return LHS.Arch == RHS;
}

/// Compare a target's architecture for inequality against an architecture.
///
/// \param LHS The target whose architecture is compared.
/// \param RHS The architecture to compare against.
/// \return True if the target's architecture differs from \p RHS.
inline bool operator!=(const Target &LHS, const Architecture &RHS) {
  return LHS.Arch != RHS;
}

/// Map a list of targets to the set of platform and version pairs.
///
/// \param Targets The targets to map.
/// \return The set of platform and minimum-deployment pairs.
LLVM_ABI PlatformVersionSet mapToPlatformVersionSet(ArrayRef<Target> Targets);

/// Map a list of targets to the set of platforms.
///
/// \param Targets The targets to map.
/// \return The set of platforms.
LLVM_ABI PlatformSet mapToPlatformSet(ArrayRef<Target> Targets);

/// Map a list of targets to the set of architectures.
///
/// \param Targets The targets to map.
/// \return The set of architectures.
LLVM_ABI ArchitectureSet mapToArchitectureSet(ArrayRef<Target> Targets);

/// Build a target triple name string for a TextAPI target.
///
/// \param Targ The target to format.
/// \return The target triple name.
LLVM_ABI std::string getTargetTripleName(const Target &Targ);

/// Write a target to an output stream.
///
/// \param OS The output stream.
/// \param Target The target to print.
/// \return The output stream.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const Target &Target);

} // namespace MachO
} // namespace llvm

#endif // LLVM_TEXTAPI_TARGET_H
