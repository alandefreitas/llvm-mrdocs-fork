//===- PGOForceFunctionAttrs.h - --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_PGOFORCEFUNCTIONATTRS_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_PGOFORCEFUNCTIONATTRS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/PGOOptions.h"

namespace llvm {

/// Pass that applies size or optnone attributes to cold functions based on PGO.
struct PGOForceFunctionAttrsPass
    : public OptionalPassInfoMixin<PGOForceFunctionAttrsPass> {
  /// Construct a pass that attributes cold functions with \p ColdType.
  /// @param ColdType How cold functions should be optimized (optsize, minsize,
  /// or optnone).
  PGOForceFunctionAttrsPass(PGOOptions::ColdFuncOpt ColdType)
      : ColdType(ColdType) {}
  /// Apply cold-function attributes to functions in \p M according to profile
  /// data.
  /// @param M Module whose functions may receive cold attributes.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  PGOOptions::ColdFuncOpt ColdType;
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_PGOFORCEFUNCTIONATTRS_H
