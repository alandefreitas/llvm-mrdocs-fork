//===-- CFGuard.h - CFGuard Transformations ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
// Windows Control Flow Guard passes (/guard:cf).
//===---------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_CFGUARD_H
#define LLVM_TRANSFORMS_CFGUARD_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class CallBase;
class FunctionPass;
class GlobalValue;

/// New Pass Manager pass that inserts Control Flow Guard checks.
class CFGuardPass : public OptionalPassInfoMixin<CFGuardPass> {
public:
  /// How Control Flow Guard instruments indirect calls.
  enum class Mechanism {
    Check,    ///< Call a guard check function before the indirect call.
    Dispatch, ///< Replace the call with a guard dispatch that validates and jumps.
  };

  /// Construct a Control Flow Guard pass.
  CFGuardPass() {}

  /// Run Control Flow Guard instrumentation on a function.
  /// @param F Function to instrument with Control Flow Guard checks.
  /// @param FAM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

/// Insert Control Flow Guard checks on indirect function calls.
/// @return A FunctionPass that inserts Control Flow Guard checks.
LLVM_ABI FunctionPass *createCFGuardPass();

/// Return true if \p CB is a Control Flow Guard check or dispatch call.
/// @param CB Call to test for Control Flow Guard instrumentation.
/// @return True if \p CB is a CFGuard check or dispatch call.
LLVM_ABI bool isCFGuardCall(const CallBase *CB);

/// Return true if \p GV is a Control Flow Guard runtime function.
/// @param GV Global value to test for a Control Flow Guard guard function.
/// @return True if \p GV is `__guard_check_icall_fptr` or
/// `__guard_dispatch_icall_fptr`.
LLVM_ABI bool isCFGuardFunction(const GlobalValue *GV);

} // namespace llvm

#endif
