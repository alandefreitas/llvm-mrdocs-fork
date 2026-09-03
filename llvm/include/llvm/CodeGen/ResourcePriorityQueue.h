//===----- ResourcePriorityQueue.h - A DFA-oriented priority queue -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ResourcePriorityQueue class, which is a
// SchedulingPriorityQueue that schedules using DFA state to
// reduce the length of the critical path through the basic block
// on VLIW platforms.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_RESOURCEPRIORITYQUEUE_H
#define LLVM_CODEGEN_RESOURCEPRIORITYQUEUE_H

#include "llvm/CodeGen/ScheduleDAG.h"

namespace llvm {
  class DFAPacketizer;
  class InstrItineraryData;
  class ResourcePriorityQueue;
  class SelectionDAGISel;
  class TargetInstrInfo;
  class TargetRegisterInfo;

  /// Sorting functions for the Available queue.
  struct resource_sort {
    /// Owning resource priority queue used to resolve node priorities.
    ResourcePriorityQueue *PQ;
    /// Construct a sorter bound to \p pq.
    /// \param pq Resource priority queue whose heuristics drive comparisons.
    explicit resource_sort(ResourcePriorityQueue *pq) : PQ(pq) {}

    /// Return true if \p LHS has lower scheduling priority than \p RHS.
    /// \param LHS Left-hand scheduling unit.
    /// \param RHS Right-hand scheduling unit.
    /// \return True if \p LHS has lower scheduling priority than \p RHS.
    LLVM_ABI bool operator()(const SUnit *LHS, const SUnit *RHS) const;
  };

  /// Priority queue that schedules nodes using DFA resource state to shorten
  /// the critical path on VLIW-like targets.
  class LLVM_ABI ResourcePriorityQueue : public SchedulingPriorityQueue {
    /// SUnits - The SUnits for the current graph.
    std::vector<SUnit> *SUnits;

    /// NumNodesSolelyBlocking - This vector contains, for every node in the
    /// Queue, the number of nodes that the node is the sole unscheduled
    /// predecessor for.  This is used as a tie-breaker heuristic for better
    /// mobility.
    std::vector<unsigned> NumNodesSolelyBlocking;

    /// Queue - The queue.
    std::vector<SUnit*> Queue;

    /// RegPressure - Tracking current reg pressure per register class.
    ///
    std::vector<unsigned> RegPressure;

    /// RegLimit - Tracking the number of allocatable registers per register
    /// class.
    std::vector<unsigned> RegLimit;

    resource_sort Picker;
    const TargetRegisterInfo *TRI;
    const TargetLowering *TLI;
    const TargetInstrInfo *TII;
    const InstrItineraryData* InstrItins;
    /// ResourcesModel - Represents VLIW state.
    /// Not limited to VLIW targets per say, but assumes
    /// definition of DFA by a target.
    std::unique_ptr<DFAPacketizer> ResourcesModel;

    /// Resource model - packet/bundle model. Purely
    /// internal at the time.
    std::vector<SUnit*> Packet;

    /// Heuristics for estimating register pressure.
    unsigned ParallelLiveRanges;
    int HorizontalVerticalBalance;

  public:
    /// Construct a resource priority queue for the given instruction selector.
    /// \param IS Instruction selector providing the MachineFunction and TLI.
    ResourcePriorityQueue(SelectionDAGISel *IS);
    /// Destroy the resource priority queue and its DFA packetizer.
    ~ResourcePriorityQueue() override;

    /// Returns false; this queue schedules top-down.
    /// \return Always false.
    bool isBottomUp() const override { return false; }

    /// Initialize the queue from the DAG's SUnit vector.
    /// \param sunits Scheduling units owned by the ScheduleDAG.
    void initNodes(std::vector<SUnit> &sunits) override;

    /// Notify the queue that \p SU was added to the DAG.
    /// \param SU Newly added scheduling unit.
    void addNode(const SUnit *SU) override {
      NumNodesSolelyBlocking.resize(SUnits->size(), 0);
    }

