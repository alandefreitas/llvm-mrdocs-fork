//===- llvm/CodeGen/TargetSchedule.h - Sched Machine Model ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a wrapper around MCSchedModel that allows the interface to
// benefit from information currently only available in TargetInstrInfo.
// Ideally, the scheduling interface would be fully defined in the MC layer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETSCHEDULE_H
#define LLVM_CODEGEN_TARGETSCHEDULE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class MachineInstr;
class TargetInstrInfo;

/// Provide an instruction scheduling machine model to CodeGen passes.
class TargetSchedModel {
  // For efficiency, hold a copy of the statically defined MCSchedModel for this
  // processor.
  MCSchedModel SchedModel;
  InstrItineraryData InstrItins;
  const TargetSubtargetInfo *STI = nullptr;
  const TargetInstrInfo *TII = nullptr;

  SmallVector<unsigned, 16> ResourceFactors;

  // Multiply to normalize microops to resource units.
  unsigned MicroOpFactor = 0;

  // Resource units per cycle. Latency normalization factor.
  unsigned ResourceLCM = 0;

  unsigned computeInstrLatency(const MCSchedClassDesc &SCDesc) const;

  // EnableSchedModel and EnableSchedItins are used to control whether or not to
  // use the Target's {SchedMachineModel, InstrItins} for hardware infor based
  // Scheduling decisions. If both are enabled, as is the default, preference
  // will be given to one based on the API implementation. By disabling one, we
  // can force preference of the other. By disabling both, we will throw away
  // any target specific hardware details for scheduling decisions, and fall
  // into things that provide generic info such as defaultDefLatency.
  bool EnableSchedModel = true;
  bool EnableSchedItins = true;

public:
  /// Construct a target schedule model with the default MCSchedModel.
  TargetSchedModel() : SchedModel(MCSchedModel::Default) {}

  /// Initialize the machine model for instruction scheduling.
  ///
  /// The machine model API keeps a copy of the top-level MCSchedModel table
  /// indices and may query TargetSubtargetInfo and TargetInstrInfo to resolve
  /// dynamic properties.
  /// \param TSInfo Subtarget providing the schedule model and instruction info.
  /// \param EnableSModel Whether to enable the instruction scheduling model.
  /// \param EnableSItins Whether to enable cycle-to-cycle itinerary data.
  LLVM_ABI void init(const TargetSubtargetInfo *TSInfo,
                     bool EnableSModel = true, bool EnableSItins = true);

  /// Return the MCSchedClassDesc for this instruction.
  /// \param MI Instruction whose scheduling class is resolved.
  /// @return The scheduling class descriptor for \p MI.
  LLVM_ABI const MCSchedClassDesc *
  resolveSchedClass(const MachineInstr *MI) const;

  /// TargetSubtargetInfo getter.
  /// @return The subtarget info used to initialize this model.
  const TargetSubtargetInfo *getSubtargetInfo() const { return STI; }

  /// TargetInstrInfo getter.
  /// @return The target instruction info used to initialize this model.
  const TargetInstrInfo *getInstrInfo() const { return TII; }

  /// Return true if this machine model includes an instruction-level
  /// scheduling model.
  ///
  /// This is more detailed than the course grain IssueWidth and default
  /// latency properties, but separate from the per-cycle itinerary data.
  /// @return True if an instruction-level scheduling model is present.
  LLVM_ABI bool hasInstrSchedModel() const;

  /// Return the underlying MCSchedModel for this processor.
  /// @return Pointer to the underlying MCSchedModel.
  const MCSchedModel *getMCSchedModel() const { return &SchedModel; }

  /// Return true if this machine model includes cycle-to-cycle itinerary
  /// data.
  ///
  /// This models scheduling at each stage in the processor pipeline.
  /// @return True if cycle-to-cycle itinerary data is present.
  LLVM_ABI bool hasInstrItineraries() const;

  /// Return itinerary data if this model has itineraries, otherwise nullptr.
  /// @return Pointer to itinerary data, or nullptr if none.
  const InstrItineraryData *getInstrItineraries() const {
    if (hasInstrItineraries())
      return &InstrItins;
    return nullptr;
  }

