//===- llvm/CodeGen/FixupStatepointCallerSaved.h ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_FIXUPSTATEPOINTCALLERSAVED_H
#define LLVM_CODEGEN_FIXUPSTATEPOINTCALLERSAVED_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that spills caller-saved registers used by statepoints.
///
/// Statepoint deopt operands must remain readable when the call returns, but
/// the register allocator may place them in registers clobbered by the call.
/// This pass spills those registers and rewrites the corresponding statepoint
/// operands to refer to the spill slots.
class FixupStatepointCallerSavedPass
    : public OptionalPassInfoMixin<FixupStatepointCallerSavedPass> {
public:
  /// Spill caller-saved registers referenced by statepoints in \p MF.
  /// \param MF Machine function whose statepoint operands are fixed up.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after fixing up statepoint
  /// caller-saved registers.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_FIXUPSTATEPOINTCALLERSAVED_H