    /// Notify the queue that \p SU's priority-relevant state changed.
    /// \param SU Scheduling unit whose priority may need recomputation.
    void updateNode(const SUnit *SU) override {}

    /// Release any queue state held between scheduling regions.
    void releaseState() override {
      SUnits = nullptr;
    }

    /// Return the latency (height) of the node with index \p NodeNum.
    /// \param NodeNum Index of the SUnit in the current graph.
    /// \return Latency height of the specified node.
    unsigned getLatency(unsigned NodeNum) const {
      assert(NodeNum < (*SUnits).size());
      return (*SUnits)[NodeNum].getHeight();
    }

    /// Return how many nodes are solely blocked by the node with index
    /// \p NodeNum.
    /// \param NodeNum Index of the SUnit in NumNodesSolelyBlocking.
    /// \return Count of nodes solely blocked by the specified node.
    unsigned getNumSolelyBlockNodes(unsigned NodeNum) const {
      assert(NodeNum < NumNodesSolelyBlocking.size());
      return NumNodesSolelyBlocking[NodeNum];
    }

    /// Return a cost reflecting the benefit of scheduling \p SU in the current
    /// cycle.
    /// \param SU Candidate scheduling unit.
    /// \return Scheduling cost reflecting the benefit of scheduling \p SU.
    int SUSchedulingCost (SUnit *SU);

    /// Determine the number of registers defined by this node.
    /// \param SU Node whose remaining register definitions are initialized.
    void initNumRegDefsLeft(SUnit *SU);
    /// Estimate the change in register pressure from scheduling \p SU.
    ///
    /// Tracks defined and used vregs in dependent instructions. When
    /// \p RawPressure is true, ignores existing register-file limits and
    /// reports the raw def/use balance.
    /// \param SU Candidate scheduling unit.
    /// \param RawPressure If true, ignore register-file sizes.
    /// \return Estimated register-pressure change from scheduling \p SU.
    int regPressureDelta(SUnit *SU, bool RawPressure = false);
    /// Return the raw register-pressure delta of \p SU for register class
    /// \p RCId.
    /// \param SU Candidate scheduling unit.
    /// \param RCId Target register class identifier.
    /// \return Raw register-pressure delta for the given register class.
    int rawRegPressureDelta (SUnit *SU, unsigned RCId);

    /// Returns true if the queue contains no nodes.
    /// \return True if the queue contains no nodes.
    bool empty() const override { return Queue.empty(); }

    /// Push scheduling unit \p U onto the queue.
    /// \param U Node to insert.
    void push(SUnit *U) override;

    /// Remove and return the highest-priority ready node.
    /// \return Highest-priority ready scheduling unit.
    SUnit *pop() override;

    /// Remove \p SU from the queue without returning it.
    /// \param SU Node to remove.
    void remove(SUnit *SU) override;

    /// Update resource and register-pressure state after \p SU is scheduled.
    ///
    /// Main resource tracking point.
    /// \param SU Node that was just scheduled, or null to reset DFA state.
    void scheduledNode(SUnit *SU) override;
    /// Return true if \p SU can be scheduled in the current packet.
    /// \param SU Candidate scheduling unit.
    /// \return True if \p SU can be scheduled in the current packet.
    bool isResourceAvailable(SUnit *SU);
    /// Reserve DFA resources for \p SU in the current packet.
    /// \param SU Node whose resources are reserved.
    void reserveResources(SUnit *SU);

private:
    void adjustPriorityOfUnscheduledPreds(SUnit *SU);
    SUnit *getSingleUnscheduledPred(SUnit *SU);
    unsigned numberRCValPredInSU (SUnit *SU, unsigned RCId);
    unsigned numberRCValSuccInSU (SUnit *SU, unsigned RCId);
  };
}

#endif
