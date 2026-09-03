//===--- llvm/CodeGen/WasmEHPrepare.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_WASMEHPREPARE_H
#define LLVM_CODEGEN_WASMEHPREPARE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// New PM pass that prepares WebAssembly exception handling for code
/// generation.
///
/// Transforms catchpad blocks for the WebAssembly exception handling scheme.
/// Required if using WebAssembly exception handling.
class WasmEHPreparePass : public RequiredPassInfoMixin<WasmEHPreparePass> {
public:
  /// Prepare WebAssembly exception handling in \p F for code generation.
  /// \param F Function whose exception handling is prepared.
  /// \param FAM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_WASMEHPREPARE_H
