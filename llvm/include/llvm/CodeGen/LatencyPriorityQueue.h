//===---- LatencyPriorityQueue.h - A latency-oriented priority queue ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the LatencyPriorityQueue class, which is a
// SchedulingPriorityQueue that schedules using latency information to
// reduce the length of the critical path through the basic block.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LATENCYPRIORITYQUEUE_H
#define LLVM_CODEGEN_LATENCYPRIORITYQUEUE_H

#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/Config/llvm-config.h"

namespace llvm {
  class LatencyPriorityQueue;

  /// Sorting functions for the Available queue.
  struct latency_sort {
    /// Owning latency priority queue used to resolve node priorities.
    LatencyPriorityQueue *PQ;
    /// Construct a sorter bound to \p pq.
    /// \param pq Latency priority queue whose heuristics drive comparisons.
    explicit latency_sort(LatencyPriorityQueue *pq) : PQ(pq) {}

    /// Return true if \p LHS has lower scheduling priority than \p RHS.
    /// \param LHS Left-hand scheduling unit.
    /// \param RHS Right-hand scheduling unit.
    /// \return True if \p LHS has lower scheduling priority than \p RHS.
    LLVM_ABI bool operator()(const SUnit *LHS, const SUnit *RHS) const;
  };

  /// Priority queue that schedules nodes by latency to shorten the critical
  /// path.
  class LLVM_ABI LatencyPriorityQueue : public SchedulingPriorityQueue {
    // SUnits - The SUnits for the current graph.
    std::vector<SUnit> *SUnits = nullptr;

    /// NumNodesSolelyBlocking - This vector contains, for every node in the
    /// Queue, the number of nodes that the node is the sole unscheduled
    /// predecessor for.  This is used as a tie-breaker heuristic for better
    /// mobility.
    std::vector<unsigned> NumNodesSolelyBlocking;

    /// Queue - The queue.
    std::vector<SUnit*> Queue;
    latency_sort Picker;

  public:
    /// Construct an empty latency priority queue.
    LatencyPriorityQueue() : Picker(this) {
    }

    /// Returns false; this queue schedules top-down.
    /// \return Always false.
    bool isBottomUp() const override { return false; }

    /// Initialize the queue from the DAG's SUnit vector.
    /// \param sunits Scheduling units owned by the ScheduleDAG.
    void initNodes(std::vector<SUnit> &sunits) override {
      SUnits = &sunits;
      NumNodesSolelyBlocking.resize(SUnits->size(), 0);
    }

    /// Notify the queue that \p SU was added to the DAG.
    /// \param SU Newly added scheduling unit.
    void addNode(const SUnit *SU) override {
      NumNodesSolelyBlocking.resize(SUnits->size(), 0);
    }

    /// Notify the queue that \p SU's priority-relevant state changed.
    /// \param SU Scheduling unit whose priority may need recomputation.
    void updateNode(const SUnit *SU) override {
    }

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

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    /// Dump queue contents for debugging.
    /// \param DAG Owning schedule DAG, if needed by the dump implementation.
    LLVM_DUMP_METHOD void dump(ScheduleDAG *DAG) const override;
#endif

    /// Adjust priorities after \p SU is scheduled.
    ///
    /// As nodes are scheduled, we look to see if there are any successor nodes
    /// that have a single unscheduled predecessor.  If so, that single
    /// predecessor has a higher priority, since scheduling it will make the
    /// node available.
    /// \param SU Node that was just scheduled.
    void scheduledNode(SUnit *SU) override;

private:
    void AdjustPriorityOfUnscheduledPreds(SUnit *SU);
    SUnit *getSingleUnscheduledPred(SUnit *SU);
  };
}

#endif
