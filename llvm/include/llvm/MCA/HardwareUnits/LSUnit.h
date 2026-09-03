//===------------------------- LSUnit.h --------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// A Load/Store unit class that models load/store queues and that implements
/// a simple weak memory consistency model.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_HARDWAREUNITS_LSUNIT_H
#define LLVM_MCA_HARDWAREUNITS_LSUNIT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MCA/HardwareUnits/HardwareUnit.h"
#include "llvm/MCA/Instruction.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace mca {

/// Abstract base interface for LS (load/store) units in llvm-mca.
class LLVM_ABI LSUnitBase : public HardwareUnit {
  /// Load queue size.
  ///
  /// A value of zero for this field means that the load queue is unbounded.
  /// Processor models can declare the size of a load queue via tablegen (see
  /// the definition of tablegen class LoadQueue in
  /// llvm/Target/TargetSchedule.td).
  unsigned LQSize;

  /// Load queue size.
  ///
  /// A value of zero for this field means that the store queue is unbounded.
  /// Processor models can declare the size of a store queue via tablegen (see
  /// the definition of tablegen class StoreQueue in
  /// llvm/Target/TargetSchedule.td).
  unsigned SQSize;

  unsigned UsedLQEntries;
  unsigned UsedSQEntries;

  /// True if loads don't alias with stores.
  ///
  /// By default, the LS unit assumes that loads and stores don't alias with
  /// each other. If this field is set to false, then loads are always assumed
  /// to alias with stores.
  const bool NoAlias;

public:
  /// Construct an LS unit with the given queue sizes and aliasing assumption.
  ///
  /// \param SM Scheduling model for the simulated processor.
  /// \param LoadQueueSize Maximum load-queue entries; zero means unbounded.
  /// \param StoreQueueSize Maximum store-queue entries; zero means unbounded.
  /// \param AssumeNoAlias When true, loads are assumed not to alias stores.
  LSUnitBase(const MCSchedModel &SM, unsigned LoadQueueSize,
             unsigned StoreQueueSize, bool AssumeNoAlias);

  /// Destroy the LS unit.
  ~LSUnitBase() override;

  /// Returns the total number of entries in the load queue.
  /// \return Total load-queue capacity; zero means unbounded.
  unsigned getLoadQueueSize() const { return LQSize; }

  /// Returns the total number of entries in the store queue.
  /// \return Total store-queue capacity; zero means unbounded.
  unsigned getStoreQueueSize() const { return SQSize; }

  /// Returns the number of load-queue entries currently in use.
  /// \return Count of occupied load-queue entries.
  unsigned getUsedLQEntries() const { return UsedLQEntries; }
  /// Returns the number of store-queue entries currently in use.
  /// \return Count of occupied store-queue entries.
  unsigned getUsedSQEntries() const { return UsedSQEntries; }
  /// Reserves one entry in the load queue.
  void acquireLQSlot() { ++UsedLQEntries; }
  /// Reserves one entry in the store queue.
  void acquireSQSlot() { ++UsedSQEntries; }
  /// Releases one entry in the load queue.
  void releaseLQSlot() { --UsedLQEntries; }
  /// Releases one entry in the store queue.
  void releaseSQSlot() { --UsedSQEntries; }

  /// Returns true if loads are assumed not to alias with stores.
  /// \return True when loads are assumed not to alias stores.
  bool assumeNoAlias() const { return NoAlias; }

  /// Status codes returned when checking load/store queue availability.
  enum Status {
    /// Enough queue entries are available for the instruction.
    LSU_AVAILABLE = 0,
    /// The load queue has no free entries.
    LSU_LQUEUE_FULL,
    /// The store queue has no free entries.
    LSU_SQUEUE_FULL
  };

  /// This method checks the availability of the load/store buffers.
  ///
  /// Returns LSU_AVAILABLE if there are enough load/store queue entries to
  /// accomodate instruction IR. By default, LSU_AVAILABLE is returned if IR is
  /// not a memory operation.
  ///
  /// \param IR Instruction to check for load/store queue availability.
  /// \return LSU_AVAILABLE, LSU_LQUEUE_FULL, or LSU_SQUEUE_FULL.
  virtual Status isAvailable(const InstRef &IR) const = 0;

