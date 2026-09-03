//===-- llvm/MC/MCSchedule.h - Scheduling -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the classes used to describe a subtarget's machine model
// for scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSCHEDULE_H
#define LLVM_MC_MCSCHEDULE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringTable.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <optional>

namespace llvm {

template <typename T> class ArrayRef;
struct InstrItinerary;
class MCSubtargetInfo;
class MCInstrInfo;
class MCInst;
class MCInstrDesc;
class InstrItineraryData;

namespace cl {
class OptionCategory;
}

/// Option category for machine-model scheduling command-line flags.
extern LLVM_ABI cl::OptionCategory MCScheduleOptions;

/// Define a kind of processor resource that will be modeled by the scheduler.
struct MCProcResourceDesc {
  /// Name of this processor resource kind.
  const char *Name;
  /// Number of resource of this kind.
  unsigned NumUnits;
  /// Index of the resources kind that contains this kind.
  unsigned SuperIdx;

  /// Number of resources that may be buffered.
  ///
  /// Buffered resources (BufferSize != 0) may be consumed at some indeterminate
  /// cycle after dispatch. This should be used for out-of-order cpus when
  /// instructions that use this resource can be buffered in a reservaton
  /// station.
  ///
  /// Unbuffered resources (BufferSize == 0) always consume their resource some
  /// fixed number of cycles after dispatch. If a resource is unbuffered, then
  /// the scheduler will avoid scheduling instructions with conflicting resources
  /// in the same cycle. This is for in-order cpus, or the in-order portion of
  /// an out-of-order cpus.
  int BufferSize;

  /// Pointer to the ProcResourceIdx array of sub-units, or nullptr.
  ///
  /// If the resource has sub-units, a pointer to the first element of an array
  /// of `NumUnits` elements containing the ProcResourceIdx of the sub units.
  /// nullptr if the resource does not have sub-units.
  const unsigned *SubUnitsIdxBegin;

  /// Return true if this resource description equals \p Other.
  ///
  /// \param Other The other resource description to compare against.
  /// \return True if the descriptions are equal.
  bool operator==(const MCProcResourceDesc &Other) const {
    return NumUnits == Other.NumUnits && SuperIdx == Other.SuperIdx
      && BufferSize == Other.BufferSize;
  }
};

/// Identify one of the processor resource kinds consumed by a
/// particular scheduling class for the specified number of cycles.
struct MCWriteProcResEntry {
  /// Index of the processor resource kind consumed by this write.
  uint16_t ProcResourceIdx;
  /// Cycle at which the resource will be released by an instruction,
  /// relatively to the cycle in which the instruction is issued
  /// (assuming no stalls inbetween).
  uint16_t ReleaseAtCycle;
  /// Cycle at which the resource will be aquired by an instruction,
  /// relatively to the cycle in which the instruction is issued
  /// (assuming no stalls inbetween).
  uint16_t AcquireAtCycle;

  /// Return true if this write-resource entry equals \p Other.
  ///
  /// \param Other The other write-resource entry to compare against.
  /// \return True if the entries are equal.
  bool operator==(const MCWriteProcResEntry &Other) const {
    return ProcResourceIdx == Other.ProcResourceIdx &&
           ReleaseAtCycle == Other.ReleaseAtCycle &&
           AcquireAtCycle == Other.AcquireAtCycle;
  }
};

/// Specify the latency in cpu cycles for a particular scheduling class and def.
///
/// -1 indicates an invalid latency. Heuristics would typically consider an
/// instruction with invalid latency to have infinite latency. Also identify the
/// WriteResources of this def. When the operand expands to a sequence of
/// writes, this ID is the last write in the sequence.
struct MCWriteLatencyEntry {
  /// Latency in CPU cycles for this def (-1 means invalid / infinite).
  int16_t Cycles;
  /// Write resource ID for this def (last write when expanded to a sequence).
  uint16_t WriteResourceID;

