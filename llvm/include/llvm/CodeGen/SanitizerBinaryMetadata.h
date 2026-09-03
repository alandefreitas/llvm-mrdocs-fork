//===- llvm/CodeGen/SanitizerBinaryMetadata.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SANITIZERBINARYMETADATA_H
#define LLVM_CODEGEN_SANITIZERBINARYMETADATA_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that finalizes sanitizer binary metadata for a machine function.
///
/// For functions covered by sanitizer PC-section metadata with use-after-return
/// enabled, appends the size of stack arguments to the metadata.
class MachineSanitizerBinaryMetadataPass
    : public RequiredPassInfoMixin<MachineSanitizerBinaryMetadataPass> {
public:
  /// Finalize sanitizer binary metadata for \p MF.
  /// \param MF Machine function whose PC-section metadata may be updated.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_SANITIZERBINARYMETADATA_H
