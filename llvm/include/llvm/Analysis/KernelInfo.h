//=- KernelInfo.h - Kernel Analysis -------------------------------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the KernelInfoPrinter class used to emit remarks about
// function properties from a GPU kernel.
//
// See llvm/docs/KernelInfo.rst.
// ===---------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_KERNELINFO_H
#define LLVM_ANALYSIS_KERNELINFO_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class TargetMachine;

/// Pass that emits optimization remarks about GPU kernel function properties.
class KernelInfoPrinter : public RequiredPassInfoMixin<KernelInfoPrinter> {
  TargetMachine *TM;

public:
  /// Construct a kernel-info printer for target \p TM.
  /// @param TM Target machine used when collecting kernel properties.
  explicit KernelInfoPrinter(TargetMachine *TM) : TM(TM) {}

  /// Emit kernel-info remarks for function \p F when enabled.
  /// @param F Function to analyze as a GPU kernel.
  /// @param AM Function analysis manager providing supporting analyses.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm
#endif // LLVM_ANALYSIS_KERNELINFO_H
