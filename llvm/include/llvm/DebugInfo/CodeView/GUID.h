//===- GUID.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_GUID_H
#define LLVM_DEBUGINFO_CODEVIEW_GUID_H

#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <cstring>

namespace llvm {
class raw_ostream;

namespace codeview {

/// This represents the 'GUID' type from windows.h.
struct GUID {
  /// The 16-byte GUID value.
  uint8_t Guid[16];
};

/// Return true if \p LHS and \p RHS have the same GUID bytes.
///
/// \param LHS Left-hand GUID.
/// \param RHS Right-hand GUID.
///
/// \returns True if the GUID bytes are equal.
inline bool operator==(const GUID &LHS, const GUID &RHS) {
  return 0 == ::memcmp(LHS.Guid, RHS.Guid, sizeof(LHS.Guid));
}

/// Return true if \p LHS compares less than \p RHS by byte order.
///
/// \param LHS Left-hand GUID.
/// \param RHS Right-hand GUID.
///
/// \returns True if \p LHS is less than \p RHS.
inline bool operator<(const GUID &LHS, const GUID &RHS) {
  return ::memcmp(LHS.Guid, RHS.Guid, sizeof(LHS.Guid)) < 0;
}

/// Return true if \p LHS compares less than or equal to \p RHS by byte order.
///
/// \param LHS Left-hand GUID.
/// \param RHS Right-hand GUID.
///
/// \returns True if \p LHS is less than or equal to \p RHS.
inline bool operator<=(const GUID &LHS, const GUID &RHS) {
  return ::memcmp(LHS.Guid, RHS.Guid, sizeof(LHS.Guid)) <= 0;
}

/// Return true if \p LHS compares greater than \p RHS by byte order.
///
/// \param LHS Left-hand GUID.
/// \param RHS Right-hand GUID.
///
/// \returns True if \p LHS is greater than \p RHS.
inline bool operator>(const GUID &LHS, const GUID &RHS) {
  return !(LHS <= RHS);
}

/// Return true if \p LHS compares greater than or equal to \p RHS by byte order.
///
/// \param LHS Left-hand GUID.
/// \param RHS Right-hand GUID.
///
/// \returns True if \p LHS is greater than or equal to \p RHS.
inline bool operator>=(const GUID &LHS, const GUID &RHS) {
  return !(LHS < RHS);
}

/// Return true if \p LHS and \p RHS have different GUID bytes.
///
/// \param LHS Left-hand GUID.
/// \param RHS Right-hand GUID.
///
/// \returns True if the GUID bytes differ.
inline bool operator!=(const GUID &LHS, const GUID &RHS) {
  return !(LHS == RHS);
}

/// Stream a human-readable representation of \p Guid to \p OS.
///
/// \param OS Destination stream.
/// \param Guid GUID to print.
///
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const GUID &Guid);

} // namespace codeview
} // namespace llvm

#endif
