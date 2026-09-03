///===- DroppedVariableStats.h - Opt Diagnostics -*- C++ -*----------------===//
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

#ifndef LLVM_CODEGEN_DROPPEDVARIABLESTATS_H
#define LLVM_CODEGEN_DROPPEDVARIABLESTATS_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include <tuple>

namespace llvm {

class DIScope;
class DILocalVariable;
class Function;
class DILocation;
class DebugLoc;
class StringRef;

/// A unique key that represents a debug variable.
///
/// First const DIScope *: Represents the scope of the debug variable.
/// Second const DIScope *: Represents the InlinedAt scope of the debug
/// variable. const DILocalVariable *: It is a pointer to the debug variable
/// itself.
using VarID =
    std::tuple<const DIScope *, const DIScope *, const DILocalVariable *>;

/// A base class to collect and print dropped debug information variable
/// statistics.
class DroppedVariableStats {
public:
  /// Construct a DroppedVariableStats tracker.
  ///
  /// \param DroppedVarStatsEnabled When true, enable dropped-variable
  /// statistics and print the CSV header.
  LLVM_ABI DroppedVariableStats(bool DroppedVarStatsEnabled);

  /// Destroy a DroppedVariableStats tracker.
  virtual ~DroppedVariableStats() = default;

  /// Copy construction is deleted; this class is unique per compilation.
  /// \param Other Unused; copy construction is deleted.
  DroppedVariableStats(const DroppedVariableStats &Other) = delete;
  /// Copy assignment is deleted; this class is unique per compilation.
  /// \param Other Unused; copy assignment is deleted.
  void operator=(const DroppedVariableStats &Other) = delete;

  /// Return true if the most recent pass dropped any debug variables.
  /// \return True if the most recent pass dropped any debug variables.
  bool getPassDroppedVariables() { return PassDroppedVariables; }

protected:
  /// Push a new empty level onto the per-pass tracking stacks.
  LLVM_ABI void setup();

  /// Pop the most recent level from the per-pass tracking stacks.
  LLVM_ABI void cleanup();

  /// True when dropped-variable statistics collection is enabled.
  bool DroppedVariableStatsEnabled = false;
  /// Per-function debug variable sets recorded before and after a pass.
  struct DebugVariables {
    /// DenseSet of VarIDs before an optimization pass has run.
    DenseSet<VarID> DebugVariablesBefore;
    /// DenseSet of VarIDs after an optimization pass has run.
    DenseSet<VarID> DebugVariablesAfter;
  };

  /// A stack of a DenseMap, that maps DebugVariables for every pass to an
  /// llvm::Function. A stack is used because an optimization pass can call
  /// other passes.
  SmallVector<DenseMap<const Function *, DebugVariables>> DebugVariablesStack;

  /// A DenseSet tracking whether a scope was visited before.
  DenseSet<const DIScope *> VisitedScope;
  /// A stack of DenseMaps, which map the name of an llvm::Function to a
  /// DenseMap of VarIDs and their inlinedAt locations before an optimization
  /// pass has run.
  SmallVector<DenseMap<StringRef, DenseMap<VarID, DILocation *>>> InlinedAts;
  /// Calculate the number of dropped variables in an llvm::Function or
  /// llvm::MachineFunction and print the relevant information to stdout.
  ///
  /// \param DbgVariables The before/after debug variable sets for the function.
  /// \param FuncName The name of the function being analyzed.
  /// \param PassID The identifier of the optimization pass.
  /// \param FuncOrModName The function or module name printed in the report.
  /// \param PassLevel The pass level string printed in the report.
  /// \param Func The function whose dropped variables are being counted.
  LLVM_ABI void calculateDroppedStatsAndPrint(
      DebugVariables &DbgVariables, StringRef FuncName, StringRef PassID,
      StringRef FuncOrModName, StringRef PassLevel, const Function *Func);

