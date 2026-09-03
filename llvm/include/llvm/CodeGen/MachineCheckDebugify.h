//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the CheckDebugMachineModulePass class,
/// used by the new pass manager to check debug info after mir-debugify.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINECHECKDEBUGIFY_H_
#define LLVM_CODEGEN_MACHINECHECKDEBUGIFY_H_

#include "llvm/IR/Analysis.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// New PM pass that checks Machine IR debug info after mir-debugify.
///
/// Verifies the integrity of line info in DILocation and DILocalVariable that
/// mir-debugify previously generated.
class CheckDebugMachineModulePass
    : public RequiredPassInfoMixin<CheckDebugMachineModulePass> {
public:
  /// Check Machine IR debug info for module \p M after mir-debugify.
  ///
  /// \param M Module whose MachineFunctions are checked against
  ///          llvm.mir.debugify metadata.
  /// \param AM Module analysis manager providing MachineFunction analyses.
  /// \return All analyses preserved; this pass does not transform \p M.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINECHECKDEBUGIFY_H_
