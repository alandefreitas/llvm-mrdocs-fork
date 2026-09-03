///===- DroppedVariableStatsIR.h - Opt Diagnostics -*- C++ -*--------------===//
///
/// Part of the LLVM Project, under the Apache License v2.0 with LLVM
/// Exceptions. See https://llvm.org/LICENSE.txt for license information.
/// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
///
///===---------------------------------------------------------------------===//
/// \file
/// Dropped Variable Statistics for Debug Information. Reports any number
/// of #dbg_value that get dropped due to an optimization pass.
///
///===---------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DROPPEDVARIABLESTATSIR_H
#define LLVM_CODEGEN_DROPPEDVARIABLESTATSIR_H

#include "llvm/IR/DroppedVariableStats.h"
#include "llvm/IR/IRUnitRef.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class StringRef;
class PassInstrumentationCallbacks;
class Function;
class Module;
class DILocation;

/// Collects and prints debug values dropped by LLVM IR optimization passes.
///
/// After every LLVM IR pass is run, it will print how many #dbg_values were
/// dropped due to that pass.
class LLVM_ABI DroppedVariableStatsIR : public DroppedVariableStats {
public:
  /// Construct dropped-variable stats for LLVM IR passes.
  /// \param DroppedVarStatsEnabled When true, collect and print dropped stats.
  DroppedVariableStatsIR(bool DroppedVarStatsEnabled)
      : llvm::DroppedVariableStats(DroppedVarStatsEnabled) {}

  /// Record debug variables in \p IR before pass \p P runs.
  /// \param P Identifier of the pass about to run.
  /// \param IR Function or Module the pass will run on.
  void runBeforePass(StringRef P, IRUnitRef IR);

  /// Compute and print debug variables dropped from \p IR by pass \p P.
  /// \param P Identifier of the pass that just ran.
  /// \param IR Function or Module the pass ran on.
  void runAfterPass(StringRef P, IRUnitRef IR);

  /// Register before/after pass callbacks on \p PIC.
  /// \param PIC Pass instrumentation callbacks to register with.
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

private:
  const Function *Func;

  void runAfterPassFunction(StringRef PassID, const Function *F);

  void runAfterPassModule(StringRef PassID, const Module *M);

  /// Populate DebugVariablesBefore, DebugVariablesAfter, InlinedAts before or
  /// after a pass has run to facilitate dropped variable calculation for an
  /// llvm::Function.
  void runOnFunction(StringRef PassID, const Function *F, bool Before);

  /// Iterate over all Instructions in a Function and report any dropped debug
  /// information.
  void calculateDroppedVarStatsOnFunction(const Function *F, StringRef PassID,
                                          StringRef FuncOrModName,
                                          StringRef PassLevel);

  /// Populate DebugVariablesBefore, DebugVariablesAfter, InlinedAts before or
  /// after a pass has run to facilitate dropped variable calculation for an
  /// llvm::Module. Calls runOnFunction on every Function in the Module.
  void runOnModule(StringRef PassID, const Module *M, bool Before);

  /// Iterate over all Functions in a Module and report any dropped debug
  /// information. Will call calculateDroppedVarStatsOnFunction on every
  /// Function.
  void calculateDroppedVarStatsOnModule(const Module *M, StringRef PassID,
                                        StringRef FuncOrModName,
                                        StringRef PassLevel);

  /// Override base class method to run on an llvm::Function specifically.
  void visitEveryInstruction(unsigned &DroppedCount,
                             DenseMap<VarID, DILocation *> &InlinedAtsMap,
                             VarID Var) override;

  /// Override base class method to run on #dbg_values specifically.
  void visitEveryDebugRecord(
      DenseSet<VarID> &VarIDSet,
      DenseMap<StringRef, DenseMap<VarID, DILocation *>> &InlinedAtsMap,
      StringRef FuncName, bool Before) override;
};

} // namespace llvm

#endif
