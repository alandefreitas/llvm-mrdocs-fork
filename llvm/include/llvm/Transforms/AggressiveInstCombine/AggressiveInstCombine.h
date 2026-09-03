//===- AggressiveInstCombine.h - AggressiveInstCombine pass -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// AggressiveInstCombiner - Combine expression patterns to form expressions
/// with fewer, simple instructions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_AGGRESSIVEINSTCOMBINE_AGGRESSIVEINSTCOMBINE_H
#define LLVM_TRANSFORMS_AGGRESSIVEINSTCOMBINE_AGGRESSIVEINSTCOMBINE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// A pass that combines expression patterns into fewer, simpler instructions.
///
/// This pass performs more expensive pattern matching than InstCombine, folding
/// expression patterns into forms with fewer, simpler instructions.
class AggressiveInstCombinePass
    : public OptionalPassInfoMixin<AggressiveInstCombinePass> {
public:
  /// Run aggressive instruction combining over the function.
  /// @param F Function to combine instructions in.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif
