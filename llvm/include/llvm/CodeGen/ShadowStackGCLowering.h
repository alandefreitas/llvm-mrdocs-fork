//===- llvm/CodeGen/ShadowStackGCLowering.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SHADOWSTACKGCLOWERING_H
#define LLVM_CODEGEN_SHADOWSTACKGCLOWERING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// New PM pass that implements the custom lowering used by the shadow stack GC.
///
/// Only runs on functions which opt in to the shadow stack collector.
class ShadowStackGCLoweringPass
    : public RequiredPassInfoMixin<ShadowStackGCLoweringPass> {
public:
  /// Lower shadow-stack GC roots in module \p M.
  /// \param M Module whose shadow-stack GC functions are lowered.
  /// \param MAM Module analysis manager providing required analyses.
  /// \return The set of analyses preserved after lowering shadow-stack GC roots.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_SHADOWSTACKGCLOWERING_H
