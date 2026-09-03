//===- LoadStoreVectorizer.cpp - GPU Load & Store Vectorizer --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_LOADSTOREVECTORIZER_H
#define LLVM_TRANSFORMS_VECTORIZE_LOADSTOREVECTORIZER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Pass;
class Function;

/// Merges consecutive loads and stores into vector loads and stores.
class LoadStoreVectorizerPass
    : public OptionalPassInfoMixin<LoadStoreVectorizerPass> {
public:
  /// Run the load/store vectorizer pass over the function.
  /// @param F Function whose consecutive loads and stores may be vectorized.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Create a legacy pass manager instance of the LoadStoreVectorizer pass.
/// @return A new LoadStoreVectorizer pass for the legacy pass manager.
LLVM_ABI Pass *createLoadStoreVectorizerPass();
}

#endif /* LLVM_TRANSFORMS_VECTORIZE_LOADSTOREVECTORIZER_H */
