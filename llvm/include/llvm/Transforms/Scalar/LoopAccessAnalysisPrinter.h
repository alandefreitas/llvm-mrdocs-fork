//===- llvm/Analysis/LoopAccessAnalysisPrinter.h ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPACCESSANALYSISPRINTER_H
#define LLVM_TRANSFORMS_SCALAR_LOOPACCESSANALYSISPRINTER_H
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;
class raw_ostream;

/// Printer pass for the \c LoopAccessInfo results.
class LoopAccessInfoPrinterPass
    : public RequiredPassInfoMixin<LoopAccessInfoPrinterPass> {
  raw_ostream &OS;
  bool AllowPartial;

public:
  /// Construct a printer that writes LoopAccessInfo results to \p OS.
  /// @param OS Output stream for the printed analysis.
  /// @param AllowPartial When true, keep partial runtime checks on failure.
  explicit LoopAccessInfoPrinterPass(raw_ostream &OS, bool AllowPartial)
      : OS(OS), AllowPartial(AllowPartial) {}

  /// Print LoopAccessInfo for each loop in \p F and return all analyses preserved.
  /// @param F Function whose loops are analyzed and printed.
  /// @param AM Function analysis manager providing LoopAccessAnalysis.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // End llvm namespace

#endif
