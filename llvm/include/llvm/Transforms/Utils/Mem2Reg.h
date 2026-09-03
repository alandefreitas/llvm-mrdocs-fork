//===- Mem2Reg.h - The -mem2reg pass, a wrapper around the Utils lib ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass is a simple pass wrapper around the PromoteMemToReg function call
// exposed by the Utils library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_MEM2REG_H
#define LLVM_TRANSFORMS_UTILS_MEM2REG_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Function;

/// Pass that promotes memory allocas to SSA registers via PromoteMemToReg.
///
/// This is a simple pass wrapper around the PromoteMemToReg function call
/// exposed by the Utils library.
class PromotePass : public OptionalPassInfoMixin<PromotePass> {
public:
  /// Run the mem2reg pass over the function.
  /// @param F Function whose allocas should be promoted to SSA registers.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_MEM2REG_H
