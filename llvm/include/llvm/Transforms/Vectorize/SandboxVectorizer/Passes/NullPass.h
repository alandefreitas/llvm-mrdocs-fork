//===- NullPass.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A null region pass that does nothing. Used for testing.
//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_NULLPASS_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_NULLPASS_H

#include "llvm/SandboxIR/Pass.h"

namespace llvm::sandboxir {

class Region;

/// A Region pass that does nothing, for use as a placeholder in tests.
///
/// It can also echo the AuxArg passed to it by the pass builder, which is used
/// for AuxArg testing.
class NullPass final : public RegionPass {
  StringRef AuxArg;

public:
  /// Construct a NullPass that stores \p AuxArg for later retrieval.
  /// \param AuxArg Auxiliary argument from the pass builder; echoed by
  /// getAuxArg() for AuxArg testing.
  NullPass(StringRef AuxArg) : RegionPass("null"), AuxArg(AuxArg) {}
  /// Run on region \p R; always a no-op.
  /// \param R Region to transform.
  /// \param A Analyses available to the pass.
  /// \returns False; this pass never modifies the IR.
  bool runOnRegion(Region &R, const Analyses &A) final { return false; }
  /// Return the AuxArg passed to the constructor.
  /// \returns The auxiliary argument stored at construction.
  StringRef getAuxArg() const { return AuxArg; }
};

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_NULLPASS_H
