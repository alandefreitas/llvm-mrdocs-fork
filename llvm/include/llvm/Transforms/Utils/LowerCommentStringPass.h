//===-- LowerCommentStringPass.h - Lower Comment string metadata        --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOWERCOMMENTSTRINGPASS_H
#define LLVM_TRANSFORMS_UTILS_LOWERCOMMENTSTRINGPASS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that lowers loadtime comment string metadata for supported targets.
///
/// Processes copyright comment strings created by Clang for
/// `#pragma comment(copyright, ...)`. Attaches `!implicit.ref` metadata from
/// every defined function to each `!loadtime_comment` global so backends such
/// as PowerPC AIX can keep the strings alive via `.ref` relocations. Currently
/// enabled for AIX targets only.
class LowerCommentStringPass
    : public RequiredPassInfoMixin<LowerCommentStringPass> {
public:
  /// Run the lower-comment-string pass over the module.
  /// @param M Module whose loadtime comment globals are lowered.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOWERCOMMENTSTRINGPASS_H
