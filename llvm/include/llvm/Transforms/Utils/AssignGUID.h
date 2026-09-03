//===-- AssignGUID.h - Unique identifier assignment pass --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a pass which assigns a a GUID (globally unique identifier)
// to every GlobalValue in the module, according to its current name, linkage,
// and originating file. This way we have a consistent identifier even when
// these inputs to the GUID change (for instance, after externalising a global
// in ThinLTO).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_ASSIGNGUID_H
#define LLVM_TRANSFORMS_UTILS_ASSIGNGUID_H

#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Debug.h"

namespace llvm {

class AssignGUIDPass : public RequiredPassInfoMixin<AssignGUIDPass> {
public:
  /// Construct an AssignGUID pass.
  AssignGUIDPass() = default;

  /// Assign a GUID to every GlobalValue in the module.
  /// @param M Module whose GlobalValues receive GUIDs.
  LLVM_ABI static void runOnModule(Module &M);

  /// Run the AssignGUID pass over the module.
  /// @param M Module whose GlobalValues receive GUIDs.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    AssignGUIDPass::runOnModule(M);
    return PreservedAnalyses::all();
  }

  /// Assign a GUID to a GlobalVariable produced by GlobalMerge.
  ///
  /// Let GlobalMerge assign a GUID for merged GVs, instead of needing to
  /// traverse all the module; or instead of making GlobalValue::assignGUID
  /// public.
  /// @param GV Merged global variable that should receive a GUID.
  LLVM_ABI static void assignGUIDForMergedGV(GlobalVariable &GV);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_ASSIGNGUID_H