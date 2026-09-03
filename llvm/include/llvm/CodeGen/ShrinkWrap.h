//===- llvm/CodeGen/ShrinkWrap.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SHRINKWRAP_H
#define LLVM_CODEGEN_SHRINKWRAP_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that finds better places for prologue/epilogue save and restore.
///
/// Looks for the best place to insert save and restore instructions so that
/// callee-saved registers are only preserved on paths that need them.
class ShrinkWrapPass : public OptionalPassInfoMixin<ShrinkWrapPass> {
public:
  /// Shrink-wrap save and restore points in \p MF.
  /// \param MF Machine function whose prologue/epilogue points are adjusted.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after shrink wrapping.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Shrink wrapping expects that the function contains no virtual registers.
  /// \return Required properties, with NoVRegs set.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_SHRINKWRAP_H
