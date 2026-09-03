//===- CodeGenPrepare.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Defines an IR pass for CodeGen Prepare.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PREPARE_H
#define LLVM_CODEGEN_PREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;
class TargetMachine;

/// New PM pass that prepares IR for instruction selection.
///
/// Transforms the code to expose more pattern matching during instruction
/// selection.
class CodeGenPreparePass : public OptionalPassInfoMixin<CodeGenPreparePass> {
private:
  const TargetMachine *TM;

public:
  /// Construct a pass using target information from \p TM.
  /// \param TM Target machine used to guide CodeGen preparation.
  CodeGenPreparePass(const TargetMachine &TM) : TM(&TM) {}
  /// Prepare \p F for instruction selection on the configured target.
  /// \param F Function whose IR is prepared for CodeGen.
  /// \param AM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_PREPARE_H
