//===-- llvm/CodeGen/JMCInstrumenter------------------------ ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_JMCINSTRUMENTER_H
#define LLVM_CODEGEN_JMCINSTRUMENTER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// New PM pass that instruments functions for Just My Code debugging.
///
/// Inserts a call to \c __CheckForDebuggerJustMyCode at each function entry,
/// using a per-file flag in the \c .msvcjmc section, and provides a weak
/// default stub so linking succeeds when the real check is unavailable.
class JMCInstrumenterPass : public RequiredPassInfoMixin<JMCInstrumenterPass> {
public:
  /// Instrument functions in \p M with Just My Code debugger checks.
  /// \param M Module whose functions are instrumented.
  /// \param MAM Module analysis manager providing required analyses.
  /// \return The set of analyses preserved after instrumenting functions.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_JMCINSTRUMENTER_H
