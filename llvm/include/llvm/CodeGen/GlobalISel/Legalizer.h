//== llvm/CodeGen/GlobalISel/Legalizer.h ---------------- -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file A pass to convert the target-illegal operations created by IR -> MIR
/// translation into ones the target expects to be able to select. This may
/// occur in multiple phases, for example G_ADD <2 x i8> -> G_ADD <2 x i16> ->
/// G_ADD <4 x i16>.
///
/// The LegalizeHelper class is where most of the work happens, and is designed
/// to be callable from other passes that find themselves with an illegal
/// instruction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_LEGALIZER_H
#define LLVM_CODEGEN_GLOBALISEL_LEGALIZER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/GlobalISel/GISelValueTracking.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class LegalizerInfo;
class MachineIRBuilder;
class MachineInstr;
class GISelChangeObserver;
class LibcallLoweringInfo;
class LostDebugLocObserver;

/// Result of running GlobalISel legalization on a machine function.
struct LegalizerMFResult {
  /// True if the machine function was modified during legalization.
  bool Changed;
  /// Instruction that could not be legalized, or null on success.
  const MachineInstr *FailedOn;
};

/// Legalize generic instructions in \p MF according to \p LI.
///
/// Walks the function, legalizes pre-ISel generic opcodes, and updates
/// observers as instructions are created, changed, or erased.
///
/// \param MF Machine function whose instructions are legalized.
/// \param LI Target legalization rules to apply.
/// \param AuxObservers Additional change observers (for example CSE).
/// \param LocObserver Observer that tracks lost debug locations.
/// \param MIRBuilder Builder used to create replacement instructions.
/// \param Libcalls Optional libcall lowering info; may be null.
/// \param VT Optional value-tracking analysis; may be null.
/// \return Whether the function changed and, on failure, the illegal instr.
LegalizerMFResult legalizeMachineFunction(
    MachineFunction &MF, const LegalizerInfo &LI,
    ArrayRef<GISelChangeObserver *> AuxObservers,
    LostDebugLocObserver &LocObserver, MachineIRBuilder &MIRBuilder,
    const LibcallLoweringInfo *Libcalls, GISelValueTracking *VT);

/// Legacy pass that legalizes generic machine instructions for GlobalISel.
///
/// Converts target-illegal operations into forms the target can select, using
/// the shared legalizeMachineFunction implementation.
class LLVM_ABI LegalizerLegacy : public MachineFunctionPass {
public:
  /// Pass identification, replacement for typeid.
  static char ID;

public:
  /// Construct the legacy GlobalISel legalizer pass.
  LegalizerLegacy();

  /// Return the name of this pass.
  ///
  /// \return A string identifying this pass as "Legalizer".
  StringRef getPassName() const override { return "Legalizer"; }

  /// Declare required and preserved analyses for this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Return the properties this pass requires of the machine function.
  ///
  /// Legalization expects the function to be in SSA form.
  ///
  /// \return Properties requiring the function to be in SSA form.
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setIsSSA();
  }

  /// Return the properties this pass sets on the machine function.
  ///
  /// Marks the function as having completed legalization.
  ///
  /// \return Properties marking the function as legalized.
  MachineFunctionProperties getSetProperties() const override {
    return MachineFunctionProperties().setLegalized();
  }

  /// Return the properties this pass clears on the machine function.
  ///
  /// Legalization may introduce PHIs and virtual registers that later passes
  /// must not assume are absent.
  ///
  /// \return Properties clearing NoPHIs and NoVRegs.
  MachineFunctionProperties getClearedProperties() const override {
    return MachineFunctionProperties().setNoPHIs().setNoVRegs();
  }

  /// Legalize generic instructions in \p MF for GlobalISel.
  ///
  /// \param MF Machine function whose instructions are legalized.
  /// \return True if the machine function was modified.
  bool runOnMachineFunction(MachineFunction &MF) override;
};

/// New PM pass that legalizes generic machine instructions for GlobalISel.
class LegalizerPass : public RequiredPassInfoMixin<LegalizerPass> {
public:
  /// Legalize generic instructions in \p MF for GlobalISel.
  ///
  /// \param MF Machine function whose instructions are legalized.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Legalization expects the function to be in SSA form.
  ///
  /// \return Properties requiring the function to be in SSA form.
  MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties().setIsSSA();
  }

  /// Return the properties this pass sets on the machine function.
  ///
  /// Marks the function as having completed legalization.
  ///
  /// \return Properties marking the function as legalized.
  MachineFunctionProperties getSetProperties() const {
    return MachineFunctionProperties().setLegalized();
  }

  /// Return the properties this pass clears on the machine function.
  ///
  /// Legalization may introduce PHIs and virtual registers that later passes
  /// must not assume are absent.
  ///
  /// \return Properties clearing NoPHIs and NoVRegs.
  MachineFunctionProperties getClearedProperties() const {
    return MachineFunctionProperties().setNoPHIs().setNoVRegs();
  }
};

} // End namespace llvm.

#endif
