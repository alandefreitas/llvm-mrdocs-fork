//===- llvm/CodeGen/RegisterCoalescerPass.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTER_COALESCERPASS_H
#define LLVM_CODEGEN_REGISTER_COALESCERPASS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {
/// New PM pass that coalesces copy-related virtual registers.
///
/// Joins live intervals across copies and related moves to eliminate
/// redundant register transfers before register allocation.
class RegisterCoalescerPass
    : public RequiredPassInfoMixin<RegisterCoalescerPass> {
public:
  /// Coalesce register copies in \p MF.
  /// \param MF Machine function whose copies are coalesced.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after register coalescing.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass clears on the machine function.
  ///
  /// Coalescing may leave the function no longer in SSA form.
  /// \return Cleared properties, with IsSSA set.
  MachineFunctionProperties getClearedProperties() const {
    return MachineFunctionProperties().setIsSSA();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_REGISTER_COALESCERPASS_H
