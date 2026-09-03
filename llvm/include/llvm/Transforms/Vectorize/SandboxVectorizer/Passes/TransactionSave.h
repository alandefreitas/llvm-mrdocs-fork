//===- TransactionSave.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a region pass that simply calls Context::save() to save the IR state.
//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_TRANSACTIONSAVE_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_TRANSACTIONSAVE_H

#include "llvm/SandboxIR/Pass.h"
#include "llvm/SandboxIR/Region.h"

namespace llvm::sandboxir {

/// A Region pass that saves the Sandbox IR state via Context::save().
///
/// Simply calls Context::save() to save the IR state for the region.
class LLVM_ABI TransactionSave : public RegionPass {
public:
  /// Construct a TransactionSave pass.
  /// \param AuxArg Unused; must be empty.
  TransactionSave(StringRef AuxArg) : RegionPass("tr-save") {
    assert(AuxArg.empty() && "This pass ignores aux arg!");
  }
  /// Save the IR state for the given region.
  /// \param Rgn The region whose context state is saved.
  /// \param A Analyses available to the pass.
  /// \returns False; this pass never modifies the IR.
  bool runOnRegion(Region &Rgn, const Analyses &A) final;
};

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_TRANSACTIONSAVE_H
