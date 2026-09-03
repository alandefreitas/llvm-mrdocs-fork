//===- llvm/CodeGen/PEI.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PEI_H
#define LLVM_CODEGEN_PEI_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that inserts function prologs and epilogs.
class PrologEpilogInserterPass
    : public RequiredPassInfoMixin<PrologEpilogInserterPass> {
public:
  /// Insert prolog and epilog code into \p MF.
  /// \param MF Machine function to instrument with prolog and epilog.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after inserting prologs and epilogs.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_PEI_H
