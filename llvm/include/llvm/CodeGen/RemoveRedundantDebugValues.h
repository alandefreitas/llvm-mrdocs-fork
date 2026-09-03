//===- llvm/CodeGen/RemoveRedundantDebugValues.h ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REMOVEREDUNDANTDEBUGVALUES_H
#define LLVM_CODEGEN_REMOVEREDUNDANTDEBUGVALUES_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that removes redundant DBG_VALUE instructions.
///
/// Eliminates DBG_VALUEs that appear in MIR after register allocation when
/// they are redundant with respect to earlier debug value information.
class RemoveRedundantDebugValuesPass
    : public RequiredPassInfoMixin<RemoveRedundantDebugValuesPass> {
public:
  /// Remove redundant debug values in \p MF.
  /// \param MF Machine function whose redundant DBG_VALUEs are removed.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after removing redundant debug values.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_REMOVEREDUNDANTDEBUGVALUES_H
