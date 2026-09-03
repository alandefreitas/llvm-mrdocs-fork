//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_STATICDATASPLITTER_H
#define LLVM_CODEGEN_STATICDATASPLITTER_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// New PM pass that categorizes static data hotness using profile information.
///
/// Uses branch profile data to assign hotness-based section qualifiers for
/// jump tables, module-internal global variables, and constant pools.
class StaticDataSplitterPass
    : public OptionalPassInfoMixin<StaticDataSplitterPass> {
public:
  /// Categorize static data hotness in \p MF using profile information.
  /// \param MF Machine function whose static data is partitioned.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after categorizing static data.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_STATICDATASPLITTER_H
