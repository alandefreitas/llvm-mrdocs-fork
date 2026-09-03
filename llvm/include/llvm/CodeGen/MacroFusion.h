//===- MacroFusion.h - Macro Fusion -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file contains the definition of the DAG scheduling mutation to
/// pair instructions back to back.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACROFUSION_H
#define LLVM_CODEGEN_MACROFUSION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {

class MachineInstr;
class ScheduleDAGMutation;
class TargetInstrInfo;
class TargetSubtargetInfo;
class ScheduleDAGInstrs;
class SUnit;
class SDep;

/// Predicate type that decides whether two instructions should be fused.
///
/// Check if the instr pair, FirstMI and SecondMI, should be fused together,
/// based on the dependency between them, Dep. Given SecondMI, when FirstMI is
/// unspecified, then check if SecondMI may be part of a fused pair at all.
using MacroFusionPredTy = bool (*)(const TargetInstrInfo &TII,
                                   const TargetSubtargetInfo &STI,
                                   const MachineInstr *FirstMI,
                                   const MachineInstr &SecondMI,
                                   const SDep *Dep);

/// Checks if the number of cluster edges between SU and its predecessors is
/// less than FuseLimit.
///
/// \param SU Scheduling unit whose predecessor cluster chain is counted.
/// \param FuseLimit Maximum length of the fused cluster chain.
/// \return True if the predecessor cluster chain is shorter than FuseLimit.
LLVM_ABI bool hasLessThanNumFused(const SUnit &SU, unsigned FuseLimit);

/// Returns true if \p Dep is a non-null non-data dependency.
///
/// \param Dep Dependency to test, which may be null.
/// \return True if Dep is non-null and not a data dependency.
LLVM_ABI bool isNonDataDep(const SDep *Dep);

/// Fuse two scheduling units so they are scheduled back to back.
///
/// Create an artificial edge between FirstSU and SecondSU. Make data
/// dependencies from the FirstSU also dependent on the SecondSU to prevent
/// them from being scheduled between the FirstSU and the SecondSU and
/// vice-versa. Fusing more than 2 instructions is not currently supported.
///
/// \param DAG Scheduling DAG that owns the units being fused.
/// \param FirstSU First scheduling unit in the fused pair.
/// \param SecondSU Second scheduling unit in the fused pair.
/// \return True if the pair was fused; false if either unit was already
///         clustered.
LLVM_ABI bool fuseInstructionPair(ScheduleDAGInstrs &DAG, SUnit &FirstSU,
                                  SUnit &SecondSU);

/// Create a DAG mutation that fuses instruction pairs matching the given
/// predicates.
///
/// Create a DAG scheduling mutation to pair instructions back to back for
/// instructions that benefit according to the target-specific predicate
/// functions. shouldScheduleAdjacent will be true if any of the provided
/// predicates are true. If BranchOnly is true, only branch instructions with
/// one of their predecessors will be fused.
///
/// \param Predicates Target-specific predicates that decide which instruction
///        pairs should be fused.
/// \param BranchOnly If true, only fuse a branch with one of its predecessors.
/// \return A new ScheduleDAGMutation, or null if macro fusion is disabled.
LLVM_ABI std::unique_ptr<ScheduleDAGMutation>
createMacroFusionDAGMutation(ArrayRef<MacroFusionPredTy> Predicates,
                             bool BranchOnly = false);

} // end namespace llvm

#endif // LLVM_CODEGEN_MACROFUSION_H
