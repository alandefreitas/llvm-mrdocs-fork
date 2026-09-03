//===- PreISelIntrinsicLowering.h - Pre-ISel intrinsic lowering pass ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements IR lowering for the llvm.load.relative and llvm.objc.*
// intrinsics.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_PREISELINTRINSICLOWERING_H
#define LLVM_CODEGEN_PREISELINTRINSICLOWERING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;
class TargetMachine;

/// Pass that lowers selected intrinsics before instruction selection.
///
/// Implements IR lowering for the \c llvm.load.relative and \c llvm.objc.*
/// intrinsics.
struct PreISelIntrinsicLoweringPass
    : RequiredPassInfoMixin<PreISelIntrinsicLoweringPass> {
  /// Target machine used when deciding how to lower intrinsics.
  const TargetMachine *TM;

  /// Construct a pre-ISel intrinsic lowering pass for target machine \p TM.
  /// \param TM Target machine used when lowering intrinsics.
  PreISelIntrinsicLoweringPass(const TargetMachine *TM) : TM(TM) {}
  /// Run pre-ISel intrinsic lowering on module \p M.
  /// \param M Module whose intrinsics are lowered.
  /// \param AM Module analysis manager providing required analyses.
  /// \return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_PREISELINTRINSICLOWERING_H