  /// Allocates LS resources for instruction IR.
  ///
  /// This method assumes that a previous call to `isAvailable(IR)` succeeded
  /// with a LSUnitBase::Status value of LSU_AVAILABLE.
  /// Returns the GroupID associated with this instruction. That value will be
  /// used to set the LSUTokenID field in class Instruction.
  ///
  /// \param IR Instruction for which to allocate load/store resources.
  /// \return Memory-group identifier used as Instruction::LSUTokenID.
  virtual unsigned dispatch(const InstRef &IR) = 0;

  /// Returns true if the store queue currently holds no entries.
  /// \return True when no store-queue entries are in use.
  bool isSQEmpty() const { return !UsedSQEntries; }
  /// Returns true if the load queue currently holds no entries.
  /// \return True when no load-queue entries are in use.
  bool isLQEmpty() const { return !UsedLQEntries; }
  /// Returns true if the store queue is bounded and fully occupied.
  /// \return True when the store queue has a finite size and is full.
  bool isSQFull() const { return SQSize && SQSize == UsedSQEntries; }
  /// Returns true if the load queue is bounded and fully occupied.
  /// \return True when the load queue has a finite size and is full.
  bool isLQFull() const { return LQSize && LQSize == UsedLQEntries; }

  /// Check if a peviously dispatched instruction IR is now ready for execution.
  ///
  /// \param IR Instruction to test for readiness.
  /// \return True if \p IR is ready for execution.
  virtual bool isReady(const InstRef &IR) const = 0;

  /// Check if instruction IR only depends on memory instructions that are
  /// currently executing.
  ///
  /// \param IR Instruction to test for a pending memory dependency.
  /// \return True if \p IR only waits on currently executing memory ops.
  virtual bool isPending(const InstRef &IR) const = 0;

  /// Check if instruction IR is still waiting on memory operations, and the
  /// wait time is still unknown.
  ///
  /// \param IR Instruction to test for an unresolved memory wait.
  /// \return True if \p IR still waits on unresolved memory operations.
  virtual bool isWaiting(const InstRef &IR) const = 0;

  /// Returns true if instruction \p IR still has dependent memory users.
  ///
  /// \param IR Instruction to query for dependent users.
  /// \return True if \p IR still has dependent memory users.
  virtual bool hasDependentUsers(const InstRef &IR) const = 0;

  /// Returns the critical memory predecessor for the given group.
  ///
  /// \param GroupId Memory group identifier to query.
  /// \return Critical dependency describing the group's memory predecessor.
  virtual const CriticalDependency getCriticalPredecessor(unsigned GroupId) = 0;

  /// Notifies the LS unit that instruction \p IR has finished execution.
  ///
  /// \param IR Instruction that reached the executed state.
  virtual void onInstructionExecuted(const InstRef &IR) = 0;

  /// Notifies the LS unit that instruction \p IR has retired.
  ///
  /// Loads are tracked by the LDQ (load queue) from dispatch until completion.
  /// Stores are tracked by the STQ (store queue) from dispatch until commitment.
  /// By default we conservatively assume that the LDQ receives a load at
  /// dispatch. Loads leave the LDQ at retirement stage.
  ///
  /// \param IR Instruction that reached the retired state.
  virtual void onInstructionRetired(const InstRef &IR) = 0;

  /// Notifies the LS unit that instruction \p IR has been issued.
  ///
  /// \param IR Instruction that started execution.
  virtual void onInstructionIssued(const InstRef &IR) = 0;

  /// Advances simulation by one cycle for load/store bookkeeping.
  virtual void cycleEvent() = 0;

#ifndef NDEBUG
  /// Dumps the internal state of the LS unit for debugging.
  virtual void dump() const = 0;
#endif
};

