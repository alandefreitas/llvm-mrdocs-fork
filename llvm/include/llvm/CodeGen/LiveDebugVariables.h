//===- LiveDebugVariables.h - Tracking debug info variables -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface to the LiveDebugVariables analysis.
//
// The analysis removes DBG_VALUE instructions for virtual registers and tracks
// live user variables in a data structure that can be updated during register
// allocation.
//
// After register allocation new DBG_VALUE instructions are emitted to reflect
// the new locations of user variables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVEDEBUGVARIABLES_H
#define LLVM_CODEGEN_LIVEDEBUGVARIABLES_H

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace llvm {

template <typename T> class ArrayRef;
class LiveIntervals;
class VirtRegMap;

/// Analysis that tracks user variables across register allocation.
class LiveDebugVariables {

public:
  /// Opaque implementation of LiveDebugVariables.
  class LDVImpl;
  /// Construct an empty LiveDebugVariables analysis.
  LLVM_ABI LiveDebugVariables();
  /// Destroy the LiveDebugVariables analysis.
  LLVM_ABI ~LiveDebugVariables();
  /// Move-construct LiveDebugVariables from \p Other.
  ///
  /// \param Other Source analysis to move from.
  LLVM_ABI LiveDebugVariables(LiveDebugVariables &&Other);

  /// Analyze machine function \p MF and build live debug variable info.
  ///
  /// \param MF Machine function to analyze.
  /// \param LIS Live intervals used while tracking debug values.
  LLVM_ABI void analyze(MachineFunction &MF, LiveIntervals *LIS);
  /// Move user variables from \p OldReg onto the live ranges in \p NewRegs.
  ///
  /// Mark the values as unavailable where no new register is live.
  ///
  /// \param OldReg Virtual register whose debug users are being remapped.
  /// \param NewRegs Replacement registers covering parts of \p OldReg.
  /// \param LIS Live intervals used to determine where values remain live.
  LLVM_ABI void splitRegister(Register OldReg, ArrayRef<Register> NewRegs,
                              LiveIntervals &LIS);

  /// emitDebugValues - Emit new DBG_VALUE instructions reflecting the changes
  /// that happened during register allocation.
  /// @param VRM Rename virtual registers according to map.
  LLVM_ABI void emitDebugValues(VirtRegMap *VRM);

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// dump - Print data structures to dbgs().
  void dump() const;
#endif

  /// Print live debug variable data structures to \p OS.
  ///
  /// \param OS Output stream for the dump.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Release memory used by the analysis.
  LLVM_ABI void releaseMemory();

  /// Invalidate cached analysis results when dependencies change.
  ///
  /// \param MF Machine function whose analyses may be invalidated.
  /// \param PA Set of analyses preserved by the last transformation.
  /// \param Inv Invalidator for dependent machine function analyses.
  /// \return True if this analysis should be discarded.
  LLVM_ABI bool invalidate(MachineFunction &MF, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);

private:
  std::unique_ptr<LDVImpl> PImpl;
};

/// Legacy pass wrapper for LiveDebugVariables.
class LLVM_ABI LiveDebugVariablesWrapperLegacy : public MachineFunctionPass {
  std::unique_ptr<LiveDebugVariables> Impl;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy LiveDebugVariables wrapper pass.
  LiveDebugVariablesWrapperLegacy();

  /// Run LiveDebugVariables on machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \return False; this analysis does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &MF) override;

  /// Return the computed LiveDebugVariables analysis.
  ///
  /// \return Reference to the wrapped LiveDebugVariables result.
  LiveDebugVariables &getLDV() { return *Impl; }
  /// Return the computed LiveDebugVariables analysis.
  ///
  /// \return Const reference to the wrapped LiveDebugVariables result.
  const LiveDebugVariables &getLDV() const { return *Impl; }

  /// Release memory used by the wrapped analysis.
  void releaseMemory() override {
    if (Impl)
      Impl->releaseMemory();
  }
  /// Declare analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Return machine function properties required by this pass.
  ///
  /// \return Properties that include tracking of debug user values.
  MachineFunctionProperties getSetProperties() const override {
    return MachineFunctionProperties().setTracksDebugUserValues();
  }
};

/// Analysis pass that computes \c LiveDebugVariables for a machine function.
class LiveDebugVariablesAnalysis
    : public AnalysisInfoMixin<LiveDebugVariablesAnalysis> {
  friend AnalysisInfoMixin<LiveDebugVariablesAnalysis>;
  static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = LiveDebugVariables;

  /// Return machine function properties required by this analysis.
  ///
  /// \return Properties that include tracking of debug user values.
  MachineFunctionProperties getSetProperties() const {
    return MachineFunctionProperties().setTracksDebugUserValues();
  }

  /// Compute LiveDebugVariables for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MFAM Analysis manager for the machine function.
  /// \return Live debug variable info for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// Printer pass for the \c LiveDebugVariablesAnalysis results.
class LiveDebugVariablesPrinterPass
    : public RequiredPassInfoMixin<LiveDebugVariablesPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  ///
  /// \param OS Output stream for the live debug variables dump.
  LiveDebugVariablesPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print LiveDebugVariablesAnalysis results for \p MF.
  ///
  /// \param MF Machine function whose live debug variables are printed.
  /// \param MFAM Analysis manager providing LiveDebugVariablesAnalysis.
  /// \return All analyses preserved; this pass does not transform \p MF.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};
} // end namespace llvm

#endif // LLVM_CODEGEN_LIVEDEBUGVARIABLES_H
