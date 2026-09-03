//===- CoroAnnotationElide.h - Elide attributed safe coroutine calls ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This pass transforms all Call or Invoke instructions that are annotated
// "coro_elide_safe" to call the `.noalloc` variant of coroutine instead.
// The frame of the callee coroutine is allocated inside the caller. A pointer
// to the allocated frame will be passed into the `.noalloc` ramp function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_COROANNOTATIONELIDE_H
#define LLVM_TRANSFORMS_COROUTINES_COROANNOTATIONELIDE_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that elides heap allocation for coroutine calls marked coro_elide_safe.
///
/// Call and Invoke instructions annotated \c coro_elide_safe are rewritten to
/// call the \c .noalloc variant of the coroutine. The callee's frame is
/// allocated in the caller and a pointer to it is passed into the \c .noalloc
/// ramp function.
struct CoroAnnotationElidePass
    : OptionalPassInfoMixin<CoroAnnotationElidePass> {
  /// Construct a coroutine annotation elide pass.
  CoroAnnotationElidePass() = default;

  /// Elide annotated coroutine calls in SCC \p C.
  ///
  /// \param C The SCC whose calls are considered for elision.
  /// \param AM The CGSCC analysis manager.
  /// \param CG The lazy call graph.
  /// \param UR The CGSCC update result.
  /// \return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(LazyCallGraph::SCC &C,
                                 CGSCCAnalysisManager &AM, LazyCallGraph &CG,
                                 CGSCCUpdateResult &UR);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_COROUTINES_COROANNOTATIONELIDE_H
