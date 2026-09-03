//===- VLIWMachineScheduler.h - VLIW-Focused Scheduling Pass ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//                                                                            //
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_VLIWMACHINESCHEDULER_H
#define LLVM_CODEGEN_VLIWMACHINESCHEDULER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include <limits>
#include <memory>
#include <utility>

namespace llvm {

class DFAPacketizer;
class RegisterClassInfo;
class ScheduleHazardRecognizer;
class SUnit;
class TargetInstrInfo;
class TargetSubtargetInfo;

/// Model of VLIW packet resources used while scheduling SUnits.
class LLVM_ABI VLIWResourceModel {
protected:
  /// Target instruction info used to create the DFA packetizer.
  const TargetInstrInfo *TII;

  /// ResourcesModel - Represents VLIW state.
  /// Not limited to VLIW targets per se, but assumes definition of resource
  /// model by a target.
  DFAPacketizer *ResourcesModel;

  /// Target scheduling model that supplies issue width and related limits.
  const TargetSchedModel *SchedModel;

  /// Local packet/bundle model. Purely
  /// internal to the MI scheduler at the time.
  SmallVector<SUnit *> Packet;

  /// Total packets created.
  unsigned TotalPackets = 0;

public:
  /// Construct a resource model for \p STI using scheduling model \p SM.
  /// @param STI Subtarget used to create the DFA packetizer.
  /// @param SM Target scheduling model for issue width and resources.
  VLIWResourceModel(const TargetSubtargetInfo &STI, const TargetSchedModel *SM);
  /// Assignment is deleted; VLIWResourceModel is not copyable.
  /// @param other Unused; copy assignment is deleted.
  VLIWResourceModel &operator=(const VLIWResourceModel &other) = delete;
  /// Copy construction is deleted; VLIWResourceModel is not copyable.
  /// @param other Unused; copy construction is deleted.
  VLIWResourceModel(const VLIWResourceModel &other) = delete;
  /// Destroy the resource model and its owned DFA packetizer.
  virtual ~VLIWResourceModel();

  /// Clear the current packet and DFA resource state.
  virtual void reset();

  /// Return true if \p SUd has a positive-latency dependence on \p SUu.
  /// @param SUd Producer scheduling unit already in (or considered for) a packet.
  /// @param SUu Consumer scheduling unit being tested against \p SUd.
  /// @return True if \p SUd has a positive-latency dependence on \p SUu.
  virtual bool hasDependence(const SUnit *SUd, const SUnit *SUu);
  /// Return true if \p SU can be added to the current packet.
  /// @param SU Scheduling unit to test for resource availability.
  /// @param IsTop True when checking top-down packeting dependencies.
  /// @return True if \p SU can be added to the current packet.
  virtual bool isResourceAvailable(SUnit *SU, bool IsTop);
  /// Reserve resources for \p SU, starting a new packet if needed.
  /// @param SU Scheduling unit to reserve, or nullptr to force a new packet.
  /// @param IsTop True when packeting in the top-down direction.
  /// @return True if \p SU was reserved in the current or a new packet.
  virtual bool reserveResources(SUnit *SU, bool IsTop);
  /// Return the total number of packets created so far.
  /// @return The total number of packets created so far.
  unsigned getTotalPackets() const { return TotalPackets; }
  /// Return the number of instructions currently in the open packet.
  /// @return The number of instructions currently in the open packet.
  size_t getPacketInstCount() const { return Packet.size(); }
  /// Return true if \p SU is already present in the current packet.
  /// @param SU Scheduling unit to look up in the open packet.
  /// @return True if \p SU is already present in the current packet.
  bool isInPacket(SUnit *SU) const { return is_contained(Packet, SU); }

protected:
  /// Create a DFA packetizer for \p STI.
  /// @param STI Subtarget whose instruction info builds the packetizer.
  /// @return A newly created DFA packetizer for \p STI.
  virtual DFAPacketizer *createPacketizer(const TargetSubtargetInfo &STI) const;
};

/// Extend the standard ScheduleDAGMILive to provide more context and override
/// the top-level schedule() driver.
class LLVM_ABI VLIWMachineScheduler : public ScheduleDAGMILive {
public:
  /// Construct a VLIW machine scheduler with strategy \p S.
  /// @param C MachineSchedContext providing pass analyses.
  /// @param S Scheduling strategy that selects nodes from the ready queues.
  VLIWMachineScheduler(MachineSchedContext *C,
                       std::unique_ptr<MachineSchedStrategy> S)
      : ScheduleDAGMILive(C, std::move(S)) {}

  /// Schedule - This is called back from ScheduleDAGInstrs::Run() when it's
  /// time to do some work.
  void schedule() override;

