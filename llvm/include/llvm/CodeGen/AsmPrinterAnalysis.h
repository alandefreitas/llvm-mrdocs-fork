//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Holds an AsmPrinter instance so that state can be shared appropriately
// between the Module and MachineFunction portions of AsmPrinter.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ASMPRINTERANALYSIS_H
#define LLVM_CODEGEN_ASMPRINTERANALYSIS_H

#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// Module analysis that holds a shared AsmPrinter for a module.
///
/// Owns an AsmPrinter so that state can be shared between the Module and
/// MachineFunction portions of assembly emission.
class AsmPrinterAnalysis : public AnalysisInfoMixin<AsmPrinterAnalysis> {
public:
  /// Analysis key used to identify AsmPrinterAnalysis.
  LLVM_ABI static AnalysisKey Key;
  /// AsmPrinter owned by this analysis and shared across functions.
  std::unique_ptr<AsmPrinter> HeldPrinter;

  /// Cached analysis result wrapping the held AsmPrinter.
  class Result {
    AsmPrinter &Printer;
    Result(AsmPrinter &Printer) : Printer(Printer) {}
    friend class AsmPrinterAnalysis;

  public:
    /// Return the AsmPrinter wrapped by this result.
    ///
    /// \return The AsmPrinter owned by the parent analysis.
    AsmPrinter &getPrinter() { return Printer; }

    /// Return false so the held printer is never invalidated.
    ///
    /// The printer must remain alive for both the Module and MachineFunction
    /// phases of assembly emission.
    ///
    /// \param M Module for which invalidation is queried (unused).
    /// \param PA Set of analyses preserved by the last transformation (unused).
    /// \param Inv Invalidator for other module analyses (unused).
    /// \return Always false, so the held printer is never invalidated.
    bool invalidate(Module &M, const PreservedAnalyses &PA,
                    ModuleAnalysisManager::Invalidator &Inv) {
      return false;
    }
  };

    /// Return a result wrapping the held AsmPrinter.
  ///
  /// \param M Module being analyzed (unused).
  /// \param MAM Module analysis manager (unused).
  /// \return A Result wrapping the held AsmPrinter.
  Result run(Module &M, ModuleAnalysisManager &MAM) {
    return Result(*HeldPrinter);
  }

public:
  /// Construct an analysis that takes ownership of \p Printer.
  ///
  /// \param Printer AsmPrinter to hold and share across the module.
  AsmPrinterAnalysis(std::unique_ptr<AsmPrinter> Printer)
      : HeldPrinter(std::move(Printer)) {}
};

} // namespace llvm

#endif //  LLVM_CODEGEN_ASMPRINTERANALYSIS_H
