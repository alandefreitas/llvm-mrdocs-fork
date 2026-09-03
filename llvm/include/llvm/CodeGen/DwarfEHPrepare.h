//===------------------- llvm/CodeGen/DwarfEHPrepare.h ----------*- C++-*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass mulches exception handling code into a form adapted to code
// generation. Required if using dwarf exception handling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DWARFEHPREPARE_H
#define LLVM_CODEGEN_DWARFEHPREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

/// New PM pass that prepares DWARF exception handling for code generation.
///
/// Mulches exception handling code into a form adapted to code generation.
/// Required if using dwarf exception handling.
class DwarfEHPreparePass : public RequiredPassInfoMixin<DwarfEHPreparePass> {
  const TargetMachine *TM;

public:
  /// Construct a pass using target information from \p TM_.
  /// \param TM_ Target machine used to guide DWARF EH preparation.
  explicit DwarfEHPreparePass(const TargetMachine &TM_) : TM(&TM_) {}
  /// Prepare DWARF exception handling in \p F for code generation.
  /// \param F Function whose exception handling is prepared.
  /// \param FAM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_DWARFEHPREPARE_H