  /// Return register-class info used for pressure tracking in this DAG.
  /// @return Register-class info used for pressure tracking in this DAG.
  RegisterClassInfo *getRegClassInfo() { return RegClassInfo; }
  /// Return the number of instructions in the current basic block.
  /// @return The number of instructions in the current basic block.
  int getBBSize() { return BB->size(); }
};

//===----------------------------------------------------------------------===//
// ConvergingVLIWScheduler - Implementation of a VLIW-aware
// MachineSchedStrategy.
//===----------------------------------------------------------------------===//

/// VLIW-aware MachineSchedStrategy that converges from top and bottom queues.
class LLVM_ABI ConvergingVLIWScheduler : public MachineSchedStrategy {
protected:
  /// Store the state used by ConvergingVLIWScheduler heuristics, required
  ///  for the lifetime of one invocation of pickNode().
  struct SchedCandidate {
    /// Best SUnit candidate currently under consideration.
    SUnit *SU = nullptr;

    /// Register-pressure delta associated with the best candidate.
    RegPressureDelta RPDelta;

    /// Best scheduling cost computed for this candidate.
    int SCost = 0;

    /// Construct an empty candidate with no selected SUnit.
    SchedCandidate() = default;
  };
  /// Represent the type of SchedCandidate found within a single queue.
  enum CandResult {
    /// No viable candidate was found in the queue.
    NoCand,
    /// Prefer the candidate that preserves original node order.
    NodeOrder,
    /// Single candidate that reduces an excess pressure set.
    SingleExcess,
    /// Single candidate that reduces a critical pressure set.
    SingleCritical,
    /// Single candidate that reduces a maximum pressure set.
    SingleMax,
    /// Multiple candidates affect register pressure.
    MultiPressure,
    /// Candidate selected because it has the best scheduling cost.
    BestCost,
    /// Candidate selected based on weak (heuristic) edges.
    Weak
  };

  // Constants used to denote relative importance of
  // heuristic components for cost computation.
  /// Highest-weight heuristic priority scale factor.
  static constexpr unsigned PriorityOne = 200;
  /// Medium heuristic priority scale factor.
  static constexpr unsigned PriorityTwo = 50;
  /// Secondary heuristic priority scale factor.
  static constexpr unsigned PriorityThree = 75;
  /// Secondary cost scaling divisor used by heuristics.
  static constexpr unsigned ScaleTwo = 10;

  /// One-sided scheduling boundary with ready queues and hazard state.
  ///
  /// Each scheduling boundary is associated with ready queues. It tracks the
  /// current cycle in whichever direction it has moved, and maintains the state
  /// of "hazards" and other interlocks at the current cycle.
  struct VLIWSchedBoundary {
    /// Schedule DAG that owns the SUnits being scheduled from this boundary.
    VLIWMachineScheduler *DAG = nullptr;
    /// Target scheduling model consulted for issue width and latency.
    const TargetSchedModel *SchedModel = nullptr;

    /// Queue of SUnits that are ready to schedule at the current cycle.
    ReadyQueue Available;
    /// Queue of SUnits waiting until their ready cycle or hazard clears.
    ReadyQueue Pending;
    /// True when Pending must be drained before picking another node.
    bool CheckPending = false;

    /// Hazard recognizer for this scheduling direction.
    ScheduleHazardRecognizer *HazardRec = nullptr;
    /// VLIW packet resource model for this scheduling direction.
    VLIWResourceModel *ResourceModel = nullptr;

    /// Current cycle advanced by this scheduling boundary.
    unsigned CurrCycle = 0;
    /// Micro-ops or issues counted within the current cycle.
    unsigned IssueCount = 0;
    /// Estimated critical-path length used by the cost model.
    unsigned CriticalPathLength = 0;

    /// MinReadyCycle - Cycle of the soonest available instruction.
    unsigned MinReadyCycle = std::numeric_limits<unsigned>::max();

    /// Greatest minimum operand latency observed while scheduling this zone.
    unsigned MaxMinLatency = 0;

    /// Pending queues extend the ready queues with the same ID and the
    /// PendingFlag set.
    /// @param ID Ready-queue identifier for Available (and Pending via shift).
    /// @param Name Base name used to label the Available and Pending queues.
    VLIWSchedBoundary(unsigned ID, const Twine &Name)
        : Available(ID, Name + ".A"),
          Pending(ID << ConvergingVLIWScheduler::LogMaxQID, Name + ".P") {}

    /// Destroy hazard and resource-model state owned by this boundary.
    LLVM_ABI ~VLIWSchedBoundary();
    /// Assignment is deleted; VLIWSchedBoundary is not copyable.
    /// @param other Unused; copy assignment is deleted.
    VLIWSchedBoundary &operator=(const VLIWSchedBoundary &other) = delete;
    /// Copy construction is deleted; VLIWSchedBoundary is not copyable.
    /// @param other Unused; copy construction is deleted.
    VLIWSchedBoundary(const VLIWSchedBoundary &other) = delete;

