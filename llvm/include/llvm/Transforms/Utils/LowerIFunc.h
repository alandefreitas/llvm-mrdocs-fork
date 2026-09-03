//===-- LowerIFunc.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOWERIFUNC_H
#define LLVM_TRANSFORMS_UTILS_LOWERIFUNC_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Pass to replace calls to ifuncs with indirect calls.
///
/// This could be used to support ifunc on systems where the program loader does not natively support it. Constant initializer uses of ifuncs are not handled.
class LowerIFuncPass : public OptionalPassInfoMixin<LowerIFuncPass> {
public:
  /// Construct a LowerIFunc pass.
  LowerIFuncPass() = default;

  /// Run the lower-ifunc pass over the module.
  /// @param M Module whose ifunc calls are lowered.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOWERIFUNC_H
