//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the BreakFalseDepsPass class, used to
/// identify and avoid false dependencies which cause unnecessary stalls.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_BREAKFALSEDEPS_H
#define LLVM_CODEGEN_BREAKFALSEDEPS_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// New PM pass that breaks false register dependencies which cause stalls.
class BreakFalseDepsPass : public OptionalPassInfoMixin<BreakFalseDepsPass> {
public:
  /// Identify and break false dependencies in \p MF.
  /// \param MF Machine function whose false dependencies are broken.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after breaking false dependencies.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Breaking false dependencies expects that the function contains no virtual
  /// registers.
  /// \return Required properties, with NoVRegs set.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_BREAKFALSEDEPS_H
