//===- llvm/CodeGen/LiveDebugValuesPass.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVEDEBUGVALUESPASS_H
#define LLVM_CODEGEN_LIVEDEBUGVALUESPASS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that extends variable debug locations across basic blocks.
///
/// Propagates DBG_VALUE ranges into successor blocks and other code
/// locations where those variable locations remain valid.
class LiveDebugValuesPass : public RequiredPassInfoMixin<LiveDebugValuesPass> {
  const bool ShouldEmitDebugEntryValues;

public:
  /// Construct a LiveDebugValues pass.
  /// \param ShouldEmitDebugEntryValues Whether to emit debug entry values.
  LiveDebugValuesPass(bool ShouldEmitDebugEntryValues)
      : ShouldEmitDebugEntryValues(ShouldEmitDebugEntryValues) {}

  /// Extend live debug values in \p MF.
  /// \param MF Machine function whose DBG_VALUE ranges are extended.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after extending live debug values.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Print this pass and its options as a pipeline string.
  /// \param OS Stream to write the pipeline string to.
  /// \param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);
};

} // namespace llvm

#endif // LLVM_CODEGEN_LIVEDEBUGVALUESPASS_H
