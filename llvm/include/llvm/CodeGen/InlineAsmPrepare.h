//===-- InlineAsmPrepare - Prepare inline asm for code gen ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_INLINEASMPREPARE_H
#define LLVM_CODEGEN_INLINEASMPREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// New PM pass that prepares inline asm for SelectionDAG codegen.
///
/// Lowers inline asm calls (notably callbr) so SelectionDAG can insert
/// register copies along edges to indirect targets.
class InlineAsmPreparePass
    : public RequiredPassInfoMixin<InlineAsmPreparePass> {
public:
  /// Prepare inline asm in \p F for SelectionDAG code generation.
  /// \param F Function whose inline asm is prepared.
  /// \param FAM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_INLINEASMPREPARE_H