  /// Return true if this write-latency entry equals \p Other.
  ///
  /// \param Other The other write-latency entry to compare against.
  /// \return True if the entries are equal.
  bool operator==(const MCWriteLatencyEntry &Other) const {
    return Cycles == Other.Cycles && WriteResourceID == Other.WriteResourceID;
  }
};

/// Specify how many cycles after issue a use may read its registers early.
///
/// This effectively reduces the write's latency. Here we allow negative cycles
/// for corner cases where latency increases. This rule only applies when the
/// entry's WriteResource matches the write's WriteResource.
///
/// MCReadAdvanceEntries are sorted first by operand index (UseIdx), then by
/// WriteResourceIdx.
struct MCReadAdvanceEntry {
  /// Operand index of the use that may read early.
  unsigned UseIdx;
  /// Write resource ID that this advance applies to.
  unsigned WriteResourceID;
  /// Number of cycles of read advance (may be negative).
  int Cycles;

  /// Return true if this read-advance entry equals \p Other.
  ///
  /// \param Other The other read-advance entry to compare against.
  /// \return True if the entries are equal.
  bool operator==(const MCReadAdvanceEntry &Other) const {
    return UseIdx == Other.UseIdx && WriteResourceID == Other.WriteResourceID
      && Cycles == Other.Cycles;
  }
};

/// Summarize the scheduling resources required for an instruction of a
/// particular scheduling class.
///
/// Defined as an aggregate struct for creating tables with initializer lists.
struct MCSchedClassDesc {
  /// Sentinel NumMicroOps value marking an invalid scheduling class.
  static const unsigned short InvalidNumMicroOps = (1U << 13) - 1;
  /// Sentinel NumMicroOps value marking a variant scheduling class.
  static const unsigned short VariantNumMicroOps = InvalidNumMicroOps - 1;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Offset into the sched-class name string table.
  uint32_t NameOff;
#endif
  /// Number of micro-ops required by this scheduling class.
  uint16_t NumMicroOps : 13;
  /// True if this instruction begins an issue group.
  uint16_t BeginGroup : 1;
  /// True if this instruction ends an issue group.
  uint16_t EndGroup : 1;
  /// True if this instruction may retire out of order.
  uint16_t RetireOOO : 1;
  /// First index into ReadAdvanceTable.
  uint16_t ReadAdvanceIdx;
  /// First index into WriteProcResTable.
  uint16_t WriteProcResIdx;
  /// First index into WriteLatencyTable.
  uint16_t WriteLatencyIdx;
  /// Number of read-advance table entries for this class.
  uint16_t NumReadAdvanceEntries;
  /// Number of write-proc-res table entries for this class.
  uint8_t NumWriteProcResEntries;
  /// Number of write-latency table entries for this class.
  uint8_t NumWriteLatencyEntries;

