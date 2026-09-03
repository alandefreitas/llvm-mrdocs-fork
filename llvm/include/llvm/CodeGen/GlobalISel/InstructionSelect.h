//== llvm/CodeGen/GlobalISel/InstructionSelect.h -----------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file This file describes the interface of the MachineFunctionPass
/// responsible for selecting (possibly generic) machine instructions to
/// target-specific instructions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_INSTRUCTIONSELECT_H
#define LLVM_CODEGEN_GLOBALISEL_INSTRUCTIONSELECT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class InstructionSelector;
class GISelValueTracking;
class BlockFrequencyInfo;
class ProfileSummaryInfo;

/// Legacy pass that selects generic machine instructions to target instructions.
///
/// This pass is responsible for selecting generic machine instructions to
/// target-specific instructions. It relies on the InstructionSelector provided
/// by the target. Selection is done by examining blocks in post-order, and
/// instructions in reverse order.
///
/// \post for all inst in MF: not isPreISelGenericOpcode(inst.opcode)
class LLVM_ABI InstructionSelectLegacy : public MachineFunctionPass {
public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Return the name of this pass.
  ///
  /// \return The name of this pass.
  StringRef getPassName() const override { return "InstructionSelect"; }

  /// Declare required analyses and that this pass preserves value tracking.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Return the properties this pass requires of the machine function.
  ///
  /// Selection expects SSA form and a legalized function. When
  /// RequireRegBankSelection is true, register banks must also be assigned.
  ///
  /// \return The required machine function properties.
  MachineFunctionProperties getRequiredProperties() const override {
    MachineFunctionProperties RequiredProperties;
    RequiredProperties.setIsSSA().setLegalized();
    if (RequireRegBankSelection)
      RequiredProperties.setRegBankSelected();
    return RequiredProperties;
  }

  /// Return the properties this pass sets on the machine function.
  ///
  /// Marks the function as having completed instruction selection.
  ///
  /// \return The machine function properties set by this pass.
  MachineFunctionProperties getSetProperties() const override {
    return MachineFunctionProperties().setSelected();
  }

  /// Construct the legacy GlobalISel instruction selection pass.
  ///
  /// \param OL Optimization level used when requesting profile-driven analyses.
  /// \param RequireRegBankSelection When true, require register bank selection.
  /// \param PassID Pass identification character; defaults to ID.
  InstructionSelectLegacy(CodeGenOptLevel OL = CodeGenOptLevel::Default,
                          bool RequireRegBankSelection = true,
                          char &PassID = ID);

  /// Select generic instructions in \p MF to target-specific instructions.
  ///
  /// \param MF Machine function whose instructions are selected.
  /// \return True if the machine function was modified.
  bool runOnMachineFunction(MachineFunction &MF) override;

protected:
  /// Optimization level used when requesting profile-driven analyses.
  CodeGenOptLevel OptLevel = CodeGenOptLevel::None;
  /// When true, require that register banks have already been selected.
  bool RequireRegBankSelection = true;
};

/// Shared implementation for legacy and new-PM instruction selection passes.
class InstructionSelectImpl {
public:
  /// Select all generic instructions in \p MF using the current selector.
  ///
  /// \param MF Machine function whose instructions are selected.
  /// \return True if the machine function was modified.
  bool selectMachineFunction(MachineFunction &MF);
  /// Install the instruction selector used for subsequent selection.
  ///
  /// \param NewISel Target instruction selector to use; must not be null when
  /// selecting.
  void setInstructionSelector(InstructionSelector *NewISel) { ISel = NewISel; }
  /// Prepare analyses and select generic instructions in \p MF.
  ///
  /// Fetches the target InstructionSelector, optionally loads value-tracking
  /// and profile analyses based on OptLevel, then runs selectMachineFunction.
  ///
  /// \param MF Machine function whose instructions are selected.
  /// \param GetVT Callback that returns the value-tracking analysis.
  /// \param GetPSI Callback that returns profile summary info.
  /// \param GetBFI Callback that returns block frequency info.
  /// \return True if the machine function was modified.
  bool runOnMachineFunction(MachineFunction &MF,
                            function_ref<GISelValueTracking *()> GetVT,
                            function_ref<ProfileSummaryInfo *()> GetPSI,
                            function_ref<BlockFrequencyInfo *()> GetBFI);
  /// Construct the shared instruction selection implementation.
  ///
  /// \param OL Optimization level controlling optional profile analyses.
  InstructionSelectImpl(CodeGenOptLevel OL);

protected:
  /// Observer that keeps the reverse selection iterator valid across erasures.
  class MIIteratorMaintainer;

  /// Target instruction selector used to lower generic opcodes.
  InstructionSelector *ISel = nullptr;
  /// Optional value-tracking analysis passed to the selector.
  GISelValueTracking *VT = nullptr;
  /// Optional block frequency info used at non-None optimization levels.
  BlockFrequencyInfo *BFI = nullptr;
  /// Optional profile summary info used at non-None optimization levels.
  ProfileSummaryInfo *PSI = nullptr;

  /// Optimization level controlling optional profile analyses.
  CodeGenOptLevel OptLevel = CodeGenOptLevel::None;

  /// Select a single machine instruction, or erase it if already dead.
  ///
  /// \param MI Instruction to select or eliminate.
  /// \return True on success, false if selection failed.
  bool selectInstr(MachineInstr &MI);
};

/// New PM pass that selects generic machine instructions to target instructions.
class InstructionSelectPass
    : public RequiredPassInfoMixin<InstructionSelectPass> {
  CodeGenOptLevel OptLevel;
  bool RequireRegBankSelection = true;

public:
  /// Construct the new-PM GlobalISel instruction selection pass.
  ///
  /// \param OL Optimization level used when requesting profile-driven analyses.
  /// \param RequireRegBankSelection When true, require register bank selection.
  InstructionSelectPass(CodeGenOptLevel OL = CodeGenOptLevel::Default,
                        bool RequireRegBankSelection = true);
  /// Select generic instructions in \p MF to target-specific instructions.
  ///
  /// \param MF Machine function whose instructions are selected.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);

  /// Return the properties this pass requires of the machine function.
  ///
  /// Selection expects SSA form and a legalized function. When
  /// RequireRegBankSelection is true, register banks must also be assigned.
  ///
  /// \return The required machine function properties.
  MachineFunctionProperties getRequiredProperties() const {
    MachineFunctionProperties RequiredProperties;
    RequiredProperties.setIsSSA().setLegalized();
    if (RequireRegBankSelection)
      RequiredProperties.setRegBankSelected();
    return RequiredProperties;
  }

  /// Return the properties this pass sets on the machine function.
  ///
  /// Marks the function as having completed instruction selection.
  ///
  /// \return The machine function properties set by this pass.
  MachineFunctionProperties getSetProperties() const {
    return MachineFunctionProperties().setSelected();
  }
};

} // End namespace llvm.

#endif
