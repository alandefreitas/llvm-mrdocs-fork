//===- IVUsersPrinter.h - Induction Variable Users Printing -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_IVUSERSPRINTER_H
#define LLVM_TRANSFORMS_SCALAR_IVUSERSPRINTER_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class LPMUpdater;
class Loop;
class raw_ostream;

/// Printer pass for the \c IVUsers for a loop.
class IVUsersPrinterPass : public RequiredPassInfoMixin<IVUsersPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes IVUsers results to \p OS.
  /// @param OS Output stream for the printed analysis.
  explicit IVUsersPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print the IVUsers for loop \p L and return all analyses preserved.
  /// @param L Loop whose induction-variable users are printed.
  /// @param AM Loop analysis manager providing IVUsers.
  /// @param AR Standard loop analysis results.
  /// @param U Loop pass manager updater (unused by the printer).
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};
}

#endif
