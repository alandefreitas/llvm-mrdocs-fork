//===-- llvm/CodeGen/WinEHPrepare.h ----------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_WINEHPREPARE_H
#define LLVM_CODEGEN_WINEHPREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// New PM pass that prepares Windows exception handling for code generation.
///
/// Transforms IR exception handling into the Windows funclet-based form expected
/// by code generation. Required if using Windows exception handling.
class WinEHPreparePass : public RequiredPassInfoMixin<WinEHPreparePass> {
  bool DemoteCatchSwitchPHIOnly;

public:
  /// Construct a pass that optionally demotes only catchswitch-related PHIs.
  /// \param DemoteCatchSwitchPHIOnly_ When true, demote PHIs only in
  ///        catchswitch-related blocks (used for wasm EH).
  WinEHPreparePass(bool DemoteCatchSwitchPHIOnly_ = false)
      : DemoteCatchSwitchPHIOnly(DemoteCatchSwitchPHIOnly_) {}
  /// Prepare Windows exception handling in \p F for code generation.
  /// \param F Function whose exception handling is prepared.
  /// \param FAM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_WINEHPREPARE_H