  /// Return true if this machine model includes an instruction-level
  /// scheduling model or cycle-to-cycle itinerary data.
  /// @return True if either a sched model or itineraries are present.
  bool hasInstrSchedModelOrItineraries() const {
    return hasInstrSchedModel() || hasInstrItineraries();
  }
  /// Return true if resource interval tracking is enabled for this model.
  /// @return True if resource interval tracking is enabled.
  LLVM_ABI bool enableIntervals() const;
  /// Identify the processor corresponding to the current subtarget.
  /// @return Processor ID for the current subtarget.
  unsigned getProcessorID() const { return SchedModel.getProcessorID(); }

  /// Maximum number of micro-ops that may be scheduled per cycle.
  /// @return Maximum number of micro-ops schedulable per cycle.
  unsigned getIssueWidth() const { return SchedModel.IssueWidth; }

  /// Return true if new group must begin.
  /// \param MI Instruction being scheduled.
  /// \param SC Optional precomputed scheduling class for \p MI.
  /// @return True if a new scheduling group must begin for \p MI.
  LLVM_ABI bool mustBeginGroup(const MachineInstr *MI,
                               const MCSchedClassDesc *SC = nullptr) const;
  /// Return true if current group must end.
  /// \param MI Instruction being scheduled.
  /// \param SC Optional precomputed scheduling class for \p MI.
  /// @return True if the current scheduling group must end for \p MI.
  LLVM_ABI bool mustEndGroup(const MachineInstr *MI,
                             const MCSchedClassDesc *SC = nullptr) const;

  /// Return the number of issue slots required for this MI.
  /// \param MI Instruction whose micro-op count is requested.
  /// \param SC Optional precomputed scheduling class for \p MI.
  /// @return Number of issue slots required for \p MI.
  LLVM_ABI unsigned getNumMicroOps(const MachineInstr *MI,
                                   const MCSchedClassDesc *SC = nullptr) const;

  /// Get the number of kinds of resources for this target.
  /// @return Number of processor resource kinds for this target.
  unsigned getNumProcResourceKinds() const {
    return SchedModel.getNumProcResourceKinds();
  }

