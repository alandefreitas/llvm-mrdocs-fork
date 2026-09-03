//===- Reg2Mem.h - Convert registers to allocas -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the RegToMem Pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_REG2MEM_H
#define LLVM_TRANSFORMS_SCALAR_REG2MEM_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass that demotes registers to memory allocas.
///
/// Converts virtual registers and PHI nodes into loads and stores of allocas
/// so that only allocas and loads before PHI nodes are live across basic
/// blocks, making CFG transformations easier.
class RegToMemPass : public OptionalPassInfoMixin<RegToMemPass> {
public:
  /// Run register-to-memory demotion over the function.
  /// @param F Function whose registers may be demoted to allocas.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_REG2MEM_H
