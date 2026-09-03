//===-- llvm/CodeGen/SjLjEHPrepare.h -------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SJLJEHPREPARE_H
#define LLVM_CODEGEN_SJLJEHPREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

/// New PM pass that prepares setjmp/longjmp exception handling for code
/// generation.
///
/// Adapts exception handling code to use the GCC-style builtin setjmp/longjmp
/// (sjlj) for EH control flow. Required if using sjlj exception handling.
class SjLjEHPreparePass : public RequiredPassInfoMixin<SjLjEHPreparePass> {
  const TargetMachine *TM;

public:
  /// Construct a pass using target information from \p TM.
  /// \param TM Target machine used to guide sjlj EH preparation.
  explicit SjLjEHPreparePass(const TargetMachine *TM) : TM(TM) {}
  /// Prepare setjmp/longjmp exception handling in \p F for code generation.
  /// \param F Function whose exception handling is prepared.
  /// \param FAM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_SJLJEHPREPARE_H
