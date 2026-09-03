//===- CostModel.h - --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_COSTMODEL_H
#define LLVM_ANALYSIS_COSTMODEL_H

#include "llvm/IR/PassManager.h"

namespace llvm {
/// Printer pass for cost modeling results.
class CostModelPrinterPass
    : public RequiredPassInfoMixin<CostModelPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes cost-model results to \p OS.
  /// @param OS Output stream for the printed costs.
  explicit CostModelPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print cost-model results for \p F.
  /// @param F Function whose instruction costs are printed.
  /// @param AM Function analysis manager providing TargetIRAnalysis.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_ANALYSIS_COSTMODEL_H
