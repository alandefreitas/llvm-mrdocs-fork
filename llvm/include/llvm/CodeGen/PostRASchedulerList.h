//===- llvm/CodeGen/PostRASchedulerList.h ------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_POSTRASCHEDULERLIST_H
#define LLVM_CODEGEN_POSTRASCHEDULERLIST_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that performs post-register-allocation instruction scheduling.
class PostRASchedulerPass : public OptionalPassInfoMixin<PostRASchedulerPass> {
  const TargetMachine *TM;

public:
  /// Construct a post-RA scheduler pass for the given target.
  /// \param TM Target machine used to configure scheduling.
  PostRASchedulerPass(const TargetMachine *TM) : TM(TM) {}
  /// Schedule machine instructions in \p MF after register allocation.
  /// \param MF Machine function whose instructions are scheduled.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after post-RA scheduling.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Post-RA scheduling expects that the function contains no virtual
  /// registers.
  /// \return Required properties, with NoVRegs set.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_POSTRASCHEDULERLIST_H
