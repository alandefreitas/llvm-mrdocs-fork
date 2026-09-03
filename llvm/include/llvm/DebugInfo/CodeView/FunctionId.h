//===- FunctionId.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_FUNCTIONID_H
#define LLVM_DEBUGINFO_CODEVIEW_FUNCTIONID_H

#include <cinttypes>

namespace llvm {
namespace codeview {

/// A 32-bit CodeView function identifier.
class FunctionId {
public:
  /// Construct a FunctionId with index zero.
  FunctionId() : Index(0) {}

  /// Construct a FunctionId from a raw 32-bit index value.
  ///
  /// \param Index Raw function identifier index.
  explicit FunctionId(uint32_t Index) : Index(Index) {}

  /// Return the raw 32-bit function identifier index.
  ///
  /// \returns The raw 32-bit function identifier index.
  uint32_t getIndex() const { return Index; }

private:
  uint32_t Index;
};

/// Return true if \p A and \p B have the same raw index.
///
/// \param A Left-hand FunctionId.
/// \param B Right-hand FunctionId.
/// \returns True if \p A and \p B have the same raw index.
inline bool operator==(const FunctionId &A, const FunctionId &B) {
  return A.getIndex() == B.getIndex();
}

/// Return true if \p A and \p B have different raw indices.
///
/// \param A Left-hand FunctionId.
/// \param B Right-hand FunctionId.
/// \returns True if \p A and \p B have different raw indices.
inline bool operator!=(const FunctionId &A, const FunctionId &B) {
  return A.getIndex() != B.getIndex();
}

/// Return true if \p A has a smaller raw index than \p B.
///
/// \param A Left-hand FunctionId.
/// \param B Right-hand FunctionId.
/// \returns True if \p A has a smaller raw index than \p B.
inline bool operator<(const FunctionId &A, const FunctionId &B) {
  return A.getIndex() < B.getIndex();
}

/// Return true if \p A has a raw index less than or equal to \p B.
///
/// \param A Left-hand FunctionId.
/// \param B Right-hand FunctionId.
/// \returns True if \p A has a raw index less than or equal to \p B.
inline bool operator<=(const FunctionId &A, const FunctionId &B) {
  return A.getIndex() <= B.getIndex();
}

/// Return true if \p A has a greater raw index than \p B.
///
/// \param A Left-hand FunctionId.
/// \param B Right-hand FunctionId.
/// \returns True if \p A has a greater raw index than \p B.
inline bool operator>(const FunctionId &A, const FunctionId &B) {
  return A.getIndex() > B.getIndex();
}

/// Return true if \p A has a raw index greater than or equal to \p B.
///
/// \param A Left-hand FunctionId.
/// \param B Right-hand FunctionId.
/// \returns True if \p A has a raw index greater than or equal to \p B.
inline bool operator>=(const FunctionId &A, const FunctionId &B) {
  return A.getIndex() >= B.getIndex();
}
}
}

#endif