    /// Initialize this boundary for \p dag using scheduling model \p smodel.
    /// @param dag VLIW schedule DAG being scheduled.
    /// @param smodel Target scheduling model for this region.
    void init(VLIWMachineScheduler *dag, const TargetSchedModel *smodel) {
      DAG = dag;
      SchedModel = smodel;
      CurrCycle = 0;
      IssueCount = 0;
      // Initialize the critical path length limit, which used by the scheduling
      // cost model to determine the value for scheduling an instruction. We use
      // a slightly different heuristic for small and large functions. For small
      // functions, it's important to use the height/depth of the instruction.
      // For large functions, prioritizing by height or depth increases spills.
      const auto BBSize = DAG->getBBSize();
      CriticalPathLength = BBSize / SchedModel->getIssueWidth();
      if (BBSize < 50)
        // We divide by two as a cheap and simple heuristic to reduce the
        // critcal path length, which increases the priority of using the graph
        // height/depth in the scheduler's cost computation.
        CriticalPathLength >>= 1;
      else {
        // For large basic blocks, we prefer a larger critical path length to
        // decrease the priority of using the graph height/depth.
        unsigned MaxPath = 0;
        for (auto &SU : DAG->SUnits)
          MaxPath = std::max(MaxPath, isTop() ? SU.getHeight() : SU.getDepth());
        CriticalPathLength = std::max(CriticalPathLength, MaxPath) + 1;
      }
    }

    /// Return true if this boundary schedules from the top of the region.
    /// @return True if this boundary schedules from the top of the region.
    bool isTop() const {
      return Available.getID() == ConvergingVLIWScheduler::TopQID;
    }

    /// Return true if scheduling \p SU would encounter a hazard this cycle.
    /// @param SU Scheduling unit to check against the hazard recognizer.
    /// @return True if scheduling \p SU would encounter a hazard this cycle.
    LLVM_ABI bool checkHazard(SUnit *SU);

    /// Release \p SU into Available or Pending based on \p ReadyCycle.
    /// @param SU Scheduling unit becoming ready.
    /// @param ReadyCycle Earliest cycle at which \p SU may issue.
    LLVM_ABI void releaseNode(SUnit *SU, unsigned ReadyCycle);

    /// Advance this boundary to the next cycle and clear issue state.
    LLVM_ABI void bumpCycle();

    /// Account for scheduling \p SU at the current cycle.
    /// @param SU Scheduling unit just selected for this boundary.
    LLVM_ABI void bumpNode(SUnit *SU);

    /// Move pending nodes that are ready into the Available queue.
    LLVM_ABI void releasePending();

    /// Remove \p SU from Available or Pending if present.
    /// @param SU Scheduling unit to remove from the ready queues.
    LLVM_ABI void removeReady(SUnit *SU);

    /// Return the sole Available candidate after updating Pending, or nullptr.
    /// @return The sole Available candidate, or nullptr if zero or many remain.
    LLVM_ABI SUnit *pickOnlyChoice();

      /// Return true if critical-path length binds scheduling of \p SU.
    /// @param SU Scheduling unit whose height/depth is compared to the path.
    /// @return True if critical-path length binds scheduling of \p SU.
    bool isLatencyBound(SUnit *SU) {
      if (CurrCycle >= CriticalPathLength)
        return true;
      unsigned PathLength = isTop() ? SU->getHeight() : SU->getDepth();
      return CriticalPathLength - CurrCycle <= PathLength;
    }
  };

  /// Schedule DAG currently being scheduled by this strategy.
  VLIWMachineScheduler *DAG = nullptr;
  /// Target scheduling model for the current region.
  const TargetSchedModel *SchedModel = nullptr;

  // State of the top and bottom scheduled instruction boundaries.
  /// Top-down scheduling boundary and its ready queues.
  VLIWSchedBoundary Top;
  /// Bottom-up scheduling boundary and its ready queues.
  VLIWSchedBoundary Bot;

  /// List of pressure sets that have a high pressure level in the region.
  SmallVector<bool> HighPressureSets;

public:
  /// SUnit::NodeQueueId: 0 (none), 1 (top), 2 (bot), 3 (both)
  enum {
    /// Ready-queue ID for the top-down boundary.
    TopQID = 1,
    /// Ready-queue ID for the bottom-up boundary.
    BotQID = 2,
    /// Log2 of the maximum queue ID; Pending IDs are shifted by this amount.
    LogMaxQID = 2
  };

