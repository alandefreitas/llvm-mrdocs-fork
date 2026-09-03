//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CFIINSTRINSERTER_H
#define LLVM_CODEGEN_CFIINSTRINSERTER_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that inserts correcting CFI instructions.
///
/// Verifies CFA information across basic-block boundaries and inserts
/// additional CFI instructions at block beginnings when predecessors leave
/// an incorrect CFA offset or register due to non-linear layout.
class CFIInstrInserterPass
    : public RequiredPassInfoMixin<CFIInstrInserterPass> {
public:
  /// Insert correcting CFI instructions in \p MF.
  ///
  /// \param MF Machine function whose call-frame info is verified and fixed.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after inserting CFI instructions.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_CFIINSTRINSERTER_H