/// Default Load/Store Unit (LS Unit) for simulated processors.
///
/// Each load (or store) consumes one entry in the load (or store) queue.
///
/// Rules are:
/// 1) A younger load is allowed to pass an older load only if there are no
///    stores nor barriers in between the two loads.
/// 2) An younger store is not allowed to pass an older store.
/// 3) A younger store is not allowed to pass an older load.
/// 4) A younger load is allowed to pass an older store only if the load does
///    not alias with the store.
///
/// This class optimistically assumes that loads don't alias store operations.
/// Under this assumption, younger loads are always allowed to pass older
/// stores (this would only affects rule 4).
/// Essentially, this class doesn't perform any sort alias analysis to
/// identify aliasing loads and stores.
///
/// To enforce aliasing between loads and stores, flag `AssumeNoAlias` must be
/// set to `false` by the constructor of LSUnit.
///
/// Note that this class doesn't know about the existence of different memory
/// types for memory operations (example: write-through, write-combining, etc.).
/// Derived classes are responsible for implementing that extra knowledge, and
/// provide different sets of rules for loads and stores by overriding method
/// `isReady()`.
/// To emulate a write-combining memory type, rule 2. must be relaxed in a
/// derived class to enable the reordering of non-aliasing store operations.
///
/// No assumptions are made by this class on the size of the store buffer.  This
/// class doesn't know how to identify cases where store-to-load forwarding may
/// occur.
///
/// LSUnit doesn't attempt to predict whether a load or store hits or misses
/// the L1 cache. To be more specific, LSUnit doesn't know anything about
/// cache hierarchy and memory types.
/// It only knows if an instruction "mayLoad" and/or "mayStore". For loads, the
/// scheduling model provides an "optimistic" load-to-use latency (which usually
/// matches the load-to-use latency for when there is a hit in the L1D).
/// Derived classes may expand this knowledge.
///
/// Class MCInstrDesc in LLVM doesn't know about serializing operations, nor
/// memory-barrier like instructions.
/// LSUnit conservatively assumes that an instruction which `mayLoad` and has
/// `unmodeled side effects` behave like a "soft" load-barrier. That means, it
/// serializes loads without forcing a flush of the load queue.
/// Similarly, instructions that both `mayStore` and have `unmodeled side
/// effects` are treated like store barriers. A full memory
/// barrier is a 'mayLoad' and 'mayStore' instruction with unmodeled side
/// effects. This is obviously inaccurate, but this is the best that we can do
/// at the moment.
///
/// Each load/store barrier consumes one entry in the load/store queue. A
/// load/store barrier enforces ordering of loads/stores:
///  - A younger load cannot pass a load barrier.
///  - A younger store cannot pass a store barrier.
///
/// A younger load has to wait for the memory load barrier to execute.
/// A load/store barrier is "executed" when it becomes the oldest entry in
/// the load/store queue(s). That also means, all the older loads/stores have
/// already been executed.
class LLVM_ABI LSUnit : public LSUnitBase {