  /// Return true if this scheduling class descriptor is valid.
  ///
  /// \return True if the descriptor is not the invalid sentinel.
  bool isValid() const {
    return NumMicroOps != InvalidNumMicroOps;
  }
  /// Return true if this scheduling class is variant and must be resolved.
  ///
  /// \return True if the descriptor is the variant sentinel.
  bool isVariant() const {
    return NumMicroOps == VariantNumMicroOps;
  }
};

// Guard against accidental growth. If either assertion fails, try to repack
// MCSchedClassDesc to preserve the compact layout; remove the assertion if the
// layout can no longer be kept.
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
static_assert(sizeof(MCSchedClassDesc) == 16);
#else
static_assert(sizeof(MCSchedClassDesc) == 12);
#endif

/// Specify the rename-stage cost of a register definition.
///
/// Cost is measured as the number of physical registers allocated at register
/// renaming. For example, AMD Jaguar natively supports 128-bit data types, and
/// operations on 256-bit registers (i.e. YMM registers) are internally split
/// into two COPs (complex operations) and each COP updates a physical register.
/// Basically, on Jaguar, a YMM register write effectively consumes two physical
/// registers. That means, the cost of a YMM write in the BtVer2 model is 2.
struct MCRegisterCostEntry {
  /// Register class ID this cost entry applies to.
  unsigned RegisterClassID;
  /// Number of physical registers allocated for a definition.
  unsigned Cost;
  /// True if move elimination is allowed for this register class.
  bool AllowMoveElimination;
};

/// A register file descriptor.
///
/// This struct allows to describe processor register files. In particular, it
/// helps describing the size of the register file, as well as the cost of
/// allocating a register file at register renaming stage.
/// FIXME: this struct can be extended to provide information about the number
/// of read/write ports to the register file.  A value of zero for field
/// 'NumPhysRegs' means: this register file has an unbounded number of physical
/// registers.
struct MCRegisterFileDesc {
  /// Name of this register file.
  const char *Name;
  /// Number of physical registers (zero means unbounded).
  uint16_t NumPhysRegs;
  /// Number of register cost entries for this file.
  uint16_t NumRegisterCostEntries;
  /// Index of the first cost entry in MCExtraProcessorInfo::RegisterCostTable.
  uint16_t RegisterCostEntryIdx;
  /// Max moves eliminated per cycle (zero means unlimited).
  uint16_t MaxMovesEliminatedPerCycle;
  /// True if only moves from known-zero registers can be eliminated.
  bool AllowZeroMoveEliminationOnly;
};

/// Provide extra details about the machine processor.
///
/// This is a collection of "optional" processor information that is not
/// normally used by the LLVM machine schedulers, but that can be consumed by
/// external tools like llvm-mca to improve the quality of the peformance
/// analysis.
struct MCExtraProcessorInfo {
  /// Actual size of the reorder buffer in hardware.
  unsigned ReorderBufferSize;
  /// Number of instructions retired per cycle.
  unsigned MaxRetirePerCycle;
  /// Table of register file descriptors for this processor.
  const MCRegisterFileDesc *RegisterFiles;
  /// Number of entries in \c RegisterFiles.
  unsigned NumRegisterFiles;
  /// Table of register cost entries referenced by the register files.
  const MCRegisterCostEntry *RegisterCostTable;
  /// Number of entries in \c RegisterCostTable.
  unsigned NumRegisterCostEntries;
  /// Processor resource ID of the load queue, if any.
  unsigned LoadQueueID;
  /// Processor resource ID of the store queue, if any.
  unsigned StoreQueueID;
};

/// Machine model for scheduling, bundling, and heuristics.
///
/// The machine model directly provides basic information about the
/// microarchitecture to the scheduler in the form of properties. It also
/// optionally refers to scheduler resource tables and itinerary
/// tables. Scheduler resource tables model the latency and cost for each
/// instruction type. Itinerary tables are an independent mechanism that
/// provides a detailed reservation table describing each cycle of instruction
/// execution. Subtargets may define any or all of the above categories of data
/// depending on the type of CPU and selected scheduler.
///
/// The machine independent properties defined here are used by the scheduler as
/// an abstract machine model. A real micro-architecture has a number of
/// buffers, queues, and stages. Declaring that a given machine-independent
/// abstract property corresponds to a specific physical property across all
/// subtargets can't be done. Nonetheless, the abstract model is
/// useful. Futhermore, subtargets typically extend this model with processor
/// specific resources to model any hardware features that can be exploited by
/// scheduling heuristics and aren't sufficiently represented in the abstract.
///
/// The abstract pipeline is built around the notion of an "issue point". This
/// is merely a reference point for counting machine cycles. The physical
/// machine will have pipeline stages that delay execution. The scheduler does
/// not model those delays because they are irrelevant as long as they are
/// consistent. Inaccuracies arise when instructions have different execution
/// delays relative to each other, in addition to their intrinsic latency. Those
/// special cases can be handled by TableGen constructs such as, ReadAdvance,
/// which reduces latency when reading data, and ReleaseAtCycles, which consumes
/// a processor resource when writing data for a number of abstract
/// cycles.
///
/// TODO: One tool currently missing is the ability to add a delay to
/// ReleaseAtCycles. That would be easy to add and would likely cover all cases
/// currently handled by the legacy itinerary tables.
///
/// A note on out-of-order execution and, more generally, instruction
/// buffers. Part of the CPU pipeline is always in-order. The issue point, which
/// is the point of reference for counting cycles, only makes sense as an
/// in-order part of the pipeline. Other parts of the pipeline are sometimes
/// falling behind and sometimes catching up. It's only interesting to model
/// those other, decoupled parts of the pipeline if they may be predictably
/// resource constrained in a way that the scheduler can exploit.
///
/// The LLVM machine model distinguishes between in-order constraints and
/// out-of-order constraints so that the target's scheduling strategy can apply
/// appropriate heuristics. For a well-balanced CPU pipeline, out-of-order
/// resources would not typically be treated as a hard scheduling
/// constraint. For example, in the GenericScheduler, a delay caused by limited
/// out-of-order resources is not directly reflected in the number of cycles
/// that the scheduler sees between issuing an instruction and its dependent
/// instructions. In other words, out-of-order resources don't directly increase
/// the latency between pairs of instructions. However, they can still be used
/// to detect potential bottlenecks across a sequence of instructions and bias
/// the scheduling heuristics appropriately.
struct MCSchedModel {
  /// Maximum number of instructions that may be scheduled in one cycle group.
  ///
  /// This is meant to be a hard in-order constraint (a.k.a. "hazard"). In the
  /// GenericScheduler strategy, no more than IssueWidth micro-ops can ever be
  /// scheduled in a particular cycle.
  ///
  /// In practice, IssueWidth is useful to model any bottleneck between the
  /// decoder (after micro-op expansion) and the out-of-order reservation
  /// stations or the decoder bandwidth itself. If the total number of
  /// reservation stations is also a bottleneck, or if any other pipeline stage
  /// has a bandwidth limitation, then that can be naturally modeled by adding an
  /// out-of-order processor resource.
  unsigned IssueWidth;
  /// Default value for \c IssueWidth.
  static constexpr unsigned DefaultIssueWidth = 1;

