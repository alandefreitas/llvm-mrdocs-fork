//===- MachineUniformityAnalysis.h ---------------------------*- C++ -*----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief Machine IR instance of the generic uniformity analysis
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEUNIFORMITYANALYSIS_H
#define LLVM_CODEGEN_MACHINEUNIFORMITYANALYSIS_H

#include "llvm/ADT/GenericUniformityInfo.h"
#include "llvm/CodeGen/MachineCycleAnalysis.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/MachineSSAContext.h"

namespace llvm {

/// Explicit instantiation of GenericUniformityInfo for Machine IR.
extern template class GenericUniformityInfo<MachineSSAContext>;
/// Uniformity information specialized for Machine IR.
using MachineUniformityInfo = GenericUniformityInfo<MachineSSAContext>;

/// Compute uniformity information for a Machine IR function.
///
/// If \p HasBranchDivergence is false, produces a dummy result which assumes
/// everything is uniform.
///
/// \param F Machine function to analyze.
/// \param CI Cycle information for \p F.
/// \param DT Dominator tree for \p F.
/// \param HasBranchDivergence Whether the target has divergent control flow.
/// \return Uniformity info for \p F.
LLVM_ABI MachineUniformityInfo computeMachineUniformityInfo(
    MachineFunction &F, const MachineCycleInfo &CI,
    const MachineDominatorTree &DT, bool HasBranchDivergence);

/// Legacy analysis pass which computes a \ref MachineUniformityInfo.
class LLVM_ABI MachineUniformityAnalysisPass : public MachineFunctionPass {
  MachineUniformityInfo UI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the machine uniformity analysis pass.
  MachineUniformityAnalysisPass();

  /// Return the computed uniformity information.
  ///
  /// \return The machine uniformity info computed by this pass.
  MachineUniformityInfo &getUniformityInfo() { return UI; }
  /// Return the computed uniformity information.
  ///
  /// \return The machine uniformity info computed by this pass.
  const MachineUniformityInfo &getUniformityInfo() const { return UI; }

  /// Compute uniformity information for machine function \p F.
  ///
  /// \param F Machine function to analyze.
  /// \return False; this analysis does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &F) override;
  /// Declare analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Print the uniformity analysis results.
  ///
  /// \param OS Output stream for the dump.
  /// \param M Optional module; unused by this pass.
  void print(raw_ostream &OS, const Module *M = nullptr) const override;

  // TODO: verify analysis
};

/// Analysis pass that computes \c MachineUniformityInfo for a machine function.
class MachineUniformityAnalysis
    : public AnalysisInfoMixin<MachineUniformityAnalysis> {
  friend AnalysisInfoMixin<MachineUniformityAnalysis>;
  static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = MachineUniformityInfo;
  /// Compute uniformity information for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MFAM Analysis manager for the machine function.
  /// \return Uniformity info for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// Printer pass for the \c MachineUniformityAnalysis results.
class MachineUniformityPrinterPass
    : public RequiredPassInfoMixin<MachineUniformityPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  ///
  /// \param OS Output stream for the uniformity dump.
  explicit MachineUniformityPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print MachineUniformityAnalysis results for \p MF.
  ///
  /// \param MF Machine function whose uniformity info is printed.
  /// \param MFAM Analysis manager providing MachineUniformityAnalysis.
  /// \return All analyses preserved; this pass does not transform \p MF.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINEUNIFORMITYANALYSIS_H
