//===- llvm/CodeGen/IndirectBrExpand.h -------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_INDIRECTBREXPAND_H
#define LLVM_CODEGEN_INDIRECTBREXPAND_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

/// New PM pass that expands `indirectbr` instructions into switches.
///
/// Enumerates basic blocks in a dense integer range, replaces each
/// `blockaddr` with the corresponding integer, and redirects all indirect
/// branches in the function through a common switch. Useful when a target
/// cannot codegen `indirectbr` natively or prefers switch lowering.
class IndirectBrExpandPass
    : public RequiredPassInfoMixin<IndirectBrExpandPass> {
  const TargetMachine *TM;

public:
  /// Construct a pass using target information from \p TM.
  /// \param TM Target machine used to decide whether expansion is enabled.
  IndirectBrExpandPass(const TargetMachine &TM) : TM(&TM) {}
  /// Expand `indirectbr` instructions in \p F into a switch.
  /// \param F Function whose indirect branches are expanded.
  /// \param FAM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_INDIRECTBREXPAND_H
