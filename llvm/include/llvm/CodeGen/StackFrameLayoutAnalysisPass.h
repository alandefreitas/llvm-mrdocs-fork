//===- llvm/CodeGen/StackFrameLayoutAnalysisPass.h --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_STACKFRAMELAYOUTANALYSISPASS_H
#define LLVM_CODEGEN_STACKFRAMELAYOUTANALYSISPASS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that dumps the stack frame layout via remarks.
///
/// Outputs information about the layout of the stack frame using the remarks
/// interface. On the CLI it prints a textual representation of the stack
/// frame, and when possible includes the values that occupy each stack slot
/// from available debug information. Because output is remarks-based, it is
/// also available in machine-readable formats such as YAML.
class StackFrameLayoutAnalysisPass
    : public RequiredPassInfoMixin<StackFrameLayoutAnalysisPass> {
public:
  /// Analyze and report the stack frame layout of \p MF.
  /// \param MF Machine function whose stack frame is analyzed.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after reporting the stack frame layout.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_STACKFRAMELAYOUTANALYSISPASS_H
