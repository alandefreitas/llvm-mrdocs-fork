//===- StripGCRelocates.h - -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_STRIPGCRELOCATES_H
#define LLVM_TRANSFORMS_UTILS_STRIPGCRELOCATES_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass that removes gc.relocates inserted by RewriteStatepointsForGC.
///
/// The resulting IR is incorrect, but useful for analyzing IR without
/// gc.relocates. The statepoint and gc.result intrinsics remain.
class StripGCRelocates : public OptionalPassInfoMixin<StripGCRelocates> {
public:
  /// Run the strip-gc-relocates pass over the function.
  /// @param F Function whose gc.relocates are stripped.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_STRIPGCRELOCATES_H
