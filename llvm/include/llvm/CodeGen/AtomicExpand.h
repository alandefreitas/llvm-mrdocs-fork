//===-- AtomicExpand.h - Expand Atomic Instructions -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ATOMICEXPAND_H
#define LLVM_CODEGEN_ATOMICEXPAND_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;
class TargetMachine;

/// New PM pass that expands atomic instructions for the target.
///
/// Replaces atomic IR with `__atomic_*` library calls or target-specific
/// forms (such as LL/SC loops, cmpxchg, or type coercions) that better fit
/// the backend.
class AtomicExpandPass : public RequiredPassInfoMixin<AtomicExpandPass> {
private:
  const TargetMachine *TM;

public:
  /// Construct a pass using target information from \p TM.
  /// \param TM Target machine used to decide how atomics are expanded.
  AtomicExpandPass(const TargetMachine &TM) : TM(&TM) {}
  /// Expand atomic instructions in \p F for the configured target.
  /// \param F Function whose atomic instructions are expanded.
  /// \param AM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_ATOMICEXPAND_H
