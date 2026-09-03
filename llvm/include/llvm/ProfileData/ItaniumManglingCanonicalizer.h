//===--- ItaniumManglingCanonicalizer.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a class for computing equivalence classes of mangled names
// given a set of equivalences between name fragments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_ITANIUMMANGLINGCANONICALIZER_H
#define LLVM_PROFILEDATA_ITANIUMMANGLINGCANONICALIZER_H

#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

class StringRef;

/// Canonicalizer for mangled names.
///
/// This class allows specifying a list of "equivalent" manglings. For example,
/// you can specify that Ss is equivalent to
///   NSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEE
/// and then manglings that refer to libstdc++'s 'std::string' will be
/// considered equivalent to manglings that are the same except that they refer
/// to libc++'s 'std::string'.
///
/// This can be used when data (eg, profiling data) is available for a version
/// of a program built in a different configuration, with correspondingly
/// different manglings.
class ItaniumManglingCanonicalizer {
public:
  /// Construct an empty mangling canonicalizer.
  LLVM_ABI ItaniumManglingCanonicalizer();
  /// Deleted copy constructor.
  /// @param Other Unused; copy construction is deleted.
  ItaniumManglingCanonicalizer(const ItaniumManglingCanonicalizer &Other) = delete;
  /// Deleted copy assignment.
  /// @param Other Unused; copy assignment is deleted.
  void operator=(const ItaniumManglingCanonicalizer &Other) = delete;
  /// Destroy the canonicalizer.
  LLVM_ABI ~ItaniumManglingCanonicalizer();

  /// Result of attempting to add a mangling equivalence.
  enum class EquivalenceError {
    /// The equivalence was added successfully.
    Success,

    /// Both the equivalent manglings have already been used as components of
    /// some other mangling we've looked at. It's too late to add this
    /// equivalence.
    ManglingAlreadyUsed,

    /// The first equivalent mangling is invalid.
    InvalidFirstMangling,

    /// The second equivalent mangling is invalid.
    InvalidSecondMangling,
  };

  /// Kind of Itanium mangling fragment used in an equivalence.
  enum class FragmentKind {
    /// The mangling fragment is a <name> (or a predefined <substitution>).
    Name,
    /// The mangling fragment is a <type>.
    Type,
    /// The mangling fragment is an <encoding>.
    Encoding,
  };

  /// Add an equivalence between \p First and \p Second. Both manglings must
  /// live at least as long as the canonicalizer.
  /// \param Kind Kind of mangling fragment being equated.
  /// \param First First equivalent mangling fragment.
  /// \param Second Second equivalent mangling fragment.
  /// @return Success, or an error describing why the equivalence could not be
  /// added.
  LLVM_ABI EquivalenceError addEquivalence(FragmentKind Kind, StringRef First,
                                           StringRef Second);

  /// Opaque key identifying an equivalence class of mangled names.
  using Key = uintptr_t;

  /// Form a canonical key for a mangling.
  ///
  /// The key will be the same for all equivalent manglings, and different for
  /// any two non-equivalent manglings, but is otherwise unspecified.
  ///
  /// Returns Key() if (and only if) the mangling is not a valid Itanium C++
  /// ABI mangling.
  ///
  /// The string denoted by Mangling must live as long as the canonicalizer.
  /// \param Mangling Mangled name to canonicalize.
  /// @return A canonical key for \p Mangling, or Key() if the mangling is
  /// invalid.
  LLVM_ABI Key canonicalize(StringRef Mangling);

  /// Find a canonical key for the specified mangling, if one has already been
  /// formed. Otherwise returns Key().
  /// \param Mangling Mangled name to look up.
  /// @return The previously formed canonical key, or Key() if none exists.
  LLVM_ABI Key lookup(StringRef Mangling);

private:
  struct Impl;
  Impl *P;
};
} // namespace llvm

#endif // LLVM_PROFILEDATA_ITANIUMMANGLINGCANONICALIZER_H
