//===- llvm/Support/UniqueBBID.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a structure that uniquely identifies a basic block within
// a function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_UNIQUEBBID_H
#define LLVM_SUPPORT_UNIQUEBBID_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {

/// Unique identifier for a basic block within a function.
///
/// This structure represents the information for a basic block pertaining to
/// the basic block sections profile.
struct UniqueBBID {
  /// Base block ID before cloning.
  unsigned BaseID;
  /// Clone instance ID for the block.
  unsigned CloneID;
};

/// Identifies a call site within a basic block for prefetch emission.
///
/// The prefetch symbol is emitted immediately after the call of the given
/// index, in block `BBID` (First call has an index of 1). Zero callsite index
/// means the start of the block.
struct CallsiteID {
  /// Basic block that contains the call site.
  UniqueBBID BBID;
  /// One-based index of the call in the block, or zero for the block start.
  unsigned CallsiteIndex;
};

/// Prefetch hint targeting a callsite in a named function.
///
/// This represents a prefetch hint to be injected at site `SiteID`, targeting
/// `TargetID` in function `TargetFunction`.
struct PrefetchHint {
  /// Call site where the prefetch hint is injected.
  CallsiteID SiteID;
  /// Name of the function containing the prefetch target.
  StringRef TargetFunction;
  /// Call site that is the target of the prefetch.
  CallsiteID TargetID;
};

/// DenseMapInfo specialization for UniqueBBID keys.
template <> struct DenseMapInfo<UniqueBBID> {
  /// Compute a DenseMap hash for \p Val.
  ///
  /// \param Val Unique basic-block ID to hash.
  /// \return Combined hash of the base and clone IDs.
  static unsigned getHashValue(const UniqueBBID &Val) {
    return DenseMapInfo<unsigned>::getHashValue(Val.BaseID) ^
           DenseMapInfo<unsigned>::getHashValue(Val.CloneID);
  }

  /// Return true if \p LHS and \p RHS are equal.
  ///
  /// \param LHS Left-hand unique basic-block ID.
  /// \param RHS Right-hand unique basic-block ID.
  /// \return True if \p LHS and \p RHS have the same base and clone IDs.
  static bool isEqual(const UniqueBBID &LHS, const UniqueBBID &RHS) {
    return DenseMapInfo<unsigned>::isEqual(LHS.BaseID, RHS.BaseID) &&
           DenseMapInfo<unsigned>::isEqual(LHS.CloneID, RHS.CloneID);
  }
};

} // end namespace llvm

#endif // LLVM_SUPPORT_UNIQUEBBID_H