  // This class doesn't know about the latency of a load instruction. So, it
  // conservatively/pessimistically assumes that the latency of a load opcode
  // matches the instruction latency.
  //
  // FIXME: In the absence of cache misses (i.e. L1I/L1D/iTLB/dTLB hits/misses),
  // and load/store conflicts, the latency of a load is determined by the depth
  // of the load pipeline. So, we could use field `LoadLatency` in the
  // MCSchedModel to model that latency.
  // Field `LoadLatency` often matches the so-called 'load-to-use' latency from
  // L1D, and it usually already accounts for any extra latency due to data
  // forwarding.
  // When doing throughput analysis, `LoadLatency` is likely to
  // be a better predictor of load latency than instruction latency. This is
  // particularly true when simulating code with temporal/spatial locality of
  // memory accesses.
  // Using `LoadLatency` (instead of the instruction latency) is also expected
  // to improve the load queue allocation for long latency instructions with
  // folded memory operands (See PR39829).
  //
  // FIXME: On some processors, load/store operations are split into multiple
  // uOps. For example, X86 AMD Jaguar natively supports 128-bit data types, but
  // not 256-bit data types. So, a 256-bit load is effectively split into two
  // 128-bit loads, and each split load consumes one 'LoadQueue' entry. For
  // simplicity, this class optimistically assumes that a load instruction only
  // consumes one entry in the LoadQueue.  Similarly, store instructions only
  // consume a single entry in the StoreQueue.
  // In future, we should reassess the quality of this design, and consider
  // alternative approaches that let instructions specify the number of
  // load/store queue entries which they consume at dispatch stage (See
  // PR39830).
  //
  // An instruction that both 'mayStore' and 'HasUnmodeledSideEffects' is
  // conservatively treated as a store barrier. It forces older store to be
  // executed before newer stores are issued.
  //
  // An instruction that both 'MayLoad' and 'HasUnmodeledSideEffects' is
  // conservatively treated as a load barrier. It forces older loads to execute
  // before newer loads are issued.

protected:
  /// A node of a memory dependency graph. A MemoryGroup describes a set of
  /// instructions with same memory dependencies.
  ///
  /// By construction, instructions of a MemoryGroup don't depend on each other.
  /// At dispatch stage, instructions are mapped by the LSUnit to MemoryGroups.
  /// A Memory group identifier is then stored as a "token" in field
  /// Instruction::LSUTokenID of each dispatched instructions. That token is
  /// used internally by the LSUnit to track memory dependencies.
  class MemoryGroup {
    unsigned NumPredecessors = 0;
    unsigned NumExecutingPredecessors = 0;
    unsigned NumExecutedPredecessors = 0;

    unsigned NumInstructions = 0;
    unsigned NumExecuting = 0;
    unsigned NumExecuted = 0;
    // Successors that are in a order dependency with this group.
    SmallVector<MemoryGroup *, 4> OrderSucc;
    // Successors that are in a data dependency with this group.
    SmallVector<MemoryGroup *, 4> DataSucc;

    CriticalDependency CriticalPredecessor;
    InstRef CriticalMemoryInstruction;

    MemoryGroup(const MemoryGroup &) = delete;
    MemoryGroup &operator=(const MemoryGroup &) = delete;

  public:
    /// Construct an empty memory group.
    MemoryGroup() = default;
    /// Move-construct a memory group.
    ///
    /// \param Other Memory group to move from.
    MemoryGroup(MemoryGroup &&Other) = default;

    /// Returns the number of successor groups linked by order or data edges.
    /// \return Combined count of order and data successors.
    size_t getNumSuccessors() const {
      return OrderSucc.size() + DataSucc.size();
    }
    /// Returns the number of predecessor groups this group depends on.
    /// \return Count of predecessor memory groups.
    unsigned getNumPredecessors() const { return NumPredecessors; }
    /// Returns the number of predecessors that are currently executing.
    /// \return Count of predecessors that have started but not finished.
    unsigned getNumExecutingPredecessors() const {
      return NumExecutingPredecessors;
    }
    /// Returns the number of predecessors that have finished execution.
    /// \return Count of predecessors that have finished execution.
    unsigned getNumExecutedPredecessors() const {
      return NumExecutedPredecessors;
    }
    /// Returns the number of instructions in this group.
    /// \return Count of instructions mapped to this group.
    unsigned getNumInstructions() const { return NumInstructions; }
    /// Returns the number of instructions in this group that are executing.
    /// \return Count of in-group instructions that are executing.
    unsigned getNumExecuting() const { return NumExecuting; }
    /// Returns the number of instructions in this group that have executed.
    /// \return Count of in-group instructions that have finished execution.
    unsigned getNumExecuted() const { return NumExecuted; }

    /// Returns the critical in-group memory instruction, if any.
    /// \return Reference to the critical memory instruction, or invalid.
    const InstRef &getCriticalMemoryInstruction() const {
      return CriticalMemoryInstruction;
    }
    /// Returns the critical predecessor dependency for this group.
    /// \return Critical dependency describing the group's memory predecessor.
    const CriticalDependency &getCriticalPredecessor() const {
      return CriticalPredecessor;
    }

