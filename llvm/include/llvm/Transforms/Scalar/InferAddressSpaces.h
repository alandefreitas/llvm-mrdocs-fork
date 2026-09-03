//===- InferAddressSpace.h - ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_INFERADDRESSSPACES_H
#define LLVM_TRANSFORMS_SCALAR_INFERADDRESSSPACES_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that infers specific address spaces for generic pointers.
///
/// Propagates non-generic address spaces from type-qualified variables through
/// pointer expressions so memory operations can use the more specific space.
struct InferAddressSpacesPass : OptionalPassInfoMixin<InferAddressSpacesPass> {
  /// Construct an InferAddressSpaces pass with an uninitialized flat address
  /// space.
  LLVM_ABI InferAddressSpacesPass();
  /// Construct an InferAddressSpaces pass with a flat address space.
  /// @param AddressSpace Flat (generic) address space to use for inference.
  LLVM_ABI InferAddressSpacesPass(unsigned AddressSpace);
  /// Run address-space inference over the function.
  /// @param F Function whose generic pointers may be refined.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

private:
  unsigned FlatAddrSpace = 0;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_INFERADDRESSSPACES_H