  /// Number of micro-ops the processor may buffer for out-of-order execution.
  ///
  /// "0" means operations that are not ready in this cycle are not considered
  /// for scheduling (they go in the pending queue). Latency is paramount. This
  /// may be more efficient if many instructions are pending in a schedule.
  ///
  /// "1" means all instructions are considered for scheduling regardless of
  /// whether they are ready in this cycle. Latency still causes issue stalls,
  /// but we balance those stalls against other heuristics.
  ///
  /// "> 1" means the processor is out-of-order. This is a machine independent
  /// estimate of highly machine specific characteristics such as the register
  /// renaming pool and reorder buffer.
  unsigned MicroOpBufferSize;
  /// Default value for \c MicroOpBufferSize.
  static constexpr unsigned DefaultMicroOpBufferSize = 0;

  /// Number of micro-ops the processor may buffer for optimized loop execution.
  ///
  /// More generally, this represents the optimal number of micro-ops in a loop
  /// body. A loop may be partially unrolled to bring the count of micro-ops in
  /// the loop body closer to this number.
  unsigned LoopMicroOpBufferSize;
  /// Default value for \c LoopMicroOpBufferSize.
  static constexpr unsigned DefaultLoopMicroOpBufferSize = 0;

  /// Expected latency of load instructions.
  unsigned LoadLatency;
  /// Default value for \c LoadLatency.
  static constexpr unsigned DefaultLoadLatency = 4;

  /// Expected latency of "very high latency" operations.
  ///
  /// See TargetInstrInfo::isHighLatencyDef(). By default, this is set to an
  /// arbitrarily high number of cycles likely to have some impact on scheduling
  /// heuristics.
  unsigned HighLatency;
  /// Default value for \c HighLatency.
  static constexpr unsigned DefaultHighLatency = 10;

