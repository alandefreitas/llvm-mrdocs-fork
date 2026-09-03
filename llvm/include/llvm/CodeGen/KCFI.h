//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the MachineKCFI class, which is a
/// Machine Pass that implements kernel control flow integrity.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_KCFI_H
#define LLVM_CODEGEN_KCFI_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that implements kernel control-flow integrity.
///
/// For each call with a cfi-type attribute, emits an architecture-specific
/// check before the call and bundles the check with the call.
class MachineKCFIPass : public RequiredPassInfoMixin<MachineKCFIPass> {
public:
  /// Insert KCFI indirect call checks into \p MF.
  /// \param MF Machine function whose indirect calls are checked.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after inserting KCFI checks.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_KCFI_H
