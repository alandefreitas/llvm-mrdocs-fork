//===- EntryExitInstrumenter.h - Function Entry/Exit Instrumentation ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// EntryExitInstrumenter pass - Instrument function entry/exit with calls to
// mcount(), @__cyg_profile_func_{enter,exit} and the like. There are two
// variants, intended to run pre- and post-inlining, respectively.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_ENTRYEXITINSTRUMENTER_H
#define LLVM_TRANSFORMS_UTILS_ENTRYEXITINSTRUMENTER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass that instruments function entry and exit with profiling hooks.
///
/// Inserts calls to mcount(), @__cyg_profile_func_{enter,exit}, and similar
/// functions. There are two variants, intended to run pre- and post-inlining,
/// respectively.
struct EntryExitInstrumenterPass
    : public RequiredPassInfoMixin<EntryExitInstrumenterPass> {
  /// Construct an entry/exit instrumenter pass.
  /// @param PostInlining When true, run the post-inlining variant of the pass.
  EntryExitInstrumenterPass(bool PostInlining) : PostInlining(PostInlining) {}

  /// Run entry/exit instrumentation over the function.
  /// @param F Function to instrument.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

  /// Whether this pass runs after inlining.
  bool PostInlining;
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_ENTRYEXITINSTRUMENTER_H
