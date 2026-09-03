//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This removes debug info from everything. It can be used to ensure tests can
// be debugified without affecting the output MIR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINESTRIPDEBUG_H
#define LLVM_CODEGEN_MACHINESTRIPDEBUG_H

#include "llvm/IR/Analysis.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// New PM pass that strips debug info from Machine IR in a module.
///
/// Removes debug info from everything so tests can be debugified without
/// affecting the output MIR.
class StripDebugMachineModulePass
    : public RequiredPassInfoMixin<StripDebugMachineModulePass> {
public:
  /// Strip debug info from Machine IR in module \p M.
  ///
  /// \param M Module whose MachineFunctions have debug info removed.
  /// \param AM Module analysis manager providing MachineFunction analyses.
  /// \return All analyses if nothing changed; otherwise analyses compatible
  ///         with stripping Machine IR debug info.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINESTRIPDEBUG_H
