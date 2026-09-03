//===- LibCallsShrinkWrap.h - Shrink Wrap Library Calls -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LIBCALLSSHRINKWRAP_H
#define LLVM_TRANSFORMS_UTILS_LIBCALLSSHRINKWRAP_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that shrink-wraps unused library calls that may set errno.
///
/// Guards calls whose results are unused but that can set errno on error,
/// so the call runs only on the rare error path and is otherwise skipped.
class LibCallsShrinkWrapPass
    : public OptionalPassInfoMixin<LibCallsShrinkWrapPass> {
public:
  /// Returns the name of this pass.
  /// @return The pass name string.
  static StringRef name() { return "LibCallsShrinkWrapPass"; }

  /// Run the libcalls-shrink-wrap pass over the function.
  /// @param F Function whose unused libcalls may be shrink-wrapped.
  /// @param FAM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LIBCALLSSHRINKWRAP_H
