//===- StripNonLineTableDebugInfo.h - -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_STRIPNONLINETABLEDEBUGINFO_H
#define LLVM_TRANSFORMS_UTILS_STRIPNONLINETABLEDEBUGINFO_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Pass that strips debug info down to line-table information only.
///
/// Downgrades module debug info to what -gline-tables-only would have created,
/// deleting debug intrinsics and non-line-table metadata while preserving
/// line-table locations.
class StripNonLineTableDebugInfoPass
    : public OptionalPassInfoMixin<StripNonLineTableDebugInfoPass> {
public:
  /// Run the strip-non-line-table-debug-info pass over the module.
  /// @param M Module whose debug info is reduced to line tables.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_STRIPNONLINETABLEDEBUGINFO_H