  /// Typical extra cycles to recover from a branch misprediction.
  unsigned MispredictPenalty;
  /// Default value for \c MispredictPenalty.
  static constexpr unsigned DefaultMispredictPenalty = 10;

  /// True if post-RA scheduling should be enabled (default false).
  bool PostRAScheduler;

  /// True if this model has scheduling data for all instructions with a class.
  bool CompleteModel;

  /// Whether MachineScheduler should track resource usage with intervals.
  ///
  /// Uses ResourceSegments (see
  /// llvm/include/llvm/CodeGen/MachineScheduler.h).
  bool EnableIntervals;

  /// TableGen processor ID for this scheduling model.
  unsigned ProcID;
  /// Table of processor resource descriptors.
  const MCProcResourceDesc *ProcResourceTable;
  /// Table of scheduling class descriptors.
  const MCSchedClassDesc *SchedClassTable;
  /// Number of processor resource kinds in \c ProcResourceTable.
  unsigned NumProcResourceKinds;
  /// Number of scheduling classes in \c SchedClassTable.
  unsigned NumSchedClasses;
  /// String table of scheduling class names (debug/dump builds).
  const StringTable *SchedClassNames;
  friend class InstrItineraryData;
  /// Instruction itinerary tables used by InstrItineraryData.
  const InstrItinerary *InstrItineraries;

  /// Optional extra processor details for tools such as llvm-mca.
  const MCExtraProcessorInfo *ExtraProcessorInfo;

  /// Return true if extra processor info is available for this model.
  ///
  /// \return True if \c ExtraProcessorInfo is non-null.
  bool hasExtraProcessorInfo() const { return ExtraProcessorInfo; }

  /// Return the TableGen processor ID for this model.
  ///
  /// \return TableGen processor ID.
  unsigned getProcessorID() const { return ProcID; }

  /// Does this machine model include instruction-level scheduling.
  ///
  /// \return True if a scheduling class table is available.
  bool hasInstrSchedModel() const { return SchedClassTable; }

  /// Return the extra processor info for this model.
  ///
  /// \return Reference to the extra processor info.
  const MCExtraProcessorInfo &getExtraProcessorInfo() const {
    assert(hasExtraProcessorInfo() &&
           "No extra information available for this model");
    return *ExtraProcessorInfo;
  }

  /// Return true if this machine model data for all instructions with a
  /// scheduling class (itinerary class or SchedRW list).
  ///
  /// \return True if the model is complete for all scheduled instructions.
  bool isComplete() const { return CompleteModel; }

  /// Return true if machine supports out of order execution.
  ///
  /// \return True if the micro-op buffer size indicates out-of-order execution.
  bool isOutOfOrder() const { return MicroOpBufferSize > 1; }

  /// Return the number of processor resource kinds in this model.
  ///
  /// \return Number of processor resource kinds.
  unsigned getNumProcResourceKinds() const {
    return NumProcResourceKinds;
  }

  /// Return the processor resource descriptor at \p ProcResourceIdx.
  ///
  /// \param ProcResourceIdx Index into \c ProcResourceTable.
  /// \return Pointer to the processor resource descriptor.
  const MCProcResourceDesc *getProcResource(unsigned ProcResourceIdx) const {
    assert(hasInstrSchedModel() && "No scheduling machine model");

    assert(ProcResourceIdx < NumProcResourceKinds && "bad proc resource idx");
    return &ProcResourceTable[ProcResourceIdx];
  }

  /// Return the scheduling class descriptor at \p SchedClassIdx.
  ///
  /// \param SchedClassIdx Index into \c SchedClassTable.
  /// \return Pointer to the scheduling class descriptor.
  const MCSchedClassDesc *getSchedClassDesc(unsigned SchedClassIdx) const {
    assert(hasInstrSchedModel() && "No scheduling machine model");

    assert(SchedClassIdx < NumSchedClasses && "bad scheduling class idx");
    return &SchedClassTable[SchedClassIdx];
  }

