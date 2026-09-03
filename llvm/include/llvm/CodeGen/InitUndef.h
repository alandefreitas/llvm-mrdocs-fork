//===- llvm/CodeGen/InitUndef.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_INITUNDEF_H
#define LLVM_CODEGEN_INITUNDEF_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that initializes undef values with temporary pseudos.
///
/// Gives undef early-clobber operands a pseudo definition so register
/// allocation cannot assign an overlapping register, and rewrites NoReg tied
/// operands back to IMPLICIT_DEF before TwoAddressInstruction.
class InitUndefPass : public RequiredPassInfoMixin<InitUndefPass> {
public:
  /// Initialize undef values and fix NoReg tied operands in \p MF.
  /// \param MF Machine function whose undef operands are initialized.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after initializing undef values.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_INITUNDEF_H
