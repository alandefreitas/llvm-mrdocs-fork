//===- CycleAnalysis.h - Cycle Info for LLVM IR -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file declares an analysis pass that computes CycleInfo for
/// LLVM IR, specialized from GenericCycleInfo.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CYCLEANALYSIS_H
#define LLVM_ANALYSIS_CYCLEANALYSIS_H

#include "llvm/IR/CycleInfo.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

/// Legacy analysis pass which computes a \ref CycleInfo.
class LLVM_ABI CycleInfoWrapperPass : public FunctionPass {
  Function *F = nullptr;
  CycleInfo CI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy CycleInfo wrapper pass.
  CycleInfoWrapperPass();

  /// Return the CycleInfo computed by this pass.
  /// @return The CycleInfo computed by this pass.
  CycleInfo &getResult() { return CI; }
  /// Return the CycleInfo computed by this pass.
  /// @return The CycleInfo computed by this pass.
  const CycleInfo &getResult() const { return CI; }

  /// Compute CycleInfo for \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis does not modify the function.
  bool runOnFunction(Function &F) override;
  /// Declare required and preserved analyses for this pass.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Release the CycleInfo owned by this pass.
  void releaseMemory() override;
  /// Print the CycleInfo computed by this pass.
  /// @param OS Output stream.
  /// @param M Optional module (unused).
  void print(raw_ostream &OS, const Module *M = nullptr) const override;

  // TODO: verify analysis?
};

/// Analysis pass which computes a \ref CycleInfo.
class CycleAnalysis : public AnalysisInfoMixin<CycleAnalysis> {
  friend AnalysisInfoMixin<CycleAnalysis>;
  static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  using Result = CycleInfo;

  /// Legacy pass manager wrapper for this analysis.
  using LegacyWrapper = CycleInfoWrapperPass;

  /// Run the analysis pass over a function and produce a CycleInfo.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager (unused).
  /// @return The computed CycleInfo for \p F.
  LLVM_ABI CycleInfo run(Function &F, FunctionAnalysisManager &AM);

  /// Handle invalidation explicitly.
  /// @param F Function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

  // TODO: verify analysis?
};

/// Printer pass for the \c CycleInfo.
class CycleInfoPrinterPass
    : public RequiredPassInfoMixin<CycleInfoPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed CycleInfo.
  LLVM_ABI explicit CycleInfoPrinterPass(raw_ostream &OS);
  /// Print the CycleInfo for \p F and return all analyses preserved.
  /// @param F Function whose CycleInfo is printed.
  /// @param AM Function analysis manager providing CycleInfo.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Verifier pass for the \c CycleInfo.
struct CycleInfoVerifierPass
    : public RequiredPassInfoMixin<CycleInfoVerifierPass> {
  /// Verify CycleInfo for \p F and return all analyses preserved.
  /// @param F Function whose CycleInfo is verified.
  /// @param AM Function analysis manager providing CycleInfo.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_CYCLEANALYSIS_H
