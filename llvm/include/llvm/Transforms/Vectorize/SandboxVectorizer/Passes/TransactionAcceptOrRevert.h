//===- TransactionAcceptOrRevert.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a region pass that checks the region cost before/after vectorization
// and accepts the state of Sandbox IR if the cost is better, or otherwise
// reverts it.
//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_TRANSACTIONACCEPTORREVERT_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_TRANSACTIONACCEPTORREVERT_H

#include "llvm/SandboxIR/Pass.h"
#include "llvm/SandboxIR/Region.h"

namespace llvm::sandboxir {

/// A Region pass that accepts or reverts Sandbox IR based on vectorization cost.
///
/// Checks the region cost before/after vectorization and accepts the state of
/// Sandbox IR if the cost is better, or otherwise reverts it.
class LLVM_ABI TransactionAcceptOrRevert : public RegionPass {
public:
  /// Construct a TransactionAcceptOrRevert pass.
  /// \param AuxArg Unused; must be empty.
  TransactionAcceptOrRevert(StringRef AuxArg)
      : RegionPass("tr-accept-or-revert") {
    assert(AuxArg.empty() && "This pass ignores aux arg!");
  }
  /// Accept or revert the transaction for the given region based on cost.
  /// \param Rgn The region to process.
  /// \param A Analyses available to the pass.
  /// \returns True if the IR was modified.
  bool runOnRegion(Region &Rgn, const Analyses &A) final;
};

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_TRANSACTIONACCEPTORREVERT_H
