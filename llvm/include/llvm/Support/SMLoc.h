//===- SMLoc.h - Source location for use with diagnostics -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SMLoc class.  This class encapsulates a location in
// source code for use in diagnostics.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SMLOC_H
#define LLVM_SUPPORT_SMLOC_H

#include <cassert>

namespace llvm {

/// Represents a location in source code.
class SMLoc {
  const char *Ptr = nullptr;

public:
  /// Construct an invalid source location.
  constexpr SMLoc() = default;

  /// Return true if this location refers to a valid pointer.
  ///
  /// \return True if this location refers to a valid pointer.
  constexpr bool isValid() const { return Ptr != nullptr; }

  /// Return true if this location and \p RHS refer to the same pointer.
  ///
  /// \param RHS Other source location to compare against.
  /// \return True if both locations refer to the same pointer.
  constexpr bool operator==(SMLoc RHS) const { return RHS.Ptr == Ptr; }
  /// Return true if this location and \p RHS refer to different pointers.
  ///
  /// \param RHS Other source location to compare against.
  /// \return True if the locations refer to different pointers.
  constexpr bool operator!=(SMLoc RHS) const { return RHS.Ptr != Ptr; }

  /// Return the underlying pointer for this source location.
  ///
  /// \return The underlying pointer, or null if the location is invalid.
  constexpr const char *getPointer() const { return Ptr; }

  /// Construct a source location that refers to \p Ptr.
  ///
  /// \param Ptr Pointer into source text; may be null for an invalid location.
  /// \return An SMLoc referring to \p Ptr.
  static SMLoc getFromPointer(const char *Ptr) {
    SMLoc L;
    L.Ptr = Ptr;
    return L;
  }
};

/// Represents a range in source code.
///
/// SMRange is implemented using a half-open range, as is the convention in C++.
/// In the string "abc", the range [1,3) represents the substring "bc", and the
/// range [2,2) represents an empty range between the characters "b" and "c".
class SMRange {
public:
  SMLoc Start; ///< Inclusive start of the half-open source range.
  SMLoc End;   ///< Exclusive end of the half-open source range.

  /// Construct an invalid empty source range.
  SMRange() = default;
  /// Construct a half-open source range from \p St to \p En.
  ///
  /// Both endpoints must be valid, or both must be invalid.
  ///
  /// \param St Inclusive start of the range.
  /// \param En Exclusive end of the range.
  SMRange(SMLoc St, SMLoc En) : Start(St), End(En) {
    assert(Start.isValid() == End.isValid() &&
           "Start and End should either both be valid or both be invalid!");
  }

  /// Return true if this range has a valid start (and thus a valid end).
  ///
  /// \return True if the start (and thus the end) is valid.
  bool isValid() const { return Start.isValid(); }
};

} // end namespace llvm

#endif // LLVM_SUPPORT_SMLOC_H
