///===- LazyMachineBlockFrequencyInfo.h - Lazy Block Frequency -*- C++ -*--===//
///
/// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
/// See https://llvm.org/LICENSE.txt for license information.
/// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
///
///===---------------------------------------------------------------------===//
/// \file
/// This is an alternative analysis pass to MachineBlockFrequencyInfo.  The
/// difference is that with this pass the block frequencies are not computed
/// when the analysis pass is executed but rather when the BFI result is
/// explicitly requested by the analysis client.
///
///===---------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LAZYMACHINEBLOCKFREQUENCYINFO_H
#define LLVM_CODEGEN_LAZYMACHINEBLOCKFREQUENCYINFO_H

#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class MachineCycleInfo;

/// Alternative analysis pass that computes machine block frequencies on demand.
///
/// This is an alternative analysis pass to MachineBlockFrequencyInfo.
/// The difference is that with this pass, the block frequencies are not
/// computed when the analysis pass is executed but rather when the BFI result
/// is explicitly requested by the analysis client.
///
/// This works by checking querying if MBFI is available and otherwise
/// generating MBFI on the fly.  In this case the passes required for (LI, DT)
/// are also queried before being computed on the fly.
///
/// Note that it is expected that we wouldn't need this functionality for the
/// new PM since with the new PM, analyses are executed on demand.

class LLVM_ABI LazyMachineBlockFrequencyInfoPass : public MachineFunctionPass {
private:
  /// If generated on the fly this own the instance.
  mutable std::unique_ptr<MachineBlockFrequencyInfo> OwnedMBFI;

  /// If generated on the fly this own the instance.
  mutable std::unique_ptr<MachineCycleInfo> OwnedMCI;

  /// The function.
  MachineFunction *MF = nullptr;

  /// Calculate MBFI and all other analyses that's not available and
  /// required by BFI.
  MachineBlockFrequencyInfo &calculateIfNotAvailable() const;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct a LazyMachineBlockFrequencyInfoPass.
  LazyMachineBlockFrequencyInfoPass();

  /// Compute and return the block frequencies.
  /// @return Block frequency info, computed on demand if needed.
  MachineBlockFrequencyInfo &getBFI() { return calculateIfNotAvailable(); }

  /// Compute and return the block frequencies.
  /// @return Const block frequency info, computed on demand if needed.
  const MachineBlockFrequencyInfo &getBFI() const {
    return calculateIfNotAvailable();
  }

  /// Declare the analyses required and preserved by this pass.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Set up lazy BFI analysis for machine function \p F.
  /// @param F Machine function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnMachineFunction(MachineFunction &F) override;
  /// Release the cached lazy MBFI between runs.
  void releaseMemory() override;
};
}
#endif
