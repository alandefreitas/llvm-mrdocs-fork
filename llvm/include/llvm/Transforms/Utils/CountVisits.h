//===- CountVisits.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_COUNT_VISITS_H
#define LLVM_TRANSFORMS_UTILS_COUNT_VISITS_H

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass that counts how many times each function is visited.
struct CountVisitsPass : OptionalPassInfoMixin<CountVisitsPass> {
  /// Run the count-visits pass over the function.
  /// @param F Function whose visit count should be updated.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

private:
  StringMap<uint32_t> Counts;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_COUNT_VISITS_H
