//===- VersionTuple.h - Version Number Handling -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the llvm::VersionTuple class, which represents a version in
/// the form major[.minor[.subminor]].
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_SUPPORT_VERSIONTUPLE_H
#define LLVM_SUPPORT_VERSIONTUPLE_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/Compiler.h"
#include <optional>
#include <string>
#include <tuple>

namespace llvm {
template <typename HasherT, llvm::endianness Endianness> class HashBuilder;
class raw_ostream;
class StringRef;

/// Represents a version number in the form major[.minor[.subminor[.build]]].
class VersionTuple {
  unsigned Major : 32;

  unsigned Minor : 31;
  unsigned HasMinor : 1;

  unsigned Subminor : 31;
  unsigned HasSubminor : 1;

  unsigned Build : 20;
  unsigned Subbuild : 10;
  unsigned HasBuild : 1;
  unsigned HasSubbuild : 1;

public:
  /// Construct an empty version with no components set.
  constexpr VersionTuple()
      : Major(0), Minor(0), HasMinor(false), Subminor(0), HasSubminor(false),
        Build(0), Subbuild(0), HasBuild(false), HasSubbuild(false) {}

  /// Construct a version with only a major component.
  ///
  /// \param Major Major version number.
  explicit constexpr VersionTuple(unsigned Major)
      : Major(Major), Minor(0), HasMinor(false), Subminor(0),
        HasSubminor(false), Build(0), Subbuild(0), HasBuild(false),
        HasSubbuild(false) {}

  /// Construct a version with major and minor components.
  ///
  /// \param Major Major version number.
  /// \param Minor Minor version number.
  explicit constexpr VersionTuple(unsigned Major, unsigned Minor)
      : Major(Major), Minor(Minor), HasMinor(true), Subminor(0),
        HasSubminor(false), Build(0), Subbuild(0), HasBuild(false),
        HasSubbuild(false) {}

  /// Construct a version with major, minor, and subminor components.
  ///
  /// \param Major Major version number.
  /// \param Minor Minor version number.
  /// \param Subminor Subminor version number.
  explicit constexpr VersionTuple(unsigned Major, unsigned Minor,
                                  unsigned Subminor)
      : Major(Major), Minor(Minor), HasMinor(true), Subminor(Subminor),
        HasSubminor(true), Build(0), Subbuild(0), HasBuild(false),
        HasSubbuild(false) {}

  /// Construct a version with major through build components.
  ///
  /// \param Major Major version number.
  /// \param Minor Minor version number.
  /// \param Subminor Subminor version number.
  /// \param Build Build version number.
  explicit constexpr VersionTuple(unsigned Major, unsigned Minor,
                                  unsigned Subminor, unsigned Build)
      : Major(Major), Minor(Minor), HasMinor(true), Subminor(Subminor),
        HasSubminor(true), Build(Build), Subbuild(0), HasBuild(true),
        HasSubbuild(false) {}

  /// Construct a version with major through subbuild components.
  ///
  /// \param Major Major version number.
  /// \param Minor Minor version number.
  /// \param Subminor Subminor version number.
  /// \param Build Build version number.
  /// \param Subbuild Subbuild version number.
  explicit constexpr VersionTuple(unsigned Major, unsigned Minor,
                                  unsigned Subminor, unsigned Build,
                                  unsigned Subbuild)
      : Major(Major), Minor(Minor), HasMinor(true), Subminor(Subminor),
        HasSubminor(true), Build(Build), Subbuild(Subbuild), HasBuild(true),
        HasSubbuild(true) {}

  /// Version components as a (major, minor, subminor, build, subbuild) tuple.
  ///
  /// \returns A five-element tuple of the version components.
  std::tuple<unsigned, unsigned, unsigned, unsigned, unsigned> asTuple() const {
    return {Major, Minor, Subminor, Build, Subbuild};
  }

  /// Determine whether this version information is empty
  /// (e.g., all version components are zero).
  ///
  /// \returns \c true if all version components are zero.
  bool empty() const { return *this == VersionTuple(); }

  /// Retrieve the major version number.
  ///
  /// \returns The major version number.
  unsigned getMajor() const { return Major; }

  /// Retrieve the minor version number, if provided.
  ///
  /// \returns The minor number, or \c std::nullopt if unset.
  std::optional<unsigned> getMinor() const {
    if (!HasMinor)
      return std::nullopt;
    return Minor;
  }

  /// Retrieve the subminor version number, if provided.
  ///
  /// \returns The subminor number, or \c std::nullopt if unset.
  std::optional<unsigned> getSubminor() const {
    if (!HasSubminor)
      return std::nullopt;
    return Subminor;
  }

  /// Retrieve the build version number, if provided.
  ///
  /// \returns The build number, or \c std::nullopt if unset.
  std::optional<unsigned> getBuild() const {
    if (!HasBuild)
      return std::nullopt;
    return Build;
  }

  /// Retrieve the subbuild version number, if provided.
  ///
  /// \returns The subbuild number, or \c std::nullopt if unset.
  std::optional<unsigned> getSubbuild() const {
    if (!HasSubbuild)
      return std::nullopt;
    return Subbuild;
  }

  /// Return a version tuple that contains only the first 3 version components.
  ///
  /// \returns A version with major, minor, and subminor only.
  VersionTuple withoutBuild() const {
    if (HasBuild)
      return VersionTuple(Major, Minor, Subminor);
    return *this;
  }

  /// Return a version tuple that contains a different major version but
  /// everything else is the same.
  ///
  /// \param NewMajor Replacement major version number.
  /// \returns A copy of this version with the major component replaced.
  LLVM_ABI VersionTuple withMajorReplaced(unsigned NewMajor) const;

  /// Return a version tuple that contains only components that are non-zero.
  ///
  /// \returns A normalized version with trailing zero components cleared.
  VersionTuple normalize() const {
    VersionTuple Result = *this;
    if (Result.Subbuild == 0) {
      Result.HasSubbuild = false;
      if (Result.Build == 0) {
        Result.HasBuild = false;
        if (Result.Subminor == 0) {
          Result.HasSubminor = false;
          if (Result.Minor == 0)
            Result.HasMinor = false;
        }
      }
    }
    return Result;
  }

  /// Determine if two version numbers are equivalent. If not
  /// provided, minor and subminor version numbers are considered to be zero.
  ///
  /// \param X Left-hand version.
  /// \param Y Right-hand version.
  /// \returns \c true if the versions are equivalent.
  friend bool operator==(const VersionTuple &X, const VersionTuple &Y) {
    return X.asTuple() == Y.asTuple();
  }

  /// Determine if two version numbers are not equivalent.
  ///
  /// If not provided, minor and subminor version numbers are considered to be
  /// zero.
  ///
  /// \param X Left-hand version.
  /// \param Y Right-hand version.
  /// \returns \c true if the versions are not equivalent.
  friend bool operator!=(const VersionTuple &X, const VersionTuple &Y) {
    return !(X == Y);
  }

  /// Determine whether one version number precedes another.
  ///
  /// If not provided, minor and subminor version numbers are considered to be
  /// zero.
  ///
  /// \param X Left-hand version.
  /// \param Y Right-hand version.
  /// \returns \c true if \p X is less than \p Y.
  friend bool operator<(const VersionTuple &X, const VersionTuple &Y) {
    return X.asTuple() < Y.asTuple();
  }

  /// Determine whether one version number follows another.
  ///
  /// If not provided, minor and subminor version numbers are considered to be
  /// zero.
  ///
  /// \param X Left-hand version.
  /// \param Y Right-hand version.
  /// \returns \c true if \p X is greater than \p Y.
  friend bool operator>(const VersionTuple &X, const VersionTuple &Y) {
    return Y < X;
  }

  /// Determine whether one version number precedes or is
  /// equivalent to another.
  ///
  /// If not provided, minor and subminor version numbers are considered to be
  /// zero.
  ///
  /// \param X Left-hand version.
  /// \param Y Right-hand version.
  /// \returns \c true if \p X is less than or equal to \p Y.
  friend bool operator<=(const VersionTuple &X, const VersionTuple &Y) {
    return !(Y < X);
  }

  /// Determine whether one version number follows or is
  /// equivalent to another.
  ///
  /// If not provided, minor and subminor version numbers are considered to be
  /// zero.
  ///
  /// \param X Left-hand version.
  /// \param Y Right-hand version.
  /// \returns \c true if \p X is greater than or equal to \p Y.
  friend bool operator>=(const VersionTuple &X, const VersionTuple &Y) {
    return !(X < Y);
  }

  /// Compute a hash_code for version tuple \p VT.
  ///
  /// \param VT Version tuple to hash.
  /// \returns Hash code for the version components.
  friend hash_code hash_value(const VersionTuple &VT) {
    return hash_combine(VT.Major, VT.Minor, VT.Subminor, VT.Build, VT.Subbuild);
  }

  /// Feed version components of \p VT into hash builder \p HBuilder.
  ///
  /// \param HBuilder Hash builder that receives the components.
  /// \param VT Version tuple whose components are hashed.
  template <typename HasherT, llvm::endianness Endianness>
  friend void addHash(HashBuilder<HasherT, Endianness> &HBuilder,
                      const VersionTuple &VT) {
    HBuilder.add(VT.Major, VT.Minor, VT.Subminor, VT.Build, VT.Subbuild);
  }

  /// Retrieve a string representation of the version number.
  ///
  /// \returns String form of the version number.
  LLVM_ABI std::string getAsString() const;

  /// Try to parse the given string as a version number.
  ///
  /// \param string Input text to parse as a version.
  /// \returns \c true if the string does not match the regular expression
  ///   [0-9]+(\.[0-9]+){0,3}
  LLVM_ABI bool tryParse(StringRef string);
};

/// Print a version number.
///
/// \param Out Stream to write to.
/// \param V Version tuple to print.
/// \returns The output stream \p Out.
LLVM_ABI raw_ostream &operator<<(raw_ostream &Out, const VersionTuple &V);

/// Provide DenseMapInfo for version tuples.
template <> struct DenseMapInfo<VersionTuple> {
  /// Compute a DenseMap hash for \p Value.
  ///
  /// \param Value Version tuple to hash.
  /// \returns Hash code for the version tuple.
  static unsigned getHashValue(const VersionTuple &Value) {
    return hash_value(Value);
  }

  /// Return true if \p LHS and \p RHS are equal.
  ///
  /// \param LHS Left-hand version.
  /// \param RHS Right-hand version.
  /// \returns \c true if the versions are equal.
  static bool isEqual(const VersionTuple &LHS, const VersionTuple &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm
#endif // LLVM_SUPPORT_VERSIONTUPLE_H
