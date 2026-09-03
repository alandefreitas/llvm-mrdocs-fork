//===- llvm/CodeGen/PostRAMachineSink.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_POSTRAMACHINESINK_H
#define LLVM_CODEGEN_POSTRAMACHINESINK_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that sinks copy instructions after register allocation.
///
/// Moves copies that are unused in their defining block closer to their uses
/// in successor blocks, which can expose shrink-wrapping opportunities.
class PostRAMachineSinkingPass
    : public OptionalPassInfoMixin<PostRAMachineSinkingPass> {
public:
  /// Sink unused copy instructions in \p MF closer to their uses.
  /// \param MF Machine function whose copies are sunk.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after sinking copies.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Post-RA machine sinking expects that the function contains no virtual
  /// registers.
  /// \return Required properties, with NoVRegs set.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setNoVRegs();
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_POSTRAMACHINESINK_H
