//===- llvm/CodeGen/ExpandPostRAPseudos.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_EXPANDPOSTRAPSEUDOS_H
#define LLVM_CODEGEN_EXPANDPOSTRAPSEUDOS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that expands post-RA pseudo instructions.
///
/// Pseudoinstructions must be expanded regardless of optimization level;
/// otherwise later passes (e.g., AsmPrinter) will fail.
class ExpandPostRAPseudosPass
    : public RequiredPassInfoMixin<ExpandPostRAPseudosPass> {
public:
  /// Expand post-RA pseudo instructions in \p MF.
  /// \param MF Machine function whose post-RA pseudos are expanded.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after expanding post-RA pseudos.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_EXPANDPOSTRAPSEUDOS_H
