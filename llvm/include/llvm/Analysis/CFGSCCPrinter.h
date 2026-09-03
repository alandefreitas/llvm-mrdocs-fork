//===-- CFGSCCPrinter.h ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CFGSCCPRINTER_H
#define LLVM_ANALYSIS_CFGSCCPRINTER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Printer pass for strongly connected components of a function CFG.
class CFGSCCPrinterPass : public RequiredPassInfoMixin<CFGSCCPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a CFG SCC printer that writes to \p OS.
  /// @param OS Output stream for the printed SCCs.
  explicit CFGSCCPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print the CFG SCCs of function \p F in postorder.
  /// @param F Function whose CFG SCCs are printed.
  /// @param AM Function analysis manager (unused).
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif
