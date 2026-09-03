//===- llvm/CodeGen/FEntryInserter.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_FENTRYINSERTER_H
#define LLVM_CODEGEN_FENTRYINSERTER_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that inserts fentry calls into function prologues.
///
/// When a function has the \c fentry-call attribute set to \c true, this pass
/// inserts an \c FENTRY_CALL instruction at the start of the entry block.
class FEntryInserterPass : public RequiredPassInfoMixin<FEntryInserterPass> {
public:
  /// Insert an fentry call into \p MF when requested by function attributes.
  /// \param MF Machine function that may receive an fentry call.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after inserting fentry calls.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_FENTRYINSERTER_H
