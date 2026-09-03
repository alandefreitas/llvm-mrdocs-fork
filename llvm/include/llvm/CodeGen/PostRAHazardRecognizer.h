//===- llvm/CodeGen/PostRAHazardRecognizer.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_POSTRAHAZARDRECOGNIZER_H
#define LLVM_CODEGEN_POSTRAHAZARDRECOGNIZER_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that runs the post-RA hazard recognizer and emits noops.
class PostRAHazardRecognizerPass
    : public RequiredPassInfoMixin<PostRAHazardRecognizerPass> {
public:
  /// Run the post-RA hazard recognizer on machine instructions in \p MF.
  /// \param MF Machine function whose hazards are recognized.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after running the post-RA hazard
  ///         recognizer.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_POSTRAHAZARDRECOGNIZER_H