  /// Return the name of the scheduling class at \p SchedClassIdx.
  ///
  /// \param SchedClassIdx Index into \c SchedClassTable.
  /// \return Name of the scheduling class, or `"<unknown>"` in non-debug
  ///         builds.
  StringRef getSchedClassName(unsigned SchedClassIdx) const {
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    return (*SchedClassNames)[SchedClassTable[SchedClassIdx].NameOff];
#else
    return "<unknown>";
#endif
  }

  /// Returns the latency value for the scheduling class.
  ///
  /// \param STI Subtarget info used to look up scheduling data.
  /// \param SCDesc Scheduling class whose latency is computed.
  /// \return Latency in cycles for the scheduling class.
  LLVM_ABI static int computeInstrLatency(const MCSubtargetInfo &STI,
                                          const MCSchedClassDesc &SCDesc);

  /// Return the latency for scheduling class \p SClass on \p STI.
  ///
  /// \param STI Subtarget info used to look up scheduling data.
  /// \param SClass Scheduling class index.
  /// \return Instruction latency in cycles for the scheduling class.
  LLVM_ABI int computeInstrLatency(const MCSubtargetInfo &STI,
                                   unsigned SClass) const;

  /// Return the latency for instruction \p Inst on \p STI.
  ///
  /// \param STI Subtarget info used to look up scheduling data.
  /// \param MCII Instruction info used to obtain the opcode descriptor.
  /// \param Inst Instruction whose latency is computed.
  /// \return Instruction latency in cycles.
  LLVM_ABI int computeInstrLatency(const MCSubtargetInfo &STI,
                                   const MCInstrInfo &MCII,
                                   const MCInst &Inst) const;

  /// Return the latency for \p Inst, resolving variant scheduling classes.
  ///
  /// \param STI Subtarget info used to look up scheduling data.
  /// \param MCII Instruction info used to obtain the opcode descriptor.
  /// \param Inst Instruction whose latency is computed.
  /// \param ResolveVariantSchedClass Callback that resolves a variant
  ///        scheduling class descriptor to a concrete one.
  /// \return Instruction latency in cycles, or -1 if no scheduling information
  ///         is available.
  template <typename MCSubtargetInfo, typename MCInstrInfo,
            typename InstrItineraryData, typename MCInstOrMachineInstr>
  int computeInstrLatency(
      const MCSubtargetInfo &STI, const MCInstrInfo &MCII,
      const MCInstOrMachineInstr &Inst,
      llvm::function_ref<const MCSchedClassDesc *(const MCSchedClassDesc *)>
          ResolveVariantSchedClass =
              [](const MCSchedClassDesc *SCDesc) { return SCDesc; }) const;

  /// Return the reciprocal throughput for scheduling class \p SCDesc.
  ///
  /// \param STI Subtarget info used to look up resource data.
  /// \param SCDesc Scheduling class whose throughput is computed.
  /// \return Reciprocal throughput for the scheduling class.
  LLVM_ABI static double
  getReciprocalThroughput(const MCSubtargetInfo &STI,
                          const MCSchedClassDesc &SCDesc);

  /// Return the reciprocal throughput for itinerary scheduling class \p SchedClass.
  ///
  /// \param SchedClass Scheduling class index in the itinerary.
  /// \param IID Itinerary data used to compute throughput.
  /// \return Reciprocal throughput for the itinerary scheduling class.
  LLVM_ABI static double getReciprocalThroughput(unsigned SchedClass,
                                                 const InstrItineraryData &IID);

  /// Return the reciprocal throughput for instruction \p Inst on \p STI.
  ///
  /// \param STI Subtarget info used to look up scheduling data.
  /// \param MCII Instruction info used to obtain the opcode descriptor.
  /// \param Inst Instruction whose throughput is computed.
  /// \return Reciprocal throughput for \p Inst.
  LLVM_ABI double getReciprocalThroughput(const MCSubtargetInfo &STI,
                                          const MCInstrInfo &MCII,
                                          const MCInst &Inst) const;

