//===- TransactionAlwaysRevert.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a region pass that always reverts the transaction without checking
// its cost. This is mainly used as a final pass in lit tests.
//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_TRANSACTIONALWAYSREVERT_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_TRANSACTIONALWAYSREVERT_H

#include "llvm/SandboxIR/Pass.h"
#include "llvm/SandboxIR/Region.h"

namespace llvm::sandboxir {

/// A Region pass that always reverts the transaction without checking its cost.
///
/// This is mainly used as a final pass in lit tests.
class TransactionAlwaysRevert : public RegionPass {
public:
  /// Construct a TransactionAlwaysRevert pass.
  /// \param AuxArg Unused; must be empty.
  TransactionAlwaysRevert(StringRef AuxArg) : RegionPass("tr-revert") {
    assert(AuxArg.empty() && "This pass ignores aux arg!");
  }
  /// Revert the transaction for the given region.
  /// \param Rgn The region to process.
  /// \param A Analyses available to the pass.
  /// \returns True if the tracker had changes before the revert.
  bool runOnRegion(Region &Rgn, const Analyses &A) final {
    auto &Tracker = Rgn.getContext().getTracker();
    bool HasChanges = !Tracker.empty();
    Tracker.revert();
    return HasChanges;
  }
};

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_TRANSACTIONALWAYSREVERT_H
