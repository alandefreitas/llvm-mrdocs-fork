//===- MoveAutoInit.h - Move insts marked as auto-init Pass --*- C++ -*-======//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass moves instructions marked as auto-init closer to their use if
// profitable, generally because it moves them under a guard, potentially
// skipping the overhead of the auto-init under some execution paths.
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_MOVEAUTOINIT_H
#define LLVM_TRANSFORMS_UTILS_MOVEAUTOINIT_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that moves auto-init instructions closer to their uses when profitable.
///
/// Instructions marked as auto-init are sunk toward their uses, generally under
/// a guard, so the overhead of auto-init can be skipped on some execution paths.
class MoveAutoInitPass : public OptionalPassInfoMixin<MoveAutoInitPass> {
public:
  /// Run the move-auto-init pass over the function.
  /// @param F Function whose auto-init instructions may be moved.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_MOVEAUTOINIT_H
