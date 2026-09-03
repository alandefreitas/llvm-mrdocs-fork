//===- UniformityAnalysis.h ---------------------*- C++ -*-----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief LLVM IR instance of the generic uniformity analysis
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_UNIFORMITYANALYSIS_H
#define LLVM_ANALYSIS_UNIFORMITYANALYSIS_H

#include "llvm/ADT/GenericUniformityInfo.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/SSAContext.h"
#include "llvm/Pass.h"

namespace llvm {

/// Explicit instantiation of GenericUniformityInfo for LLVM IR.
extern template class GenericUniformityInfo<SSAContext>;
/// Uniformity information specialized for LLVM IR.
using UniformityInfo = GenericUniformityInfo<SSAContext>;

/// Analysis pass which computes \ref UniformityInfo.
class UniformityInfoAnalysis
    : public AnalysisInfoMixin<UniformityInfoAnalysis> {
  friend AnalysisInfoMixin<UniformityInfoAnalysis>;
  static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  using Result = UniformityInfo;

  /// Run the analysis pass over a function and produce UniformityInfo.
  ///
  /// \param F Function to analyze.
  /// \param AM Function analysis manager providing required analyses.
  /// \return The computed UniformityInfo for \p F.
  LLVM_ABI UniformityInfo run(Function &F, FunctionAnalysisManager &AM);

  // TODO: verify analysis
};

/// Printer pass for the \c UniformityInfo.
class UniformityInfoPrinterPass
    : public RequiredPassInfoMixin<UniformityInfoPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  ///
  /// \param OS Output stream for the printed UniformityInfo.
  LLVM_ABI explicit UniformityInfoPrinterPass(raw_ostream &OS);

  /// Print UniformityInfo for \p F and return all analyses preserved.
  ///
  /// \param F Function whose UniformityInfo is printed.
  /// \param AM Function analysis manager providing UniformityInfo.
  /// \return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy analysis pass which computes a \ref CycleInfo.
class LLVM_ABI UniformityInfoWrapperPass : public FunctionPass {
  Function *Fn = nullptr;
  UniformityInfo UI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy UniformityInfo wrapper pass.
  UniformityInfoWrapperPass();

  /// Return the UniformityInfo computed by this pass.
  ///
  /// \return The UniformityInfo computed by this pass.
  UniformityInfo &getUniformityInfo() { return UI; }
  /// Return the UniformityInfo computed by this pass.
  ///
  /// \return The UniformityInfo computed by this pass.
  const UniformityInfo &getUniformityInfo() const { return UI; }

  /// Compute UniformityInfo for \p F.
  ///
  /// \param F Function to analyze.
  /// \return False; this analysis does not modify the function.
  bool runOnFunction(Function &F) override;
  /// Declare required and preserved analyses for this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Release the UniformityInfo owned by this pass.
  void releaseMemory() override;
  /// Print the UniformityInfo computed by this pass.
  ///
  /// \param OS Output stream.
  /// \param M Optional module (unused).
  void print(raw_ostream &OS, const Module *M = nullptr) const override;

  // TODO: verify analysis
};

} // namespace llvm

#endif // LLVM_ANALYSIS_UNIFORMITYANALYSIS_H
