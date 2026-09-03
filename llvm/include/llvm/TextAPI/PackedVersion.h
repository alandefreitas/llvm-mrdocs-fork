//===- llvm/TextAPI/PackedVersion.h - PackedVersion -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the Mach-O packed version format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_PACKEDVERSION_H
#define LLVM_TEXTAPI_PACKEDVERSION_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/VersionTuple.h"
#include <cstdint>
#include <string>
#include <utility>

namespace llvm {
class raw_ostream;
class StringRef;

namespace MachO {

/// Mach-O packed version number stored as a 32-bit value.
class PackedVersion {
  uint32_t Version{0};

public:
  /// Construct an empty packed version with value zero.
  constexpr PackedVersion() = default;

  /// Construct a packed version from a raw 32-bit encoding.
  ///
  /// \param RawVersion The packed major, minor, and subminor bits.
  constexpr PackedVersion(uint32_t RawVersion) : Version(RawVersion) {}

  /// Construct a packed version from major, minor, and subminor components.
  ///
  /// \param Major The major version number.
  /// \param Minor The minor version number.
  /// \param Subminor The subminor version number.
  PackedVersion(unsigned Major, unsigned Minor, unsigned Subminor)
      : Version((Major << 16) | ((Minor & 0xff) << 8) | (Subminor & 0xff)) {}

  /// Construct a packed version from a version tuple.
  ///
  /// Missing minor or subminor components are treated as zero.
  ///
  /// \param VT The version tuple to pack.
  PackedVersion(VersionTuple VT) {
    unsigned Minor = 0, Subminor = 0;
    if (auto VTMinor = VT.getMinor())
      Minor = *VTMinor;
    if (auto VTSub = VT.getSubminor())
      Subminor = *VTSub;
    *this = PackedVersion(VT.getMajor(), Minor, Subminor);
  }

  /// Return whether the packed version is zero.
  ///
  /// \return True if the version is empty.
  bool empty() const { return Version == 0; }

  /// Retrieve the major version number.
  ///
  /// \return The major component of the packed version.
  unsigned getMajor() const { return Version >> 16; }

  /// Retrieve the minor version number, if provided.
  ///
  /// \return The minor component of the packed version.
  unsigned getMinor() const { return (Version >> 8) & 0xff; }

  /// Retrieve the subminor version number, if provided.
  ///
  /// \return The subminor component of the packed version.
  unsigned getSubminor() const { return Version & 0xff; }

  /// Parse a dotted version string into a 32-bit packed version.
  ///
  /// \param Str The version string to parse.
  /// \return True if parsing succeeded.
  LLVM_ABI bool parse32(StringRef Str);

  /// Parse a dotted version string, truncating components that exceed 32-bit
  /// packed limits.
  ///
  /// \param Str The version string to parse.
  /// \return A pair of success and whether any component was truncated.
  LLVM_ABI std::pair<bool, bool> parse64(StringRef Str);

  /// Compare two packed versions for less-than ordering.
  ///
  /// \param O The other packed version to compare against.
  /// \return True if this version is less than \p O.
  bool operator<(const PackedVersion &O) const { return Version < O.Version; }

  /// Compare two packed versions for equality.
  ///
  /// \param O The other packed version to compare against.
  /// \return True if both versions encode the same value.
  bool operator==(const PackedVersion &O) const { return Version == O.Version; }

  /// Compare two packed versions for inequality.
  ///
  /// \param O The other packed version to compare against.
  /// \return True if the versions encode different values.
  bool operator!=(const PackedVersion &O) const { return Version != O.Version; }

  /// Return the raw 32-bit packed version encoding.
  ///
  /// \return The underlying packed version bits.
  uint32_t rawValue() const { return Version; }

  /// Convert the packed version to a dotted string.
  ///
  /// \return A string such as "1.2.3".
  LLVM_ABI operator std::string() const;

  /// Print the packed version as a dotted string.
  ///
  /// \param OS The output stream.
  LLVM_ABI void print(raw_ostream &OS) const;
};

/// Write a packed version to an output stream.
///
/// \param OS The output stream.
/// \param Version The packed version to print.
/// \return The output stream.
inline raw_ostream &operator<<(raw_ostream &OS, const PackedVersion &Version) {
  Version.print(OS);
  return OS;
}

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_PACKEDVERSION_H
