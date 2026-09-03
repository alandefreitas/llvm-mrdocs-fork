//===- llvm/CodeGen/SchedulerRegistry.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation for instruction scheduler function
// pass registry (RegisterScheduler).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULERREGISTRY_H
#define LLVM_CODEGEN_SCHEDULERREGISTRY_H

#include "llvm/CodeGen/MachinePassRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

//===----------------------------------------------------------------------===//
///
/// RegisterScheduler class - Track the registration of instruction schedulers.
///
//===----------------------------------------------------------------------===//

class ScheduleDAGSDNodes;
class SelectionDAGISel;

/// Registry node that tracks registration of SelectionDAG instruction
/// schedulers.
class RegisterScheduler
    : public MachinePassRegistryNode<ScheduleDAGSDNodes *(*)(SelectionDAGISel *,
                                                             CodeGenOptLevel)> {
public:
  /// Constructor type that builds a ScheduleDAGSDNodes from ISel and opt level.
  using FunctionPassCtor = ScheduleDAGSDNodes *(*)(SelectionDAGISel *,
                                                   CodeGenOptLevel);

  /// Global registry of SelectionDAG instruction scheduler constructors.
  LLVM_ABI static MachinePassRegistry<FunctionPassCtor> Registry;

  /// Register a scheduler named \p N with description \p D and ctor \p C.
  ///
  /// \param N Command-line name for this scheduler.
  /// \param D Human-readable description of this scheduler.
  /// \param C Factory that constructs the ScheduleDAGSDNodes.
  RegisterScheduler(const char *N, const char *D, FunctionPassCtor C)
      : MachinePassRegistryNode(N, D, C) {
    Registry.Add(this);
  }
  /// Remove this scheduler from the global registry.
  ~RegisterScheduler() { Registry.Remove(this); }


  // Accessors.
  /// Return the next registry entry in the linked list.
  ///
  /// \return The next RegisterScheduler, or null if this is the last entry.
  RegisterScheduler *getNext() const {
    return (RegisterScheduler *)MachinePassRegistryNode::getNext();
  }

  /// Return the head of the global SelectionDAG scheduler registry list.
  ///
  /// \return The first RegisterScheduler in the registry, or null if empty.
  static RegisterScheduler *getList() {
    return (RegisterScheduler *)Registry.getList();
  }

  /// Install \p L as the listener notified when registry entries change.
  ///
  /// \param L Listener invoked when constructors are added or removed.
  static void setListener(MachinePassRegistryListener<FunctionPassCtor> *L) {
    Registry.setListener(L);
  }
};

/// Create a bottom-up register usage reduction list scheduler.
///
/// \param IS Instruction selection pass owning the DAG to schedule.
/// \param OptLevel Codegen optimization level.
/// \return A new ScheduleDAGSDNodes that schedules for register usage reduction.
LLVM_ABI ScheduleDAGSDNodes *
createBURRListDAGScheduler(SelectionDAGISel *IS, CodeGenOptLevel OptLevel);

/// Create a bottom-up list scheduler that prefers source code order.
///
/// \param IS Instruction selection pass owning the DAG to schedule.
/// \param OptLevel Codegen optimization level.
/// \return A new ScheduleDAGSDNodes that prefers source-order scheduling.
LLVM_ABI ScheduleDAGSDNodes *
createSourceListDAGScheduler(SelectionDAGISel *IS, CodeGenOptLevel OptLevel);

/// Create a bottom-up register-pressure-aware list scheduler that uses latency.
///
/// In low register pressure mode, latency information is used to avoid stalls
/// for long latency instructions. In high register pressure mode it schedules
/// to reduce register pressure.
///
/// \param IS Instruction selection pass owning the DAG to schedule.
/// \param OptLevel Codegen optimization level.
/// \return A new ScheduleDAGSDNodes using the hybrid latency/pressure heuristic.
LLVM_ABI ScheduleDAGSDNodes *
createHybridListDAGScheduler(SelectionDAGISel *IS, CodeGenOptLevel OptLevel);

/// Create a bottom-up register-pressure-aware list scheduler that favors ILP.
///
/// In low register pressure mode, it tries to increase instruction level
/// parallelism. In high register pressure mode it schedules to reduce register
/// pressure.
///
/// \param IS Instruction selection pass owning the DAG to schedule.
/// \param OptLevel Codegen optimization level.
/// \return A new ScheduleDAGSDNodes that favors ILP under low register pressure.
LLVM_ABI ScheduleDAGSDNodes *
createILPListDAGScheduler(SelectionDAGISel *IS, CodeGenOptLevel OptLevel);

/// Create a fast SelectionDAG instruction scheduler.
///
/// \param IS Instruction selection pass owning the DAG to schedule.
/// \param OptLevel Codegen optimization level.
/// \return A new ScheduleDAGSDNodes using the fast scheduling algorithm.
LLVM_ABI ScheduleDAGSDNodes *createFastDAGScheduler(SelectionDAGISel *IS,
                                                    CodeGenOptLevel OptLevel);

/// Create a top-down DFA-driven list scheduler for VLIW targets.
///
/// Uses a clustering heuristic to control register pressure.
///
/// \param IS Instruction selection pass owning the DAG to schedule.
/// \param OptLevel Codegen optimization level.
/// \return A new ScheduleDAGSDNodes for VLIW DFA-driven list scheduling.
LLVM_ABI ScheduleDAGSDNodes *createVLIWDAGScheduler(SelectionDAGISel *IS,
                                                    CodeGenOptLevel OptLevel);

/// Create an instruction scheduler appropriate for the target.
///
/// \param IS Instruction selection pass owning the DAG to schedule.
/// \param OptLevel Codegen optimization level.
/// \return A new ScheduleDAGSDNodes chosen for the current target.
LLVM_ABI ScheduleDAGSDNodes *createDefaultScheduler(SelectionDAGISel *IS,
                                                    CodeGenOptLevel OptLevel);

/// Create a no-scheduling scheduler that linearizes the DAG topologically.
///
/// \param IS Instruction selection pass owning the DAG to schedule.
/// \param OptLevel Codegen optimization level.
/// \return A new ScheduleDAGSDNodes that only linearizes the DAG.
LLVM_ABI ScheduleDAGSDNodes *createDAGLinearizer(SelectionDAGISel *IS,
                                                 CodeGenOptLevel OptLevel);

} // end namespace llvm

#endif // LLVM_CODEGEN_SCHEDULERREGISTRY_H
