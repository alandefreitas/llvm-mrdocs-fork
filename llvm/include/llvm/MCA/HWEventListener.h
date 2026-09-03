//===----------------------- HWEventListener.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the main interface for hardware event listeners.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_HWEVENTLISTENER_H
#define LLVM_MCA_HWEVENTLISTENER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/MCA/Instruction.h"
#include "llvm/MCA/Support.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace mca {

/// Describes a state change for an instruction that listeners may observe.
///
/// Listeners can choose to ignore any event they are not interested in.
class HWInstructionEvent {
public:
  /// Event types shared by all targets and manipulated by generic MCA code.
  ///
  /// Generic subtarget-agnostic classes (e.g., Pipeline, HWInstructionEvent)
  /// and generic Views can manipulate these values. Subtargets are free to
  /// define additional event types that generic components treat as opaque
  /// values, but that can still be emitted by subtarget-specific pipeline
  /// stages (e.g., ExecuteStage, DispatchStage) and interpreted by
  /// subtarget-specific EventListener implementations.
  enum GenericEventType {
    /// Sentinel for an uninitialized or unknown event type.
    Invalid = 0,
    /// Instruction has been retired by the Retire Control Unit.
    Retired,
    /// Instruction is pending in the Scheduler.
    Pending,
    /// Instruction is ready to issue in the Scheduler.
    Ready,
    /// Instruction has been issued by the Scheduler.
    Issued,
    /// Instruction has finished execution.
    Executed,
    /// Instruction has been dispatched by the Dispatch logic.
    Dispatched,

    /// First value reserved for target-specific event types.
    LastGenericEventType,
  };

  /// Construct an instruction event of \p type for instruction \p Inst.
  ///
  /// \param type Event type; may be a GenericEventType or a target-specific
  ///             value.
  /// \param Inst Instruction this event was generated for.
  HWInstructionEvent(unsigned type, const InstRef &Inst)
      : Type(type), IR(Inst) {}

  /// Event type; the exact meaning depends on the subtarget.
  const unsigned Type;

  /// Instruction this event was generated for.
  const InstRef &IR;
};

/// Reference to a processor resource and an optional sub-unit mask.
///
/// ResourceRef::first is the index of the associated Resource.
/// ResourceRef::second is a bitmask of the referenced sub-unit of the resource.
using ResourceRef = std::pair<uint64_t, uint64_t>;

/// A processor resource reference paired with its release-at-cycles cost.
using ResourceUse = std::pair<ResourceRef, ReleaseAtCycles>;

/// Instruction event fired when an instruction is issued to hardware resources.
class HWInstructionIssuedEvent : public HWInstructionEvent {
public:
  /// Construct an issued event for \p IR using resources \p UR.
  ///
  /// \param IR Instruction that was issued.
  /// \param UR Resources consumed by the issue.
  HWInstructionIssuedEvent(const InstRef &IR, ArrayRef<ResourceUse> UR)
      : HWInstructionEvent(HWInstructionEvent::Issued, IR), UsedResources(UR) {}

  /// Resources consumed when this instruction was issued.
  ArrayRef<ResourceUse> UsedResources;
};

/// Instruction event fired when an instruction is dispatched.
class HWInstructionDispatchedEvent : public HWInstructionEvent {
public:
  /// Construct a dispatched event for \p IR with register and uOp counts.
  ///
  /// \param IR Instruction that was dispatched.
  /// \param Regs Per-register-file counts of physical registers allocated.
  /// \param UOps Number of micro-opcodes dispatched in this event.
  HWInstructionDispatchedEvent(const InstRef &IR, ArrayRef<unsigned> Regs,
                               unsigned UOps)
      : HWInstructionEvent(HWInstructionEvent::Dispatched, IR),
        UsedPhysRegs(Regs), MicroOpcodes(UOps) {}
  /// Number of physical registers allocated for this instruction.
  ///
  /// There is one entry per register file.
  ArrayRef<unsigned> UsedPhysRegs;
  /// Number of micro-opcodes dispatched in this event.
  ///
  /// This field is often set to the total number of micro-opcodes specified by
  /// the instruction descriptor of IR. The only exception is when IR declares a
  /// number of micro opcodes which exceeds the processor DispatchWidth, and -
  /// by construction - it requires multiple cycles to be fully dispatched. In
  /// that particular case, the dispatch logic would generate more than one
  /// dispatch event (one per cycle), and each event would declare how many
  /// micro opcodes are effectively been dispatched to the schedulers.
  unsigned MicroOpcodes;
};

/// Instruction event fired when an instruction is retired.
class HWInstructionRetiredEvent : public HWInstructionEvent {
public:
  /// Construct a retired event for \p IR freeing physical registers \p Regs.
  ///
  /// \param IR Instruction that was retired.
  /// \param Regs Per-register-file counts of register writes committed.
  HWInstructionRetiredEvent(const InstRef &IR, ArrayRef<unsigned> Regs)
      : HWInstructionEvent(HWInstructionEvent::Retired, IR),
        FreedPhysRegs(Regs) {}
  /// Number of register writes that have been architecturally committed.
  ///
  /// There is one entry per register file.
  ArrayRef<unsigned> FreedPhysRegs;
};

