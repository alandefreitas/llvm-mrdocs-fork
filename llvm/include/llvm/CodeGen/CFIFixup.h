//===-- CFIFixup.h - Insert CFI remember/restore instructions ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Contains definition of the base CFIFixup pass.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CFIFIXUP_H
#define LLVM_CODEGEN_CFIFIXUP_H

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/InitializePasses.h"

namespace llvm {
/// Legacy MachineFunctionPass that inserts CFI remember/restore instructions.
///
/// Fixes inconsistencies in call-frame information caused by final machine
/// basic block layout by inserting compensating CFI instructions.
class LLVM_ABI CFIFixupLegacy : public MachineFunctionPass {
public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the CFI fixup pass.
  CFIFixupLegacy() : MachineFunctionPass(ID) {}

  /// Declare that this pass preserves all analyses.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  /// Insert compensating CFI instructions in machine function \p MF.
  ///
  /// \param MF Machine function whose call-frame info is fixed up.
  /// \return True if the machine function was modified.
  bool runOnMachineFunction(MachineFunction &MF) override;
};

/// New PM pass that inserts CFI remember/restore instructions.
///
/// Fixes inconsistencies in call-frame information caused by final machine
/// basic block layout by inserting compensating CFI instructions.
class LLVM_ABI CFIFixupPass : public RequiredPassInfoMixin<CFIFixupPass> {
public:
  /// Insert compensating CFI instructions in \p MF.
  ///
  /// \param MF Machine function whose call-frame info is fixed up.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after CFI fixup.
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
};
} // namespace llvm

#endif // LLVM_CODEGEN_CFIFIXUP_H
