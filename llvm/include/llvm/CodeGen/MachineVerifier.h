//===- llvm/CodeGen/MachineVerifier.h - Machine Code Verifier ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEVERIFIER_H
#define LLVM_CODEGEN_MACHINEVERIFIER_H

#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {
/// New PM pass that verifies the integrity of machine code.
class MachineVerifierPass : public RequiredPassInfoMixin<MachineVerifierPass> {
  std::string Banner;

public:
  /// Construct a MachineVerifier pass.
  /// \param Banner Optional banner printed with verification diagnostics.
  MachineVerifierPass(const std::string &Banner = std::string())
      : Banner(Banner) {}
  /// Verify the integrity of machine code in \p MF.
  /// \param MF Machine function whose instructions are verified.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// @return A PreservedAnalyses set with all analyses preserved.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINEVERIFIER_H
