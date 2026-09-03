//===--- PartiallyInlineLibCalls.h - Partially inline libcalls --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass tries to partially inline the fast path of well-known library
// functions, such as using square-root instructions for cases where sqrt()
// does not need to set errno.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_PARTIALLYINLINELIBCALLS_H
#define LLVM_TRANSFORMS_SCALAR_PARTIALLYINLINELIBCALLS_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Function;

/// Pass that partially inlines the fast path of well-known library functions.
///
/// For example, it may use square-root instructions for cases where sqrt()
/// does not need to set errno.
class PartiallyInlineLibCallsPass
    : public OptionalPassInfoMixin<PartiallyInlineLibCallsPass> {
public:
  /// Run partial library-call inlining over the function.
  /// @param F Function whose library calls may be partially inlined.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_PARTIALLYINLINELIBCALLS_H
