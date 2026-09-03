//===- MachineCycleAnalysis.h - Cycle Info for Machine IR -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachineCycleInfo class, which is a thin wrapper over
// the Machine IR instance of GenericCycleInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINECYCLEANALYSIS_H
#define LLVM_CODEGEN_MACHINECYCLEANALYSIS_H

#include "llvm/ADT/GenericCycleInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/MachineSSAContext.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Cycle information specialized for Machine IR.
class MachineCycleInfo : public GenericCycleInfo<MachineSSAContext> {};

/// Legacy analysis pass which computes a \ref MachineCycleInfo.
class LLVM_ABI MachineCycleInfoWrapperPass : public MachineFunctionPass {
  MachineFunction *F = nullptr;
  MachineCycleInfo CI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the machine cycle info analysis pass.
  MachineCycleInfoWrapperPass();

  /// Return the computed cycle information.
  ///
  /// \return Mutable reference to the computed cycle information.
  MachineCycleInfo &getCycleInfo() { return CI; }
  /// Return the computed cycle information.
  ///
  /// \return Const reference to the computed cycle information.
  const MachineCycleInfo &getCycleInfo() const { return CI; }

  /// Compute cycle information for machine function \p F.
  ///
  /// \param F Machine function to analyze.
  /// \return False; this analysis does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &F) override;
  /// Declare analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Release memory held by the computed cycle information.
  void releaseMemory() override;
  /// Print the cycle analysis results.
  ///
  /// \param OS Output stream for the dump.
  /// \param M Optional module; unused by this pass.
  void print(raw_ostream &OS, const Module *M = nullptr) const override;
};

// TODO: add this function to the GenericCycleInfo template after implementing
//       the IR version.
/// Return true if instruction \p I is invariant in cycle \p Cycle.
///
/// \param CI Cycle information for the containing machine function.
/// \param Cycle Cycle in which invariance is tested.
/// \param I Instruction to test for cycle invariance.
/// \return True if \p I may be treated as invariant with respect to \p Cycle.
LLVM_ABI bool isCycleInvariant(const MachineCycleInfo &CI, CycleRef Cycle,
                               MachineInstr &I);

/// Analysis pass that computes \c MachineCycleInfo for a machine function.
class MachineCycleAnalysis : public AnalysisInfoMixin<MachineCycleAnalysis> {
  friend AnalysisInfoMixin<MachineCycleAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = MachineCycleInfo;

  /// Compute cycle information for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MFAM Analysis manager for the machine function.
  /// \return Cycle info for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);

  /// Invalidate this analysis result when required by the new pass manager.
  ///
  /// \param MF Machine function whose analysis result may be invalidated.
  /// \param PA Set of analyses preserved by the transform.
  /// \param Inv Invalidator for resolving analysis dependencies.
  /// \return True if this result should be discarded.
  LLVM_ABI bool invalidate(MachineFunction &MF, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);
};

/// Printer pass for the \c MachineCycleAnalysis results.
class MachineCycleInfoPrinterPass
    : public RequiredPassInfoMixin<MachineCycleInfoPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  ///
  /// \param OS Output stream for the cycle dump.
  explicit MachineCycleInfoPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print MachineCycleAnalysis results for \p MF.
  ///
  /// \param MF Machine function whose cycle info is printed.
  /// \param MFAM Analysis manager providing MachineCycleAnalysis.
  /// \return All analyses preserved; this pass does not transform \p MF.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINECYCLEANALYSIS_H