/// A pipeline stall caused by the lack of hardware resources.
class HWStallEvent {
public:
  /// Stall event types shared by all targets.
  enum GenericEventType {
    /// Sentinel for an uninitialized or unknown stall type.
    Invalid = 0,
    /// Stall waiting for a free physical register in the register file.
    RegisterFileStall,
    /// Stall waiting for a free slot in the Retire Control Unit.
    RetireControlUnitStall,
    /// Stall because the instruction would exceed the dispatch group limits.
    DispatchGroupStall,
    /// Stall because a scheduler queue is full.
    SchedulerQueueFull,
    /// Stall because the load queue is full.
    LoadQueueFull,
    /// Stall because the store queue is full.
    StoreQueueFull,
    /// Stall reported by target-specific CustomBehaviour logic.
    CustomBehaviourStall,
    /// First value reserved for target-specific stall types.
    LastGenericEvent
  };

  /// Construct a stall event of \p type for instruction \p Inst.
  ///
  /// \param type Stall type; may be a GenericEventType or a target-specific
  ///             value.
  /// \param Inst Instruction this stall was generated for.
  HWStallEvent(unsigned type, const InstRef &Inst) : Type(type), IR(Inst) {}

  /// Stall event type; the exact meaning depends on the subtarget.
  const unsigned Type;

  /// Instruction this stall event was generated for.
  const InstRef &IR;
};

/// An increase in backend pressure from data dependencies or resource limits.
class HWPressureEvent {
public:
  /// Reasons why backend pressure increased.
  enum GenericReason {
    /// Sentinel for an uninitialized or unknown pressure reason.
    INVALID = 0,
    /// Ready instructions could not issue because pipeline resources were
    /// unavailable.
    RESOURCES,
    /// Instructions could not issue because of register data dependencies.
    REGISTER_DEPS,
    /// Instructions could not issue because of memory dependencies.
    MEMORY_DEPS
  };

  /// Construct a pressure event with the given reason and affected instructions.
  ///
  /// \param reason Why backend pressure increased.
  /// \param Insts Instructions delayed by this pressure increase.
  /// \param Mask Bitmask of unavailable processor resources; defaults to 0.
  HWPressureEvent(GenericReason reason, ArrayRef<InstRef> Insts,
                  uint64_t Mask = 0)
      : Reason(reason), AffectedInstructions(Insts), ResourceMask(Mask) {}

  /// Reason for this increase in backend pressure.
  GenericReason Reason;

  /// Instructions delayed by this increase in backend pressure.
  ArrayRef<InstRef> AffectedInstructions;

  /// Bitmask of unavailable processor resources.
  const uint64_t ResourceMask;
};

/// Base class for observers of MCA pipeline hardware events.
class LLVM_ABI HWEventListener {
public:
  /// Called at the beginning of each simulated cycle.
  virtual void onCycleBegin() {}
  /// Called at the end of each simulated cycle.
  virtual void onCycleEnd() {}

  /// Notify this listener of an instruction state-change event.
  ///
  /// \param Event Instruction event to observe.
  virtual void onEvent(const HWInstructionEvent &Event) {}
  /// Notify this listener of a pipeline stall event.
  ///
  /// \param Event Stall event to observe.
  virtual void onEvent(const HWStallEvent &Event) {}
  /// Notify this listener of a backend pressure event.
  ///
  /// \param Event Pressure event to observe.
  virtual void onEvent(const HWPressureEvent &Event) {}

  /// Notify this listener that processor resource \p RRef became available.
  ///
  /// \param RRef Resource (and optional sub-unit mask) that is now free.
  virtual void onResourceAvailable(const ResourceRef &RRef) {}

  /// Notify this listener that buffered resources were reserved for \p Inst.
  ///
  /// Events generated by the Scheduler when buffered resources are consumed
  /// for an instruction.
  ///
  /// \param Inst Instruction that reserved the buffers.
  /// \param Buffers Indices of the buffered resources that were reserved.
  virtual void onReservedBuffers(const InstRef &Inst,
                                 ArrayRef<unsigned> Buffers) {}
  /// Notify this listener that buffered resources were released for \p Inst.
  ///
  /// Events generated by the Scheduler when buffered resources are freed for
  /// an instruction.
  ///
  /// \param Inst Instruction that released the buffers.
  /// \param Buffers Indices of the buffered resources that were released.
  virtual void onReleasedBuffers(const InstRef &Inst,
                                 ArrayRef<unsigned> Buffers) {}

  /// Destroy this hardware event listener.
  virtual ~HWEventListener() = default;

private:
  virtual void anchor();
};
} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_HWEVENTLISTENER_H