    /// Adds \p Group as a successor with an optional data dependency.
    ///
    /// \param Group Successor memory group to link.
    /// \param IsDataDependent True when the edge is a data dependency.
    void addSuccessor(MemoryGroup *Group, bool IsDataDependent) {
      // Do not need to add a dependency if there is no data
      // dependency and all instructions from this group have been
      // issued already.
      if (!IsDataDependent && isExecuting())
        return;

      Group->NumPredecessors++;
      assert(!isExecuted() && "Should have been removed!");
      if (isExecuting())
        Group->onGroupIssued(CriticalMemoryInstruction, IsDataDependent);

      if (IsDataDependent)
        DataSucc.emplace_back(Group);
      else
        OrderSucc.emplace_back(Group);
    }

    /// Returns true if some predecessors have not yet started execution.
    /// \return True if some predecessors have not yet started execution.
    bool isWaiting() const {
      return NumPredecessors >
             (NumExecutingPredecessors + NumExecutedPredecessors);
    }
    /// Returns true if all predecessors have issued and some are still
    /// executing.
    /// \return True if all predecessors issued and some are still executing.
    bool isPending() const {
      return NumExecutingPredecessors &&
             ((NumExecutedPredecessors + NumExecutingPredecessors) ==
              NumPredecessors);
    }
    /// Returns true if all predecessors have finished execution.
    /// \return True if all predecessors have finished execution.
    bool isReady() const { return NumExecutedPredecessors == NumPredecessors; }
    /// Returns true if every non-executed instruction in the group is executing.
    /// \return True if every non-executed instruction in the group is executing.
    bool isExecuting() const {
      return NumExecuting && (NumExecuting == (NumInstructions - NumExecuted));
    }
    /// Returns true if every instruction in the group has finished execution.
    /// \return True if every instruction in the group has finished execution.
    bool isExecuted() const { return NumInstructions == NumExecuted; }

    /// Records that a predecessor group has started issuing instructions.
    ///
    /// \param IR Instruction from the predecessor used to update the critical
    ///        dependency.
    /// \param ShouldUpdateCriticalDep True when \p IR should update the
    ///        critical predecessor.
    void onGroupIssued(const InstRef &IR, bool ShouldUpdateCriticalDep) {
      assert(!isReady() && "Unexpected group-start event!");
      NumExecutingPredecessors++;

      if (!ShouldUpdateCriticalDep)
        return;

      unsigned Cycles = IR.getInstruction()->getCyclesLeft();
      if (CriticalPredecessor.Cycles < Cycles) {
        CriticalPredecessor.IID = IR.getSourceIndex();
        CriticalPredecessor.Cycles = Cycles;
      }
    }

    /// Records that a predecessor group has finished execution.
    void onGroupExecuted() {
      assert(!isReady() && "Inconsistent state found!");
      NumExecutingPredecessors--;
      NumExecutedPredecessors++;
    }

    /// Records that instruction \p IR from this group has been issued.
    ///
    /// \param IR Instruction that started execution.
    void onInstructionIssued(const InstRef &IR) {
      assert(!isExecuting() && "Invalid internal state!");
      ++NumExecuting;

      // update the CriticalMemDep.
      const Instruction &IS = *IR.getInstruction();
      if ((bool)CriticalMemoryInstruction) {
        const Instruction &OtherIS =
            *CriticalMemoryInstruction.getInstruction();
        if (OtherIS.getCyclesLeft() < IS.getCyclesLeft())
          CriticalMemoryInstruction = IR;
      } else {
        CriticalMemoryInstruction = IR;
      }

      if (!isExecuting())
        return;

      // Notify successors that this group started execution.
      for (MemoryGroup *MG : OrderSucc) {
        MG->onGroupIssued(CriticalMemoryInstruction, false);
        // Release the order dependency with this group.
        MG->onGroupExecuted();
      }

      for (MemoryGroup *MG : DataSucc)
        MG->onGroupIssued(CriticalMemoryInstruction, true);
    }

