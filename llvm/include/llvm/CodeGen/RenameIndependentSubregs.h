//===- llvm/CodeGen/RenameIndependentSubregs.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_RENAME_INDEPENDENT_SUBREGS_H
#define LLVM_CODEGEN_RENAME_INDEPENDENT_SUBREGS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that renames independently used subregisters.
///
/// Detects subregister lanes in a virtual register that are used
/// independently of other lanes and splits them into separate virtual
/// registers.
class RenameIndependentSubregsPass
    : public RequiredPassInfoMixin<RenameIndependentSubregsPass> {
public:
  /// Rename independently used subregisters in \p MF.
  /// \param MF Machine function whose independent subregisters are renamed.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after renaming independent
  ///         subregisters.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_RENAME_INDEPENDENT_SUBREGS_H
