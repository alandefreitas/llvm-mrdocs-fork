//===- MachineSink.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINESINK_H
#define LLVM_CODEGEN_MACHINESINK_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that sinks machine instructions into successor blocks.
///
/// Moves instructions deeper into the CFG when profitable, typically to
/// reduce register pressure or avoid executing them on cold paths.
class MachineSinkingPass : public OptionalPassInfoMixin<MachineSinkingPass> {
  bool EnableSinkAndFold;

public:
  /// Construct a MachineSinking pass.
  /// \param EnableSinkAndFold Whether to also sink and fold copies.
  MachineSinkingPass(bool EnableSinkAndFold = false)
      : EnableSinkAndFold(EnableSinkAndFold) {}

  /// Sink machine instructions in \p MF.
  /// \param MF Machine function whose instructions are sunk.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after sinking machine instructions.
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
#endif // LLVM_CODEGEN_MACHINESINK_H