    /// Records that instruction \p IR from this group has finished execution.
    ///
    /// \param IR Instruction that reached the executed state.
    void onInstructionExecuted(const InstRef &IR) {
      assert(isReady() && !isExecuted() && "Invalid internal state!");
      --NumExecuting;
      ++NumExecuted;

      if (CriticalMemoryInstruction &&
          CriticalMemoryInstruction.getSourceIndex() == IR.getSourceIndex()) {
        CriticalMemoryInstruction.invalidate();
      }

      if (!isExecuted())
        return;

      // Notify data dependent successors that this group has finished
      // execution.
      for (MemoryGroup *MG : DataSucc)
        MG->onGroupExecuted();
    }

    /// Adds one instruction to this memory group.
    void addInstruction() {
      assert(!getNumSuccessors() && "Cannot add instructions to this group!");
      ++NumInstructions;
    }

    /// Decrements the remaining cycles on the critical predecessor, if any.
    void cycleEvent() {
      if (isWaiting() && CriticalPredecessor.Cycles)
        CriticalPredecessor.Cycles--;
    }
  };
  /// Used to map group identifiers to MemoryGroups.
  DenseMap<unsigned, std::unique_ptr<MemoryGroup>> Groups;
  /// Next memory-group identifier to assign.
  unsigned NextGroupID = 1;

  /// Group identifier of the most recent non-barrier load group.
  unsigned CurrentLoadGroupID;
  /// Group identifier of the most recent load-barrier group.
  unsigned CurrentLoadBarrierGroupID;
  /// Group identifier of the most recent non-barrier store group.
  unsigned CurrentStoreGroupID;
  /// Group identifier of the most recent store-barrier group.
  unsigned CurrentStoreBarrierGroupID;

public:
  /// Construct an LS unit with unbounded queues and aliasing assumed absent.
  ///
  /// \param SM Scheduling model for the simulated processor.
  LSUnit(const MCSchedModel &SM)
      : LSUnit(SM, /* LQSize */ 0, /* SQSize */ 0, /* NoAlias */ false) {}
  /// Construct an LS unit with the given queue sizes and aliasing assumed
  /// absent.
  ///
  /// \param SM Scheduling model for the simulated processor.
  /// \param LQ Maximum load-queue entries; zero means unbounded.
  /// \param SQ Maximum store-queue entries; zero means unbounded.
  LSUnit(const MCSchedModel &SM, unsigned LQ, unsigned SQ)
      : LSUnit(SM, LQ, SQ, /* NoAlias */ false) {}
  /// Construct an LS unit with the given queue sizes and aliasing assumption.
  ///
  /// \param SM Scheduling model for the simulated processor.
  /// \param LQ Maximum load-queue entries; zero means unbounded.
  /// \param SQ Maximum store-queue entries; zero means unbounded.
  /// \param AssumeNoAlias When true, loads are assumed not to alias stores.
  LSUnit(const MCSchedModel &SM, unsigned LQ, unsigned SQ, bool AssumeNoAlias)
      : LSUnitBase(SM, LQ, SQ, AssumeNoAlias), CurrentLoadGroupID(0),
        CurrentLoadBarrierGroupID(0), CurrentStoreGroupID(0),
        CurrentStoreBarrierGroupID(0) {}

  /// Returns LSU_AVAILABLE if there are enough load/store queue entries to
  /// accomodate instruction IR.
  ///
  /// \param IR Instruction to check for load/store queue availability.
  /// \return LSU_AVAILABLE, LSU_LQUEUE_FULL, or LSU_SQUEUE_FULL.
  Status isAvailable(const InstRef &IR) const override;

  /// Returns true if instruction \p IR is ready for execution.
  ///
  /// \param IR Instruction to test for readiness.
  /// \return True if \p IR is ready for execution.
  bool isReady(const InstRef &IR) const override {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    const MemoryGroup &Group = getGroup(GroupID);
    return Group.isReady();
  }

