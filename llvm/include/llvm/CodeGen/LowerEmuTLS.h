//==------ llvm/CodeGen/LowerEmuTLS.h -------------------------*- C++ -*----==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Add Add __emutls_[vt].* variables.
///
/// This file provide declaration of LowerEmuTLSPass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LOWEREMUTLS_H
#define LLVM_CODEGEN_LOWEREMUTLS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// New PM pass that adds `__emutls_[vt].*` variables for emulated TLS.
///
/// For each thread-local global `xyz`, generates `__emutls_v.xyz`, and also
/// `__emutls_t.xyz` when the variable has a non-zero initializer.
class LowerEmuTLSPass : public RequiredPassInfoMixin<LowerEmuTLSPass> {
public:
  /// Add emulated TLS variables for thread-local globals in \p M.
  /// \param M Module whose thread-local globals are lowered.
  /// \param MAM Module analysis manager providing required analyses.
  /// \return The set of analyses preserved after adding emulated TLS variables.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_LOWEREMUTLS_H