  /// Get a processor resource by ID for convenience.
  /// \param PIdx Index of the processor resource kind.
  /// @return Descriptor for the processor resource at \p PIdx.
  const MCProcResourceDesc *getProcResource(unsigned PIdx) const {
    return SchedModel.getProcResource(PIdx);
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Return the name of the processor resource with the given index.
  /// \param PIdx Index of the processor resource, or zero for micro-ops.
  /// @return Name of the resource at \p PIdx, or "MOps" when \p PIdx is zero.
  const char *getResourceName(unsigned PIdx) const {
    if (!PIdx)
      return "MOps";
    return SchedModel.getProcResource(PIdx)->Name;
  }
#endif

  /// Iterator over processor resources written by a scheduling class.
  using ProcResIter = const MCWriteProcResEntry *;

  /// Get an iterator to the first processor resource consumed by this
  /// scheduling class.
  /// \param SC Scheduling class whose write resources are iterated.
  /// @return Iterator to the first write-proc-resource entry for \p SC.
  ProcResIter getWriteProcResBegin(const MCSchedClassDesc *SC) const {
    // The subtarget holds a single resource table for all processors.
    return STI->getWriteProcResBegin(SC);
  }
  /// Get an iterator past the last processor resource consumed by this
  /// scheduling class.
  /// \param SC Scheduling class whose write resources are iterated.
  /// @return Iterator past the last write-proc-resource entry for \p SC.
  ProcResIter getWriteProcResEnd(const MCSchedClassDesc *SC) const {
    return STI->getWriteProcResEnd(SC);
  }

  /// Multiply the number of units consumed for a resource by this factor
  /// to normalize it relative to other resources.
  /// \param ResIdx Index of the processor resource.
  /// @return Normalization factor for the resource at \p ResIdx.
  unsigned getResourceFactor(unsigned ResIdx) const {
    return ResourceFactors[ResIdx];
  }

  /// Multiply number of micro-ops by this factor to normalize it
  /// relative to other resources.
  /// @return Normalization factor for micro-ops relative to other resources.
  unsigned getMicroOpFactor() const {
    return MicroOpFactor;
  }

  /// Multiply cycle count by this factor to normalize it relative to
  /// other resources. This is the number of resource units per cycle.
  /// @return Latency normalization factor (resource units per cycle).
  unsigned getLatencyFactor() const {
    return ResourceLCM;
  }

  /// Number of micro-ops that may be buffered for OOO execution.
  /// @return Number of micro-ops that may be buffered for out-of-order execution.
  unsigned getMicroOpBufferSize() const { return SchedModel.MicroOpBufferSize; }

  /// Return the original buffer size for a processor resource.
  /// \param PIdx Index of the processor resource.
  /// @return Buffer size for the processor resource at \p PIdx.
  int getResourceBufferSize(unsigned PIdx) const {
    return SchedModel.getProcResource(PIdx)->BufferSize;
  }

  /// Compute operand latency based on the available machine model.
  ///
  /// Compute and return the latency of the given data dependent def and use
  /// when the operand indices are already known. UseMI may be NULL for an
  /// unknown user.
  /// \param DefMI Instruction that defines the value.
  /// \param DefOperIdx Operand index of the definition in \p DefMI.
  /// \param UseMI Instruction that uses the value, or nullptr if unknown.
  /// \param UseOperIdx Operand index of the use in \p UseMI.
  /// @return Latency in cycles from the definition to the use.
  LLVM_ABI unsigned computeOperandLatency(const MachineInstr *DefMI,
                                          unsigned DefOperIdx,
                                          const MachineInstr *UseMI,
                                          unsigned UseOperIdx) const;

  /// Compute the instruction latency based on the available machine
  /// model.
  ///
  /// Compute and return the expected latency of this instruction independent of
  /// a particular use. computeOperandLatency is the preferred API, but this is
  /// occasionally useful to help estimate instruction cost.
  ///
  /// If UseDefaultDefLatency is false and no new machine sched model is
  /// present this method falls back to TII->getInstrLatency with an empty
  /// instruction itinerary (this is so we preserve the previous behavior of the
  /// if converter after moving it to TargetSchedModel).
  /// \param MI Instruction whose latency is computed.
  /// \param UseDefaultDefLatency Whether to fall back to default def latency
  /// when no machine sched model is present.
  /// @return Expected latency of \p MI in cycles.
  LLVM_ABI unsigned computeInstrLatency(const MachineInstr *MI,
                                        bool UseDefaultDefLatency = true) const;
  /// Compute the instruction latency for an MCInst based on the machine model.
  /// \param Inst Instruction whose latency is computed.
  /// @return Expected latency of \p Inst in cycles.
  LLVM_ABI unsigned computeInstrLatency(const MCInst &Inst) const;
  /// Compute the instruction latency for an opcode based on the machine model.
  /// \param Opcode Opcode whose latency is computed.
  /// @return Expected latency of \p Opcode in cycles.
  LLVM_ABI unsigned computeInstrLatency(unsigned Opcode) const;

  /// Output dependency latency of a pair of defs of the same register.
  ///
  /// This is typically one cycle.
  /// \param DefMI First instruction that defines the register.
  /// \param DefOperIdx Operand index of the definition in \p DefMI.
  /// \param DepMI Dependent instruction that also defines the same register.
  /// @return Output dependency latency in cycles between the two definitions.
  LLVM_ABI unsigned computeOutputLatency(const MachineInstr *DefMI,
                                         unsigned DefOperIdx,
                                         const MachineInstr *DepMI) const;

  /// Compute the reciprocal throughput of the given instruction.
  /// \param MI Instruction whose reciprocal throughput is computed.
  /// @return Reciprocal throughput of \p MI.
  LLVM_ABI double computeReciprocalThroughput(const MachineInstr *MI) const;
  /// Compute the reciprocal throughput of the given MCInst.
  /// \param MI Instruction whose reciprocal throughput is computed.
  /// @return Reciprocal throughput of \p MI.
  LLVM_ABI double computeReciprocalThroughput(const MCInst &MI) const;
  /// Compute the reciprocal throughput of the given opcode.
  /// \param Opcode Opcode whose reciprocal throughput is computed.
  /// @return Reciprocal throughput of \p Opcode.
  LLVM_ABI double computeReciprocalThroughput(unsigned Opcode) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETSCHEDULE_H