  /// Returns true if instruction \p IR only waits on executing memory ops.
  ///
  /// \param IR Instruction to test for a pending memory dependency.
  /// \return True if \p IR only waits on currently executing memory ops.
  bool isPending(const InstRef &IR) const override {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    const MemoryGroup &Group = getGroup(GroupID);
    return Group.isPending();
  }

  /// Returns true if instruction \p IR still waits on unresolved memory ops.
  ///
  /// \param IR Instruction to test for an unresolved memory wait.
  /// \return True if \p IR still waits on unresolved memory operations.
  bool isWaiting(const InstRef &IR) const override {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    const MemoryGroup &Group = getGroup(GroupID);
    return Group.isWaiting();
  }

  /// Returns true if instruction \p IR still has dependent memory users.
  ///
  /// \param IR Instruction to query for dependent users.
  /// \return True if \p IR still has dependent memory users.
  bool hasDependentUsers(const InstRef &IR) const override {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    const MemoryGroup &Group = getGroup(GroupID);
    return !Group.isExecuted() && Group.getNumSuccessors();
  }

  /// Returns the critical memory predecessor for the given group.
  ///
  /// \param GroupId Memory group identifier to query.
  /// \return Critical dependency describing the group's memory predecessor.
  const CriticalDependency getCriticalPredecessor(unsigned GroupId) override {
    const MemoryGroup &Group = getGroup(GroupId);
    return Group.getCriticalPredecessor();
  }

  /// Allocates LS resources for instruction IR.
  ///
  /// This method assumes that a previous call to `isAvailable(IR)` succeeded
  /// returning LSU_AVAILABLE.
  ///
  /// Rules are:
  /// By default, rules are:
  /// 1. A store may not pass a previous store.
  /// 2. A load may not pass a previous store unless flag 'NoAlias' is set.
  /// 3. A load may pass a previous load.
  /// 4. A store may not pass a previous load (regardless of flag 'NoAlias').
  /// 5. A load has to wait until an older load barrier is fully executed.
  /// 6. A store has to wait until an older store barrier is fully executed.
  ///
  /// \param IR Instruction for which to allocate load/store resources.
  /// \return Memory-group identifier used as Instruction::LSUTokenID.
  unsigned dispatch(const InstRef &IR) override;

  /// Notifies the LS unit that instruction \p IR has been issued.
  ///
  /// \param IR Instruction that started execution.
  void onInstructionIssued(const InstRef &IR) override {
    unsigned GroupID = IR.getInstruction()->getLSUTokenID();
    Groups[GroupID]->onInstructionIssued(IR);
  }

  /// Notifies the LS unit that instruction \p IR has retired.
  ///
  /// \param IR Instruction that reached the retired state.
  void onInstructionRetired(const InstRef &IR) override;

  /// Notifies the LS unit that instruction \p IR has finished execution.
  ///
  /// \param IR Instruction that reached the executed state.
  void onInstructionExecuted(const InstRef &IR) override;

  /// Advances simulation by one cycle for load/store bookkeeping.
  void cycleEvent() override;

#ifndef NDEBUG
  /// Dumps the internal state of the LS unit for debugging.
  void dump() const override;
#endif

private:
  bool isValidGroupID(unsigned Index) const {
    return Index && Groups.contains(Index);
  }

  const MemoryGroup &getGroup(unsigned Index) const {
    assert(isValidGroupID(Index) && "Group doesn't exist!");
    return *Groups.find(Index)->second;
  }

  MemoryGroup &getGroup(unsigned Index) {
    assert(isValidGroupID(Index) && "Group doesn't exist!");
    return *Groups.find(Index)->second;
  }

  unsigned createMemoryGroup() {
    Groups.insert(std::make_pair(NextGroupID, std::make_unique<MemoryGroup>()));
    return NextGroupID++;
  }
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_HARDWAREUNITS_LSUNIT_H