  /// Construct a converging VLIW scheduler with top and bottom boundaries.
  ConvergingVLIWScheduler() : Top(TopQID, "TopQ"), Bot(BotQID, "BotQ") {}
  /// Destroy the converging VLIW scheduler.
  ~ConvergingVLIWScheduler() override = default;

  /// Initialize this strategy for the given schedule DAG.
  /// @param dag Schedule DAG MI for the current region.
  void initialize(ScheduleDAGMI *dag) override;

  /// Pick the next node to schedule and set \p IsTopNode accordingly.
  /// @param IsTopNode Set to true if the node should be scheduled top-down.
  /// @return The next node to schedule, or null if none remain.
  SUnit *pickNode(bool &IsTopNode) override;

  /// Notify that \p SU has been scheduled at the top or bottom.
  /// @param SU Scheduling unit that was just scheduled.
  /// @param IsTopNode True if \p SU was scheduled from the top boundary.
  void schedNode(SUnit *SU, bool IsTopNode) override;

  /// Release \p SU for top-down scheduling once predecessors are resolved.
  /// @param SU Scheduling unit whose predecessors are now scheduled.
  void releaseTopNode(SUnit *SU) override;

  /// Release \p SU for bottom-up scheduling once successors are resolved.
  /// @param SU Scheduling unit whose successors are now scheduled.
  void releaseBottomNode(SUnit *SU) override;

  /// Return the total packets formed by the top and bottom resource models.
  /// @return The sum of packets created by the top and bottom resource models.
  unsigned reportPackets() {
    return Top.ResourceModel->getTotalPackets() +
           Bot.ResourceModel->getTotalPackets();
  }

protected:
  /// Create a VLIW resource model for \p STI and \p SchedModel.
  /// @param STI Subtarget used to construct the packetizer.
  /// @param SchedModel Target scheduling model for the resource model.
  /// @return A newly allocated VLIW resource model for the given subtarget.
  virtual VLIWResourceModel *
  createVLIWResourceModel(const TargetSubtargetInfo &STI,
                          const TargetSchedModel *SchedModel) const;

  /// Pick a node by comparing top and bottom candidates bidirectionally.
  /// @param IsTopNode Set to true if the chosen node comes from the top queue.
  /// @return The chosen scheduling unit, or null if neither queue has a pick.
  SUnit *pickNodeBidrectional(bool &IsTopNode);

  /// Return the pressure-change contribution of scheduling \p SU.
  /// @param SU Scheduling unit whose pressure impact is evaluated.
  /// @param isBotUp True when evaluating pressure for bottom-up scheduling.
  /// @return The pressure-change contribution of scheduling \p SU.
  int pressureChange(const SUnit *SU, bool isBotUp);

  /// Compute the scheduling cost of \p SU relative to \p Candidate.
  /// @param Q Ready queue that currently owns \p SU.
  /// @param SU Scheduling unit whose cost is computed.
  /// @param Candidate Best candidate found so far in this pick.
  /// @param Delta Register-pressure delta for \p SU.
  /// @param verbose When true, emit verbose debug cost tracing.
  /// @return The computed scheduling cost for \p SU.
  virtual int SchedulingCost(ReadyQueue &Q, SUnit *SU,
                             SchedCandidate &Candidate, RegPressureDelta &Delta,
                             bool verbose);

  /// Select the best candidate from \p Zone into \p Candidate.
  /// @param Zone Scheduling boundary whose Available queue is searched.
  /// @param RPTracker Register-pressure tracker for the current region.
  /// @param Candidate Best candidate updated in place.
  /// @return The candidate-selection result describing why the pick won.
  CandResult pickNodeFromQueue(VLIWSchedBoundary &Zone,
                               const RegPressureTracker &RPTracker,
                               SchedCandidate &Candidate);
#ifndef NDEBUG
  /// Trace why \p SU was considered as a scheduling candidate.
  /// @param Label Short label describing the candidate decision.
  /// @param Q Ready queue that contained \p SU.
  /// @param SU Scheduling unit being traced.
  /// @param Cost Computed scheduling cost for \p SU.
  /// @param P Optional pressure change associated with the candidate.
  void traceCandidate(const char *Label, const ReadyQueue &Q, SUnit *SU,
                      int Cost, PressureChange P = PressureChange());

  /// Dump the ready queue and pressure state for verbose misched debugging.
  /// @param RPTracker Register-pressure tracker for the current region.
  /// @param Candidate Current best candidate being compared.
  /// @param Q Ready queue whose contents are dumped.
  void readyQueueVerboseDump(const RegPressureTracker &RPTracker,
                             SchedCandidate &Candidate, ReadyQueue &Q);
#endif
};

} // end namespace llvm

#endif // LLVM_CODEGEN_VLIWMACHINESCHEDULER_H
