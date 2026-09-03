//===- MemDerefPrinter.h - Printer for isDereferenceablePointer -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMDEREFPRINTER_H
#define LLVM_ANALYSIS_MEMDEREFPRINTER_H

#include "llvm/IR/PassManager.h"

namespace llvm {
/// Printer pass for whether values are known dereferenceable pointers.
class MemDerefPrinterPass : public RequiredPassInfoMixin<MemDerefPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a memory-dereferenceability printer that writes to \p OS.
  /// @param OS Output stream for the printed results.
  MemDerefPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print which values in \p F are known dereferenceable pointers.
  /// @param F Function whose values are inspected.
  /// @param AM Function analysis manager providing supporting analyses.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif // LLVM_ANALYSIS_MEMDEREFPRINTER_H