  /// Returns the maximum forwarding delay for register reads dependent on
  /// writes of scheduling class WriteResourceIdx.
  ///
  /// \param Entries Read-advance table entries to search.
  /// \param WriteResourceIdx Write resource whose forwarding delay is sought.
  /// \return Maximum forwarding delay in cycles for the given write resource.
  LLVM_ABI static unsigned
  getForwardingDelayCycles(ArrayRef<MCReadAdvanceEntry> Entries,
                           unsigned WriteResourceIdx = 0);

  /// Returns the bypass delay cycle for the maximum latency write cycle.
  ///
  /// \param STI Subtarget info used to look up scheduling data.
  /// \param SCDesc Scheduling class whose bypass delay is computed.
  /// \return Bypass delay in cycles for the maximum-latency write.
  LLVM_ABI static unsigned getBypassDelayCycles(const MCSubtargetInfo &STI,
                                                const MCSchedClassDesc &SCDesc);

  /// Return the buffer size of the resource. If a positive scale factor
  /// is provided and the original buffer size is > 1, the size is scaled
  /// accordingly.
  ///
  /// \param ProcResourceIdx Index of the processor resource whose buffer size
  ///        is returned.
  /// \return Buffer size of the resource (possibly scaled).
  LLVM_ABI int getResourceBufferSize(unsigned ProcResourceIdx) const;

  /// Returns the default initialized model.
  LLVM_ABI static const MCSchedModel Default;
};

// The first three are only template'd arguments so we can get away with leaving
// them as incomplete types below. The third is a template over
// MCInst/MachineInstr so as to avoid a layering violation here that would make
// the MC layer depend on CodeGen.
/// Return the latency for \p Inst, resolving variant scheduling classes.
///
/// \return Instruction latency in cycles, or -1 if no scheduling information
///         is available.
template <typename MCSubtargetInfo, typename MCInstrInfo,
          typename InstrItineraryData, typename MCInstOrMachineInstr>
int MCSchedModel::computeInstrLatency(
    const MCSubtargetInfo &STI, const MCInstrInfo &MCII,
    const MCInstOrMachineInstr &Inst,
    llvm::function_ref<const MCSchedClassDesc *(const MCSchedClassDesc *)>
        ResolveVariantSchedClass) const {
  static const int NoInformationAvailable = -1;
  // Check if we have a scheduling model for instructions.
  if (!hasInstrSchedModel()) {
    // Try to fall back to the itinerary model if the scheduling model doesn't
    // have a scheduling table.  Note the default does not have a table.

    llvm::StringRef CPU = STI.getCPU();

    // Check if we have a CPU to get the itinerary information.
    if (CPU.empty())
      return NoInformationAvailable;

    // Get itinerary information.
    InstrItineraryData IID = STI.getInstrItineraryForCPU(CPU);
    // Get the scheduling class of the requested instruction.
    const MCInstrDesc &Desc = MCII.get(Inst.getOpcode());
    unsigned SCClass = Desc.getSchedClass();

    unsigned Latency = 0;

    for (unsigned Idx = 0, IdxEnd = Inst.getNumOperands(); Idx != IdxEnd; ++Idx)
      if (std::optional<unsigned> OperCycle = IID.getOperandCycle(SCClass, Idx))
        Latency = std::max(Latency, *OperCycle);

    return int(Latency);
  }

  unsigned SchedClass = MCII.get(Inst.getOpcode()).getSchedClass();
  const MCSchedClassDesc *SCDesc = getSchedClassDesc(SchedClass);
  SCDesc = ResolveVariantSchedClass(SCDesc);

  if (!SCDesc || !SCDesc->isValid())
    return NoInformationAvailable;

  return MCSchedModel::computeInstrLatency(STI, *SCDesc);
}

} // namespace llvm

#endif