  /// Check if a \p Var has been dropped or is a false positive. Also update the
  /// \p DroppedCount if a debug variable is dropped.
  ///
  /// \param DbgLoc The debug location of the instruction being considered.
  /// \param Scope The scope of the instruction being considered.
  /// \param DbgValScope The scope of the debug variable being checked.
  /// \param InlinedAtsMap Map from VarID to its inlinedAt location.
  /// \param Var The debug variable ID being checked.
  /// \param DroppedCount Counter incremented when a dropped variable is found.
  /// \return True if the variable was counted as dropped; false otherwise.
  LLVM_ABI bool updateDroppedCount(DILocation *DbgLoc, const DIScope *Scope,
                                   const DIScope *DbgValScope,
                                   DenseMap<VarID, DILocation *> &InlinedAtsMap,
                                   VarID Var, unsigned &DroppedCount);

  /// Run code to populate relevant data structures over an llvm::Function or
  /// llvm::MachineFunction.
  ///
  /// \param DbgVariables The before/after debug variable sets to populate.
  /// \param FuncName The name of the function being processed.
  /// \param Before True when populating the before-pass set; false for after.
  LLVM_ABI void run(DebugVariables &DbgVariables, StringRef FuncName,
                    bool Before);

  /// Populate the VarIDSet and InlinedAtMap with the relevant information
  /// needed for before and after pass analysis to determine dropped variable
  /// status.
  ///
  /// \param DbgVar The local debug variable being recorded.
  /// \param DbgLoc The debug location associated with \p DbgVar.
  /// \param VarIDSet The set of VarIDs to insert into.
  /// \param InlinedAtsMap Map from function name to VarID inlinedAt locations.
  /// \param FuncName The name of the function that owns \p DbgVar.
  /// \param Before True when recording state before the pass; false for after.
  LLVM_ABI void populateVarIDSetAndInlinedMap(
      const DILocalVariable *DbgVar, DebugLoc DbgLoc, DenseSet<VarID> &VarIDSet,
      DenseMap<StringRef, DenseMap<VarID, DILocation *>> &InlinedAtsMap,
      StringRef FuncName, bool Before);

  /// Visit every llvm::Instruction or llvm::MachineInstruction and check if the
  /// debug variable denoted by its ID \p Var may have been dropped by an
  /// optimization pass.
  ///
  /// \param DroppedCount Counter incremented when a dropped variable is found.
  /// \param InlinedAtsMap Map from VarID to its inlinedAt location.
  /// \param Var The debug variable ID being checked.
  virtual void
  visitEveryInstruction(unsigned &DroppedCount,
                        DenseMap<VarID, DILocation *> &InlinedAtsMap,
                        VarID Var) = 0;
  /// Visit every debug record in an llvm::Function or llvm::MachineFunction
  /// and call populateVarIDSetAndInlinedMap on it.
  ///
  /// \param VarIDSet The set of VarIDs to populate.
  /// \param InlinedAtsMap Map from function name to VarID inlinedAt locations.
  /// \param FuncName The name of the function being visited.
  /// \param Before True when recording state before the pass; false for after.
  virtual void visitEveryDebugRecord(
      DenseSet<VarID> &VarIDSet,
      DenseMap<StringRef, DenseMap<VarID, DILocation *>> &InlinedAtsMap,
      StringRef FuncName, bool Before) = 0;

private:
  /// Remove a dropped debug variable's VarID from all Sets in the
  /// DroppedVariablesBefore stack.
  void removeVarFromAllSets(VarID Var, const Function *F);

  /// Return true if \p Scope is the same as \p DbgValScope or a child scope of
  /// \p DbgValScope, return false otherwise.
  bool isScopeChildOfOrEqualTo(const DIScope *Scope,
                               const DIScope *DbgValScope);

  /// Return true if \p InlinedAt is the same as \p DbgValInlinedAt or part of
  /// the InlinedAt chain, return false otherwise.
  bool isInlinedAtChildOfOrEqualTo(const DILocation *InlinedAt,
                                   const DILocation *DbgValInlinedAt);

  bool PassDroppedVariables = false;
};

} // namespace llvm

#endif
