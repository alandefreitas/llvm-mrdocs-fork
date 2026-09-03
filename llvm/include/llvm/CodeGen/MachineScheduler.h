//===- MachineScheduler.h - MachineInstr Scheduling Pass --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an interface for customizing the standard MachineScheduler
// pass. Note that the entire pass may be replaced as follows:
//
// <Target>TargetMachine::createPassConfig(PassManagerBase &PM) {
//   PM.substitutePass(&MachineSchedulerID, &CustomSchedulerPassID);
//   ...}
//
// The MachineScheduler pass is only responsible for choosing the regions to be
// scheduled. Targets can override the DAG builder and scheduler without
// replacing the pass as follows:
//
// ScheduleDAGInstrs *<Target>TargetMachine::
// createMachineScheduler(MachineSchedContext *C) {
//   return new CustomMachineScheduler(C);
// }
//
// The default scheduler, ScheduleDAGMILive, builds the DAG and drives list
// scheduling while updating the instruction stream, register pressure, and live
// intervals. Most targets don't need to override the DAG builder and list
// scheduler, but subtargets that require custom scheduling heuristics may
// plugin an alternate MachineSchedStrategy. The strategy is responsible for
// selecting the highest priority node from the list:
//
// ScheduleDAGInstrs *<Target>TargetMachine::
// createMachineScheduler(MachineSchedContext *C) {
//   return new ScheduleDAGMILive(C, CustomStrategy(C));
// }
//
// The DAG builder can also be customized in a sense by adding DAG mutations
// that will run after DAG building and before list scheduling. DAG mutations
// can adjust dependencies based on target-specific knowledge or add weak edges
// to aid heuristics:
//
// ScheduleDAGInstrs *<Target>TargetMachine::
// createMachineScheduler(MachineSchedContext *C) {
//   ScheduleDAGMI *DAG = createSchedLive(C);
//   DAG->addMutation(new CustomDAGMutation(...));
//   return DAG;
// }
//
// A target that supports alternative schedulers can use the
// MachineSchedRegistry to allow command line selection. This can be done by
// implementing the following boilerplate:
//
// static ScheduleDAGInstrs *createCustomMachineSched(MachineSchedContext *C) {
//  return new CustomMachineScheduler(C);
// }
// static MachineSchedRegistry
// SchedCustomRegistry("custom", "Run my target's custom scheduler",
//                     createCustomMachineSched);
//
//
// Finally, subtargets that don't need to implement custom heuristics but would
// like to configure the GenericScheduler's policy for a given scheduler region,
// including scheduling direction and register pressure tracking policy, can do
// this:
//
// void <SubTarget>Subtarget::
// overrideSchedPolicy(MachineSchedPolicy &Policy,
//                     const SchedRegion &Region) const {
//   Policy.<Flag> = true;
// }
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINESCHEDULER_H
#define LLVM_CODEGEN_MACHINESCHEDULER_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachinePassRegistry.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
/// Implementation details for the new-pass-manager machine scheduler wrappers.
namespace impl_detail {
// FIXME: Remove these declarations once RegisterClassInfo is queryable as an
// analysis.
/// Opaque implementation of the pre-RA MachineScheduler pass.
class MachineSchedulerImpl;
/// Opaque implementation of the post-RA PostMachineScheduler pass.
class PostMachineSchedulerImpl;
} // namespace impl_detail

/// Options controlling machine-instruction scheduling direction.
namespace MISched {
/// Direction in which the machine instruction scheduler walks a region.
enum Direction {
  /// Let the scheduler or subtarget choose the direction.
  Unspecified,
  /// Schedule from the top of the region downward.
  TopDown,
  /// Schedule from the bottom of the region upward.
  BottomUp,
  /// Schedule from both ends of the region toward the middle.
  Bidirectional,
};
} // namespace MISched

/// Command-line override for pre-RA scheduling direction.
LLVM_ABI extern cl::opt<MISched::Direction> PreRADirection;
/// When true, verify the schedule after each region is scheduled.
LLVM_ABI extern cl::opt<bool> VerifyScheduling;

#ifndef NDEBUG
/// When true, open a Graphviz view of each MISched DAG.
extern cl::opt<bool> ViewMISchedDAGs;
/// When true, print each MISched DAG to the debug stream.
extern cl::opt<bool> PrintDAGs;
#else
/// Always false in release builds; Graphviz MISched DAG viewing is disabled.
LLVM_ABI extern const bool ViewMISchedDAGs;
/// Always false in release builds; MISched DAG printing is disabled.
LLVM_ABI extern const bool PrintDAGs;
#endif

class AAResults;
class LiveIntervals;
class MachineFunction;
class MachineInstr;
class MachineLoopInfo;
class RegisterClassInfo;
class SchedDFSResult;
class TargetInstrInfo;
class TargetPassConfig;
class TargetRegisterInfo;

/// Context from the MachineScheduler pass used to instantiate a scheduler.
struct LLVM_ABI MachineSchedContext {
  /// Machine function currently being scheduled.
  MachineFunction *MF = nullptr;
  /// Loop information for the current machine function.
  const MachineLoopInfo *MLI = nullptr;
  /// Target machine for the current compilation.
  const TargetMachine *TM = nullptr;
  /// Alias analysis results used while building the schedule DAG.
  AAResults *AA = nullptr;
  /// Live interval information for virtual registers.
  LiveIntervals *LIS = nullptr;
  /// Block frequency information for the current machine function.
  MachineBlockFrequencyInfo *MBFI = nullptr;

  /// Cached register-class information for the current function.
  RegisterClassInfo *RegClassInfo = nullptr;

  /// Construct an empty scheduling context.
  MachineSchedContext();
  /// Assignment is deleted; MachineSchedContext is not copyable.
  /// @param other Unused; copy assignment is deleted.
  MachineSchedContext &operator=(const MachineSchedContext &other) = delete;
  /// Copy construction is deleted; MachineSchedContext is not copyable.
  /// @param other Unused; copy construction is deleted.
  MachineSchedContext(const MachineSchedContext &other) = delete;
  /// Destroy the scheduling context and owned register-class info.
  virtual ~MachineSchedContext();
};

/// Registry of available machine instruction schedulers.
class MachineSchedRegistry
    : public MachinePassRegistryNode<
          ScheduleDAGInstrs *(*)(MachineSchedContext *)> {
public:
  /// Constructor type that builds a ScheduleDAGInstrs from pass context.
  using ScheduleDAGCtor = ScheduleDAGInstrs *(*)(MachineSchedContext *);

  /// Alias required by RegisterPassParser (historically misnamed).
  using FunctionPassCtor = ScheduleDAGCtor;

  /// Global registry of machine instruction scheduler constructors.
  LLVM_ABI static MachinePassRegistry<ScheduleDAGCtor> Registry;

  /// Register a scheduler named \p N with description \p D and ctor \p C.
  /// @param N Command-line name for this scheduler.
  /// @param D Human-readable description of this scheduler.
  /// @param C Factory that constructs the ScheduleDAGInstrs.
  MachineSchedRegistry(const char *N, const char *D, ScheduleDAGCtor C)
      : MachinePassRegistryNode(N, D, C) {
    Registry.Add(this);
  }

  /// Remove this scheduler from the global registry.
  ~MachineSchedRegistry() { Registry.Remove(this); }

  /// Return the next registry entry in the linked list.
  /// @return The next registry entry in the linked list.
  MachineSchedRegistry *getNext() const {
    return (MachineSchedRegistry *)MachinePassRegistryNode::getNext();
  }

  /// Return the head of the global machine scheduler registry list.
  /// @return The head of the global machine scheduler registry list.
  static MachineSchedRegistry *getList() {
    return (MachineSchedRegistry *)Registry.getList();
  }

  /// Install \p L as the listener notified when registry entries change.
  /// @param L Listener invoked when constructors are added or removed.
  static void setListener(MachinePassRegistryListener<FunctionPassCtor> *L) {
    Registry.setListener(L);
  }
};

class ScheduleDAGMI;

/// Generic scheduling policy for targets without a custom MachineSchedStrategy.
///
/// This can be overriden for each scheduling region before building the DAG.
struct MachineSchedPolicy {
  /// Whether register pressure should be tracked while scheduling.
  bool ShouldTrackPressure = false;
  /// Track LaneMasks to allow reordering of independent subregister writes
  /// of the same vreg. \sa MachineSchedStrategy::shouldTrackLaneMasks()
  bool ShouldTrackLaneMasks = false;

  /// Force top-down-only scheduling when true.
  bool OnlyTopDown = false;
  /// Force bottom-up-only scheduling when true.
  bool OnlyBottomUp = false;

  /// Disable the heuristic that prefers nodes on long dependency chains.
  bool DisableLatencyHeuristic = false;

  /// Compute a DFSResult for use in scheduling heuristics.
  bool ComputeDFSResult = false;

  /// Bias additional physreg-def cases toward their users when enabled.
  bool BiasPRegsExtra = false;

  /// Construct a policy with all flags at their defaults.
  MachineSchedPolicy() = default;
};

/// A region of an MBB for scheduling.
struct SchedRegion {
  /// First instruction in the scheduling region.
  ///
  /// RegionEnd is either MBB->end() or the scheduling boundary after the last
  /// instruction in the scheduling region. These iterators cannot refer to
  /// instructions outside of the identified scheduling region because those may
  /// be reordered before scheduling this region.
  MachineBasicBlock::iterator RegionBegin;
  /// End iterator of the scheduling region (past the last scheduled instr).
  MachineBasicBlock::iterator RegionEnd;
  /// Number of instructions in this scheduling region.
  unsigned NumRegionInstrs;

  /// Construct a region from iterators \p B..\p E with \p N instructions.
  /// @param B First instruction in the region.
  /// @param E End iterator of the region.
  /// @param N Number of instructions in the region.
  SchedRegion(MachineBasicBlock::iterator B, MachineBasicBlock::iterator E,
              unsigned N)
      : RegionBegin(B), RegionEnd(E), NumRegionInstrs(N) {}
};

/// Interface to the scheduling algorithm used by ScheduleDAGMI.
///
/// Initialization sequence:
///   initPolicy -> shouldTrackPressure -> initialize(DAG) -> registerRoots
class LLVM_ABI MachineSchedStrategy {
  virtual void anchor();

public:
  /// Destroy the scheduling strategy.
  virtual ~MachineSchedStrategy() = default;

  /// Optionally override the per-region scheduling policy.
  /// @param Begin First instruction in the region.
  /// @param End End iterator of the region.
  /// @param NumRegionInstrs Number of instructions in the region.
  virtual void initPolicy(MachineBasicBlock::iterator Begin,
                          MachineBasicBlock::iterator End,
                          unsigned NumRegionInstrs) {}

  /// Return the current per-region scheduling policy.
  /// @return The current per-region scheduling policy.
  virtual MachineSchedPolicy getPolicy() const { return {}; }
  /// Dump the current scheduling policy to the debug stream.
  virtual void dumpPolicy() const {}

  /// Check if pressure tracking is needed before building the DAG and
  /// initializing this strategy. Called after initPolicy.
  /// @return True if pressure tracking is needed before building the DAG.
  virtual bool shouldTrackPressure() const { return true; }

  /// Return true if lane masks should be tracked while scheduling.
  ///
  /// LaneMask tracking is necessary to reorder independent subregister defs for
  /// the same vreg. This has to be enabled in combination with
  /// shouldTrackPressure().
  /// @return True if lane masks should be tracked while scheduling.
  virtual bool shouldTrackLaneMasks() const { return false; }

  /// Return true to process MBB scheduling regions top-down.
  ///
  /// When true, handling of the scheduling regions themselves (in case of a
  /// scheduling boundary in MBB) begins with the topmost region of MBB.
  /// @return True to process MBB scheduling regions top-down.
  virtual bool doMBBSchedRegionsTopDown() const { return false; }

  /// Initialize the strategy after building the DAG for a new region.
  /// @param DAG Schedule DAG for the current region.
  virtual void initialize(ScheduleDAGMI *DAG) = 0;

  /// Tell the strategy that MBB is about to be processed.
  /// @param MBB Basic block about to be scheduled.
  virtual void enterMBB(MachineBasicBlock *MBB) {};

  /// Tell the strategy that current MBB is done.
  virtual void leaveMBB() {};

  /// Notify this strategy that all roots have been released (including those
  /// that depend on EntrySU or ExitSU).
  virtual void registerRoots() {}

  /// Pick the next node to schedule, or return NULL.
  ///
  /// Set IsTopNode to true to schedule the node at the top of the unscheduled
  /// region. Otherwise it will be scheduled at the bottom.
  /// @param IsTopNode Set to true if the chosen node should be scheduled top.
  /// @return The next node to schedule, or null if none remain.
  virtual SUnit *pickNode(bool &IsTopNode) = 0;

  /// Scheduler callback to notify that a new subtree is scheduled.
  /// @param SubtreeID Identifier of the scheduled subtree.
  virtual void scheduleTree(unsigned SubtreeID) {}

  /// Notify that ScheduleDAGMI has scheduled an instruction.
  ///
  /// Called after scheduled/remaining flags in the DAG nodes are updated.
  /// @param SU Scheduled unit.
  /// @param IsTopNode True if SU was scheduled from the top boundary.
  virtual void schedNode(SUnit *SU, bool IsTopNode) = 0;

  /// Free this node for top-down scheduling once predecessors are resolved.
  /// @param SU Node whose predecessors are all scheduled.
  virtual void releaseTopNode(SUnit *SU) = 0;

  /// Free this node for bottom-up scheduling once successors are resolved.
  /// @param SU Node whose successors are all scheduled.
  virtual void releaseBottomNode(SUnit *SU) = 0;
};

/// ScheduleDAG that schedules according to a MachineSchedStrategy.
///
/// This is the common functionality between PreRA and PostRA MachineScheduler.
/// It simply schedules machine instructions according to the given strategy
/// without much extra book-keeping.
class LLVM_ABI ScheduleDAGMI : public ScheduleDAGInstrs {
protected:
  /// Alias analysis used while building memory dependencies.
  AAResults *AA;
  /// Live intervals used by DAG mutations and pressure tracking.
  LiveIntervals *LIS;
  /// Block frequencies used by scheduling heuristics.
  MachineBlockFrequencyInfo *MBFI;
  /// Strategy that selects the next node to schedule.
  std::unique_ptr<MachineSchedStrategy> SchedImpl;

  /// Ordered list of DAG postprocessing steps.
  std::vector<std::unique_ptr<ScheduleDAGMutation>> Mutations;

  /// The top of the unscheduled zone.
  MachineBasicBlock::iterator CurrentTop;

  /// The bottom of the unscheduled zone.
  MachineBasicBlock::iterator CurrentBottom;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  /// The number of instructions scheduled so far. Used to cut off the
  /// scheduler at the point determined by misched-cutoff.
  unsigned NumInstrsScheduled = 0;
#endif

public:
  /// Construct a DAG scheduler for context \p C using strategy \p S.
  /// @param C Pass context providing MF, AA, LIS, and related analyses.
  /// @param S Scheduling strategy that owns node selection heuristics.
  /// @param RemoveKillFlags Whether to clear kill flags while scheduling.
  ScheduleDAGMI(MachineSchedContext *C, std::unique_ptr<MachineSchedStrategy> S,
                bool RemoveKillFlags)
      : ScheduleDAGInstrs(*C->MF, C->MLI, RemoveKillFlags), AA(C->AA),
        LIS(C->LIS), MBFI(C->MBFI), SchedImpl(std::move(S)) {}

  /// Destroy the schedule DAG and owned strategy.
  ~ScheduleDAGMI() override;

  /// Return true to process MBB scheduling regions top-down.
  ///
  /// When true, handling of the scheduling regions themselves (in case of a
  /// scheduling boundary in MBB) begins with the topmost region of MBB.
  /// @return True to process MBB scheduling regions top-down.
  bool doMBBSchedRegionsTopDown() const override {
    return SchedImpl->doMBBSchedRegionsTopDown();
  }

  /// Return the LiveIntervals instance for use in DAG mutators and such.
  /// @return The LiveIntervals instance for use in DAG mutators and such.
  LiveIntervals *getLIS() const { return LIS; }

  /// Return true if this DAG supports VReg liveness and RegPressure.
  /// @return True if this DAG supports VReg liveness and RegPressure.
  virtual bool hasVRegLiveness() const { return false; }

  /// Add a postprocessing step to the DAG builder.
  ///
  /// Mutations are applied in the order that they are added after normal DAG
  /// building and before MachineSchedStrategy initialization.
  /// ScheduleDAGMI takes ownership of the Mutation object.
  /// @param Mutation DAG mutation to apply after building the graph.
  void addMutation(std::unique_ptr<ScheduleDAGMutation> Mutation) {
    if (Mutation)
      Mutations.push_back(std::move(Mutation));
  }

  /// Return an iterator to the top of the unscheduled zone.
  /// @return An iterator to the top of the unscheduled zone.
  MachineBasicBlock::iterator top() const { return CurrentTop; }
  /// Return an iterator to the bottom of the unscheduled zone.
  /// @return An iterator to the bottom of the unscheduled zone.
  MachineBasicBlock::iterator bottom() const { return CurrentBottom; }

  /// Prepare the next scheduling region within a basic block.
  ///
  /// This covers all instructions in a block, while schedule() may only cover a
  /// subset.
  /// @param bb Basic block containing the region.
  /// @param begin First instruction in the region.
  /// @param end End iterator of the region.
  /// @param regioninstrs Number of instructions in the region.
  void enterRegion(MachineBasicBlock *bb,
                   MachineBasicBlock::iterator begin,
                   MachineBasicBlock::iterator end,
                   unsigned regioninstrs) override;

  /// Implement ScheduleDAGInstrs interface for scheduling a sequence of
  /// reorderable instructions.
  void schedule() override;

  /// Begin scheduling within basic block \p bb.
  /// @param bb Basic block about to be scheduled.
  void startBlock(MachineBasicBlock *bb) override;
  /// Finish scheduling for the current basic block.
  void finishBlock() override;

  /// Change the position of an instruction within the basic block and update
  /// live ranges and region boundary iterators.
  /// @param MI Instruction to move.
  /// @param InsertPos New insertion point within the basic block.
  void moveInstruction(MachineInstr *MI, MachineBasicBlock::iterator InsertPos);

  /// View the schedule DAG with the given name and title.
  /// @param Name Graph name used by the viewer.
  /// @param Title Window title for the graph view.
  void viewGraph(const Twine &Name, const Twine &Title) override;
  /// View the schedule DAG with a default name and title.
  void viewGraph() override;

protected:
  // Top-Level entry points for the schedule() driver...

  /// Apply each ScheduleDAGMutation step in order. This allows different
  /// instances of ScheduleDAGMI to perform custom DAG postprocessing.
  void postProcessDAG();

  /// Release ExitSU predecessors and setup scheduler queues.
  /// @param TopRoots Roots available for top-down scheduling.
  /// @param BotRoots Roots available for bottom-up scheduling.
  void initQueues(ArrayRef<SUnit*> TopRoots, ArrayRef<SUnit*> BotRoots);

  /// Update scheduler DAG and queues after scheduling an instruction.
  /// @param SU Newly scheduled unit.
  /// @param IsTopNode True if SU was scheduled from the top boundary.
  void updateQueues(SUnit *SU, bool IsTopNode);

  /// Reinsert debug_values recorded in ScheduleDAGInstrs::DbgValues.
  void placeDebugValues();

  /// dump the scheduled Sequence.
  void dumpSchedule() const;
  /// Print execution trace of the schedule top-down or bottom-up.
  void dumpScheduleTraceTopDown() const;
  /// Print an execution trace of the schedule bottom-up.
  void dumpScheduleTraceBottomUp() const;

  /// Return false if the misched-cutoff limit has been reached.
  /// @return False if the misched-cutoff limit has been reached.
  bool checkSchedLimit();

  /// Find top/bottom roots and bias weak edges before queue setup.
  /// @param TopRoots Filled with roots for top-down scheduling.
  /// @param BotRoots Filled with roots for bottom-up scheduling.
  void findRootsAndBiasEdges(SmallVectorImpl<SUnit*> &TopRoots,
                             SmallVectorImpl<SUnit*> &BotRoots);

  /// Release successor edge \p SuccEdge of \p SU after scheduling.
  /// @param SU Scheduled predecessor unit.
  /// @param SuccEdge Successor dependence being released.
  void releaseSucc(SUnit *SU, SDep *SuccEdge);
  /// Release all successors of \p SU after it is scheduled.
  /// @param SU Scheduled unit whose successors should be released.
  void releaseSuccessors(SUnit *SU);
  /// Release predecessor edge \p PredEdge of \p SU after scheduling.
  /// @param SU Scheduled successor unit.
  /// @param PredEdge Predecessor dependence being released.
  void releasePred(SUnit *SU, SDep *PredEdge);
  /// Release all predecessors of \p SU after it is scheduled.
  /// @param SU Scheduled unit whose predecessors should be released.
  void releasePredecessors(SUnit *SU);
};

/// ScheduleDAG that tracks live intervals and register pressure.
class LLVM_ABI ScheduleDAGMILive : public ScheduleDAGMI {
protected:
  /// Cached register-class information for pressure heuristics.
  RegisterClassInfo *RegClassInfo;

  /// Information about DAG subtrees. If DFSResult is NULL, then SchedulerTrees
  /// will be empty.
  SchedDFSResult *DFSResult = nullptr;
  /// Bitmask of DAG subtrees that have already been scheduled.
  BitVector ScheduledTrees;

  /// End of the live scheduling region used for pressure tracking.
  MachineBasicBlock::iterator LiveRegionEnd;

  /// Maps vregs to the SUnits of their uses in the current scheduling region.
  VReg2SUnitMultiMap VRegUses;

  /// Per-SUnit pressure-change summaries used during bottom-up scheduling.
  ///
  /// Top-down scheduling may proceed but has no affect on the pressure diffs.
  PressureDiffs SUPressureDiffs;

  /// Register pressure in this region computed by initRegPressure.
  bool ShouldTrackPressure = false;
  /// Whether lane masks are tracked for independent subregister writes.
  bool ShouldTrackLaneMasks = false;
  /// Register pressure for the entire scheduling region before scheduling.
  IntervalPressure RegPressure;
  /// Tracker covering pressure for the entire DAG region.
  RegPressureTracker RPTracker;

  /// Pressure sets that already exceed the target limit before scheduling.
  ///
  /// Listed in increasing set ID order. Each pressure set is paired with its
  /// max pressure in the currently scheduled regions.
  std::vector<PressureChange> RegionCriticalPSets;

  /// The top of the unscheduled zone.
  IntervalPressure TopPressure;
  /// Pressure tracker for instructions scheduled from the top.
  RegPressureTracker TopRPTracker;

  /// The bottom of the unscheduled zone.
  IntervalPressure BotPressure;
  /// Pressure tracker for instructions scheduled from the bottom.
  RegPressureTracker BotRPTracker;

public:
  /// Construct a live schedule DAG for context \p C using strategy \p S.
  /// @param C Pass context providing MF, register-class info, and analyses.
  /// @param S Scheduling strategy that owns node selection heuristics.
  ScheduleDAGMILive(MachineSchedContext *C,
                    std::unique_ptr<MachineSchedStrategy> S)
      : ScheduleDAGMI(C, std::move(S), /*RemoveKillFlags=*/false),
        RegClassInfo(C->RegClassInfo), RPTracker(RegPressure),
        TopRPTracker(TopPressure), BotRPTracker(BotPressure) {}

  /// Destroy the live schedule DAG and owned DFS result.
  ~ScheduleDAGMILive() override;

  /// Return true if this DAG supports VReg liveness and RegPressure.
  /// @return Always true for ScheduleDAGMILive.
  bool hasVRegLiveness() const override { return true; }

  /// Return true if register pressure tracking is enabled.
  /// @return True if register pressure tracking is enabled.
  bool isTrackingPressure() const { return ShouldTrackPressure; }

  /// Get current register pressure for the top scheduled instructions.
  /// @return Current register pressure for the top scheduled instructions.
  const IntervalPressure &getTopPressure() const { return TopPressure; }
  /// Return the pressure tracker for the top scheduled boundary.
  /// @return The pressure tracker for the top scheduled boundary.
  const RegPressureTracker &getTopRPTracker() const { return TopRPTracker; }

  /// Get current register pressure for the bottom scheduled instructions.
  /// @return Current register pressure for the bottom scheduled instructions.
  const IntervalPressure &getBotPressure() const { return BotPressure; }
  /// Return the pressure tracker for the bottom scheduled boundary.
  /// @return The pressure tracker for the bottom scheduled boundary.
  const RegPressureTracker &getBotRPTracker() const { return BotRPTracker; }

  /// Get register pressure for the entire scheduling region before scheduling.
  /// @return Register pressure for the entire scheduling region before scheduling.
  const IntervalPressure &getRegPressure() const { return RegPressure; }

  /// Return pressure sets that were critical before scheduling.
  /// @return Pressure sets that were critical before scheduling.
  const std::vector<PressureChange> &getRegionCriticalPSets() const {
    return RegionCriticalPSets;
  }

  /// Return the mutable pressure diff for scheduling unit \p SU.
  /// @param SU Scheduling unit whose pressure delta is requested.
  /// @return The mutable pressure diff for scheduling unit SU.
  PressureDiff &getPressureDiff(const SUnit *SU) {
    return SUPressureDiffs[SU->NodeNum];
  }
  /// Return the const pressure diff for scheduling unit \p SU.
  /// @param SU Scheduling unit whose pressure delta is requested.
  /// @return The const pressure diff for scheduling unit SU.
  const PressureDiff &getPressureDiff(const SUnit *SU) const {
    return SUPressureDiffs[SU->NodeNum];
  }

  /// Compute a DFSResult after DAG building is complete, and before any
  /// queue comparisons.
  void computeDFSResult();

  /// Return a non-null DFS result if the scheduling strategy initialized it.
  /// @return A non-null DFS result if the scheduling strategy initialized it.
  const SchedDFSResult *getDFSResult() const { return DFSResult; }

  /// Return the bitmask of scheduled DAG subtrees.
  /// @return The bitmask of scheduled DAG subtrees.
  BitVector &getScheduledTrees() { return ScheduledTrees; }

  /// Prepare the next scheduling region within a basic block.
  ///
  /// This covers all instructions in a block, while schedule() may only cover a
  /// subset.
  /// @param bb Basic block containing the region.
  /// @param begin First instruction in the region.
  /// @param end End iterator of the region.
  /// @param regioninstrs Number of instructions in the region.
  void enterRegion(MachineBasicBlock *bb,
                   MachineBasicBlock::iterator begin,
                   MachineBasicBlock::iterator end,
                   unsigned regioninstrs) override;

  /// Implement ScheduleDAGInstrs interface for scheduling a sequence of
  /// reorderable instructions.
  void schedule() override;

  /// Compute the cyclic critical path through the DAG.
  /// @return The cyclic critical path length through the DAG.
  unsigned computeCyclicCriticalPath();

  /// Dump the live schedule DAG to the debug stream.
  void dump() const override;

protected:
  // Top-Level entry points for the schedule() driver...

  /// Build the schedule DAG with register pressure tracking enabled.
  ///
  /// Calls ScheduleDAGInstrs::buildSchedGraph with pressure tracking. This sets
  /// up three trackers. RPTracker covers the entire DAG region; TopTracker and
  /// BottomTracker are initialized to the top and bottom of the DAG region
  /// without covering any unscheduled instruction.
  void buildDAGWithRegPressure();

  /// Release ExitSU predecessors and setup scheduler queues.
  ///
  /// Re-positions the Top RP tracker in case the region beginning has changed.
  /// @param TopRoots Roots available for top-down scheduling.
  /// @param BotRoots Roots available for bottom-up scheduling.
  void initQueues(ArrayRef<SUnit*> TopRoots, ArrayRef<SUnit*> BotRoots);

  /// Move an instruction and update register pressure.
  /// @param SU Scheduling unit being placed into the instruction stream.
  /// @param IsTopNode True if SU is scheduled from the top boundary.
  void scheduleMI(SUnit *SU, bool IsTopNode);

  // Lesser helpers...

  /// Initialize register pressure trackers for the current region.
  void initRegPressure();

  /// Update pressure diffs for the given live uses.
  /// @param LiveUses Live virtual registers whose pressure diffs need update.
  void updatePressureDiffs(ArrayRef<VRegMaskOrUnit> LiveUses);

  /// Update scheduled pressure after placing \p SU.
  /// @param SU Newly scheduled unit.
  /// @param NewMaxPressure Updated maximum pressure per pressure set.
  void updateScheduledPressure(const SUnit *SU,
                               const std::vector<unsigned> &NewMaxPressure);

  /// Record virtual-register uses of \p SU in the current region.
  /// @param SU Scheduling unit whose vreg uses should be collected.
  void collectVRegUses(SUnit &SU);
};

//===----------------------------------------------------------------------===//
///
/// Helpers for implementing custom MachineSchedStrategy classes. These take
/// care of the book-keeping associated with list scheduling heuristics.
///
//===----------------------------------------------------------------------===//

/// Vector of ready SUnits with helpers for push and remove.
///
/// ReadyQueues are uniquely identified by an ID. SUnit::NodeQueueId is a mask
/// of the ReadyQueues the SUnit is in. This is a convenience class that may be
/// used by implementations of MachineSchedStrategy.
class ReadyQueue {
  unsigned ID;
  std::string Name;
  std::vector<SUnit*> Queue;

public:
  /// Construct a ready queue with identifier \p id and name \p name.
  /// @param id Unique bit used in SUnit::NodeQueueId masks.
  /// @param name Debug name for this queue.
  ReadyQueue(unsigned id, const Twine &name): ID(id), Name(name.str()) {}

  /// Return the unique identifier of this ready queue.
  /// @return The unique identifier of this ready queue.
  unsigned getID() const { return ID; }

  /// Return the debug name of this ready queue.
  /// @return The debug name of this ready queue.
  StringRef getName() const { return Name; }

  /// Return true if \p SU is currently in this queue.
  /// @param SU Scheduling unit to test for membership.
  /// @return True if SU is currently in this queue.
  bool isInQueue(SUnit *SU) const { return (SU->NodeQueueId & ID); }

  /// Return true if the ready queue contains no units.
  /// @return True if the ready queue contains no units.
  bool empty() const { return Queue.empty(); }

  /// Remove all scheduling units from the ready queue.
  void clear() { Queue.clear(); }

  /// Return the number of scheduling units in the ready queue.
  /// @return The number of scheduling units in the ready queue.
  unsigned size() const { return Queue.size(); }

  /// Iterator over the scheduling units in this ready queue.
  using iterator = std::vector<SUnit*>::iterator;

  /// Return an iterator to the first scheduling unit.
  /// @return An iterator to the first scheduling unit.
  iterator begin() { return Queue.begin(); }

  /// Return a past-the-end iterator over scheduling units.
  /// @return A past-the-end iterator over scheduling units.
  iterator end() { return Queue.end(); }

  /// Return the scheduling units as an ArrayRef.
  /// @return The scheduling units as an ArrayRef.
  ArrayRef<SUnit*> elements() { return Queue; }

  /// Return an iterator to \p SU, or end() if it is not present.
  /// @param SU Scheduling unit to search for.
  /// @return An iterator to SU, or end() if it is not present.
  iterator find(SUnit *SU) { return llvm::find(Queue, SU); }

  /// Append \p SU to the ready queue and mark its NodeQueueId.
  /// @param SU Scheduling unit becoming ready.
  void push(SUnit *SU) {
    Queue.push_back(SU);
    SU->NodeQueueId |= ID;
  }

  /// Remove the unit at \p I and return an iterator to the next element.
  /// @param I Iterator to the unit being removed.
  /// @return An iterator to the next element after removal.
  iterator remove(iterator I) {
    (*I)->NodeQueueId &= ~ID;
    *I = Queue.back();
    unsigned idx = I - Queue.begin();
    Queue.pop_back();
    return Queue.begin() + idx;
  }

  /// Dump the contents of this ready queue to the debug stream.
  LLVM_ABI void dump() const;
};

/// Summarize the unscheduled region.
struct SchedRemainder {
  /// Critical path through the DAG in expected latency.
  unsigned CriticalPath;
  /// Cyclic critical path through the DAG in expected latency.
  unsigned CyclicCritPath;

  /// Scaled count of micro-ops left to schedule.
  unsigned RemIssueCount;

  /// True when remaining latency dominates remaining resources.
  bool IsAcyclicLatencyLimited;

  /// Remaining counts for each processor resource.
  SmallVector<unsigned, 16> RemainingCounts;

  /// Construct a remainder and reset all counters.
  SchedRemainder() { reset(); }

  /// Clear all remainder counters for a new scheduling region.
  void reset() {
    CriticalPath = 0;
    CyclicCritPath = 0;
    RemIssueCount = 0;
    IsAcyclicLatencyLimited = false;
    RemainingCounts.clear();
  }

  /// Initialize remainder counts from \p DAG and \p SchedModel.
  /// @param DAG Schedule DAG providing remaining nodes and edges.
  /// @param SchedModel Target scheduling model for resource scaling.
  LLVM_ABI void init(ScheduleDAGMI *DAG, const TargetSchedModel *SchedModel);
};

/// ResourceSegments are a collection of intervals closed on the
/// left and opened on the right:
///
///     list{ [a1, b1), [a2, b2), ..., [a_N, b_N) }
///
/// The collection has the following properties:
///
/// 1. The list is ordered: a_i < b_i and b_i < a_(i+1)
///
/// 2. The intervals in the collection do not intersect each other.
///
/// A \ref ResourceSegments instance represents the cycle
/// reservation history of the instance of and individual resource.
class ResourceSegments {
public:
  /// Represents an interval of discrete integer values closed on
  /// the left and open on the right: [a, b).
  typedef std::pair<int64_t, int64_t> IntervalTy;

  /// Adds an interval [a, b) to the collection of the instance.
  ///
  /// When adding [a, b[ to the collection, the operation merges the
  /// adjacent intervals. For example
  ///
  ///       0  1  2  3  4  5  6  7  8  9  10
  ///       [-----)  [--)     [--)
  ///     +       [--)
  ///     = [-----------)     [--)
  ///
  /// To be able to debug duplicate resource usage, the function has
  /// assertion that checks that no interval should be added if it
  /// overlaps any of the intervals in the collection. We can
  /// require this because by definition a \ref ResourceSegments is
  /// attached only to an individual resource instance.
  /// @param A Interval to reserve on this resource instance.
  /// @param CutOff Maximum number of intervals retained after merges.
  LLVM_ABI void add(IntervalTy A, const unsigned CutOff = 10);

public:
  /// Return true if intervals \p A and \p B intersect.
  /// @param A First half-open interval.
  /// @param B Second half-open interval.
  /// @return True if intervals A and B intersect.
  LLVM_ABI static bool intersects(IntervalTy A, IntervalTy B);

  /// These function return the interval used by a resource in bottom and top
  /// scheduling.
  ///
  /// Consider an instruction that uses resources X0, X1 and X2 as follows:
  ///
  /// X0 X1 X1 X2    +--------+-------------+--------------+
  ///                |Resource|AcquireAtCycle|ReleaseAtCycle|
  ///                +--------+-------------+--------------+
  ///                |   X0   |     0       |       1      |
  ///                +--------+-------------+--------------+
  ///                |   X1   |     1       |       3      |
  ///                +--------+-------------+--------------+
  ///                |   X2   |     3       |       4      |
  ///                +--------+-------------+--------------+
  ///
  /// If we can schedule the instruction at cycle C, we need to
  /// compute the interval of the resource as follows:
  ///
  /// # TOP DOWN SCHEDULING
  ///
  /// Cycles scheduling flows to the _right_, in the same direction
  /// of time.
  ///
  ///       C      1      2      3      4      5  ...
  /// ------|------|------|------|------|------|----->
  ///       X0     X1     X1     X2   ---> direction of time
  /// X0    [C, C+1)
  /// X1           [C+1,      C+3)
  /// X2                         [C+3, C+4)
  ///
  /// Therefore, the formula to compute the interval for a resource
  /// of an instruction that can be scheduled at cycle C in top-down
  /// scheduling is:
  ///
  ///       [C+AcquireAtCycle, C+ReleaseAtCycle)
  ///
  ///
  /// # BOTTOM UP SCHEDULING
  ///
  /// Cycles scheduling flows to the _left_, in opposite direction
  /// of time.
  ///
  /// In bottom up scheduling, the scheduling happens in opposite
  /// direction to the execution of the cycles of the
  /// instruction. When the instruction is scheduled at cycle `C`,
  /// the resources are allocated in the past relative to `C`:
  ///
  ///       2      1      C     -1     -2     -3     -4     -5  ...
  /// <-----|------|------|------|------|------|------|------|---
  ///                     X0     X1     X1     X2   ---> direction of time
  /// X0           (C+1, C]
  /// X1                  (C,        C-2]
  /// X2                              (C-2, C-3]
  ///
  /// Therefore, the formula to compute the interval for a resource
  /// of an instruction that can be scheduled at cycle C in bottom-up
  /// scheduling is:
  ///
  ///       [C-ReleaseAtCycle+1, C-AcquireAtCycle+1)
  ///
  ///
  /// NOTE: In both cases, the number of cycles booked by a
  /// resources is the value (ReleaseAtCycle - AcquireAtCycle).
  /// @param C Cycle at which the instruction can be scheduled.
  /// @param AcquireAtCycle Relative cycle when the resource is acquired.
  /// @param ReleaseAtCycle Relative cycle when the resource is released.
  /// @return The half-open resource interval for bottom-up scheduling.
  static IntervalTy getResourceIntervalBottom(unsigned C, unsigned AcquireAtCycle,
                                              unsigned ReleaseAtCycle) {
    return std::make_pair<long, long>((long)C - (long)ReleaseAtCycle + 1L,
                                      (long)C - (long)AcquireAtCycle + 1L);
  }
  /// Compute the resource interval for top-down scheduling at cycle \p C.
  /// @param C Cycle at which the instruction can be scheduled.
  /// @param AcquireAtCycle Relative cycle when the resource is acquired.
  /// @param ReleaseAtCycle Relative cycle when the resource is released.
  /// @return The half-open resource interval for top-down scheduling.
  static IntervalTy getResourceIntervalTop(unsigned C, unsigned AcquireAtCycle,
                                           unsigned ReleaseAtCycle) {
    return std::make_pair<long, long>((long)C + (long)AcquireAtCycle,
                                      (long)C + (long)ReleaseAtCycle);
  }

private:
  /// Finds the first cycle in which a resource can be allocated.
  ///
  /// The function uses the \param IntervalBuider [*] to build a
  /// resource interval [a, b[ out of the input parameters \param
  /// CurrCycle, \param AcquireAtCycle and \param ReleaseAtCycle.
  ///
  /// The function then loops through the intervals in the ResourceSegments
  /// and shifts the interval [a, b[ and the ReturnCycle to the
  /// right until there is no intersection between the intervals of
  /// the \ref ResourceSegments instance and the new shifted [a, b[. When
  /// this condition is met, the ReturnCycle  (which
  /// correspond to the cycle in which the resource can be
  /// allocated) is returned.
  ///
  ///               c = CurrCycle in input
  ///               c   1   2   3   4   5   6   7   8   9   10 ... ---> (time
  ///               flow)
  ///  ResourceSegments...  [---)   [-------)           [-----------)
  ///               c   [1     3[  -> AcquireAtCycle=1, ReleaseAtCycle=3
  ///                 ++c   [1     3)
  ///                     ++c   [1     3)
  ///                         ++c   [1     3)
  ///                             ++c   [1     3)
  ///                                 ++c   [1     3)    ---> returns c
  ///                                 incremented by 5 (c+5)
  ///
  ///
  /// Notice that for bottom-up scheduling the diagram is slightly
  /// different because the current cycle c is always on the right
  /// of the interval [a, b) (see \ref
  /// `getResourceIntervalBottom`). This is because the cycle
  /// increments for bottom-up scheduling moved in the direction
  /// opposite to the direction of time:
  ///
  ///     --------> direction of time.
  ///     XXYZZZ    (resource usage)
  ///     --------> direction of top-down execution cycles.
  ///     <-------- direction of bottom-up execution cycles.
  ///
  /// Even though bottom-up scheduling moves against the flow of
  /// time, the algorithm used to find the first free slot in between
  /// intervals is the same as for top-down scheduling.
  ///
  /// [*] See \ref `getResourceIntervalTop` and
  /// \ref `getResourceIntervalBottom` to see how such resource intervals
  /// are built.
  LLVM_ABI unsigned getFirstAvailableAt(
      unsigned CurrCycle, unsigned AcquireAtCycle, unsigned ReleaseAtCycle,
      std::function<IntervalTy(unsigned, unsigned, unsigned)> IntervalBuilder)
      const;

public:
  /// Return the first bottom-up cycle where this resource is free.
  ///
  /// Ideally merged with getFirstAvailableAtFromTop by passing an interval
  /// builder.
  /// @param CurrCycle Current scheduling cycle.
  /// @param AcquireAtCycle Relative cycle when the resource is acquired.
  /// @param ReleaseAtCycle Relative cycle when the resource is released.
  /// @return The first bottom-up cycle where this resource is free.
  unsigned getFirstAvailableAtFromBottom(unsigned CurrCycle,
                                         unsigned AcquireAtCycle,
                                         unsigned ReleaseAtCycle) const {
    return getFirstAvailableAt(CurrCycle, AcquireAtCycle, ReleaseAtCycle,
                               getResourceIntervalBottom);
  }
  /// Return the first top-down cycle where this resource is free.
  /// @param CurrCycle Current scheduling cycle.
  /// @param AcquireAtCycle Relative cycle when the resource is acquired.
  /// @param ReleaseAtCycle Relative cycle when the resource is released.
  /// @return The first top-down cycle where this resource is free.
  unsigned getFirstAvailableAtFromTop(unsigned CurrCycle,
                                      unsigned AcquireAtCycle,
                                      unsigned ReleaseAtCycle) const {
    return getFirstAvailableAt(CurrCycle, AcquireAtCycle, ReleaseAtCycle,
                               getResourceIntervalTop);
  }

private:
  std::list<IntervalTy> _Intervals;
  /// Merge all adjacent intervals in the collection. For all pairs
  /// of adjacient intervals, it performs [a, b) + [b, c) -> [a, c).
  ///
  /// Before performing the merge operation, the intervals are
  /// sorted with \ref sort_predicate.
  LLVM_ABI void sortAndMerge();

public:
  /// Construct an empty set of resource intervals.
  explicit ResourceSegments() = default;
  /// Return true if no resource intervals are reserved.
  /// @return True if no resource intervals are reserved.
  bool empty() const { return _Intervals.empty(); }
  /// Construct segments from \p Intervals and merge adjacent ranges.
  /// @param Intervals Initial list of half-open reservation intervals.
  explicit ResourceSegments(const std::list<IntervalTy> &Intervals)
      : _Intervals(Intervals) {
    sortAndMerge();
  }

  /// Return true if \p c1 and \p c2 reserve the same intervals.
  /// @param c1 First resource-segment set.
  /// @param c2 Second resource-segment set.
  /// @return True if c1 and c2 reserve the same intervals.
  friend bool operator==(const ResourceSegments &c1,
                         const ResourceSegments &c2) {
    return c1._Intervals == c2._Intervals;
  }
  /// Print the reserved intervals of \p Segments to \p os.
  /// @param os Output stream.
  /// @param Segments Resource segments to print.
  /// @return The output stream os after printing.
  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                       const ResourceSegments &Segments) {
    os << "{ ";
    for (auto p : Segments._Intervals)
      os << "[" << p.first << ", " << p.second << "), ";
    os << "}\n";
    return os;
  }
};

/// Scheduling boundary that tracks ready queues and hazard state.
///
/// Each boundary is associated with ready queues. It tracks the current cycle
/// in the direction of movement, and maintains the state of "hazards" and other
/// interlocks at the current cycle.
class SchedBoundary {
public:
  /// SUnit::NodeQueueId: 0 (none), 1 (top), 2 (bot), 3 (both)
  enum {
    /// Ready-queue ID for the top scheduling boundary.
    TopQID = 1,
    /// Ready-queue ID for the bottom scheduling boundary.
    BotQID = 2,
    /// Log2 of the number of distinct ready-queue ID bits.
    LogMaxQID = 2
  };

  /// Schedule DAG owning the instructions in this boundary's zone.
  ScheduleDAGMI *DAG = nullptr;
  /// Target scheduling model used for latency and resource accounting.
  const TargetSchedModel *SchedModel = nullptr;
  /// Remainder of unscheduled latency and resources for the region.
  SchedRemainder *Rem = nullptr;

  /// Queue of instructions available to schedule at this boundary.
  ReadyQueue Available;
  /// Queue of instructions pending a hazard or ready-cycle.
  ReadyQueue Pending;

  /// Hazard recognizer for detecting interlocks at the current cycle.
  std::unique_ptr<ScheduleHazardRecognizer> HazardRec;

private:
  /// True if the pending Q should be checked/updated before scheduling another
  /// instruction.
  bool CheckPending;

  /// Number of cycles it takes to issue the instructions scheduled in this
  /// zone. It is defined as: scheduled-micro-ops / issue-width + stalls.
  /// See getStalls().
  unsigned CurrCycle;

  /// Micro-ops issued in the current cycle
  unsigned CurrMOps;

  /// MinReadyCycle - Cycle of the soonest available instruction.
  unsigned MinReadyCycle;

  // The expected latency of the critical path in this scheduled zone.
  unsigned ExpectedLatency;

  // The latency of dependence chains leading into this zone.
  // For each node scheduled bottom-up: DLat = max DLat, N.Depth.
  // For each cycle scheduled: DLat -= 1.
  unsigned DependentLatency;

  /// Count the scheduled (issued) micro-ops that can be retired by
  /// time=CurrCycle assuming the first scheduled instr is retired at time=0.
  unsigned RetiredMOps;

  // Count scheduled resources that have been executed. Resources are
  // considered executed if they become ready in the time that it takes to
  // saturate any resource including the one in question. Counts are scaled
  // for direct comparison with other resources. Counts can be compared with
  // MOps * getMicroOpFactor and Latency * getLatencyFactor.
  SmallVector<unsigned, 16> ExecutedResCounts;

  /// Cache the max count for a single resource.
  unsigned MaxExecutedResCount;

  // Cache the critical resources ID in this scheduled zone.
  unsigned ZoneCritResIdx;

  // Is the scheduled region resource limited vs. latency limited.
  bool IsResourceLimited;

public:
private:
  /// Record how resources have been allocated across the cycles of
  /// the execution.
  std::map<unsigned, ResourceSegments> ReservedResourceSegments;
  std::vector<unsigned> ReservedCycles;
  /// For each PIdx, stores first index into ReservedResourceSegments that
  /// corresponds to it.
  ///
  /// For example, consider the following 3 resources (ResourceCount =
  /// 3):
  ///
  ///   +------------+--------+
  ///   |ResourceName|NumUnits|
  ///   +------------+--------+
  ///   |     X      |    2   |
  ///   +------------+--------+
  ///   |     Y      |    3   |
  ///   +------------+--------+
  ///   |     Z      |    1   |
  ///   +------------+--------+
  ///
  /// In this case, the total number of resource instances is 6. The
  /// vector \ref ReservedResourceSegments will have a slot for each instance.
  /// The vector \ref ReservedCyclesIndex will track at what index the first
  /// instance of the resource is found in the vector of \ref
  /// ReservedResourceSegments:
  ///
  ///                              Indexes of instances in
  ///                              ReservedResourceSegments
  ///
  ///                              0   1   2   3   4  5
  /// ReservedCyclesIndex[0] = 0; [X0, X1,
  /// ReservedCyclesIndex[1] = 2;          Y0, Y1, Y2
  /// ReservedCyclesIndex[2] = 5;                     Z
  SmallVector<unsigned, 16> ReservedCyclesIndex;

  // For each PIdx, stores the resource group IDs of its subunits
  SmallVector<APInt, 16> ResourceGroupSubUnitMasks;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  // Remember the greatest possible stall as an upper bound on the number of
  // times we should retry the pending queue because of a hazard.
  unsigned MaxObservedStall;
#endif

public:
  /// Construct a boundary whose pending queue shares ID bits with \p ID.
  /// @param ID Ready-queue identifier (TopQID or BotQID).
  /// @param Name Debug name prefix for Available and Pending queues.
  SchedBoundary(unsigned ID, const Twine &Name):
    Available(ID, Name+".A"), Pending(ID << LogMaxQID, Name+".P") {
    reset();
  }
  /// Assignment is deleted; SchedBoundary is not copyable.
  /// @param other Unused; copy assignment is deleted.
  SchedBoundary &operator=(const SchedBoundary &other) = delete;
  /// Copy construction is deleted; SchedBoundary is not copyable.
  /// @param other Unused; copy construction is deleted.
  SchedBoundary(const SchedBoundary &other) = delete;
  /// Destroy the scheduling boundary and owned hazard recognizer.
  LLVM_ABI ~SchedBoundary();

  /// Reset cycle, queue, and resource state for a new region.
  LLVM_ABI void reset();

  /// Initialize this boundary for \p dag using \p smodel and remainder \p rem.
  /// @param dag Schedule DAG being scheduled.
  /// @param smodel Target scheduling model.
  /// @param rem Remainder tracking unscheduled latency and resources.
  LLVM_ABI void init(ScheduleDAGMI *dag, const TargetSchedModel *smodel,
                     SchedRemainder *rem);

  /// Return true if this is the top scheduling boundary.
  /// @return True if this is the top scheduling boundary.
  bool isTop() const {
    return Available.getID() == TopQID;
  }

  /// Number of cycles to issue the instructions scheduled in this zone.
  /// @return Number of cycles to issue the instructions scheduled in this zone.
  unsigned getCurrCycle() const { return CurrCycle; }

  /// Micro-ops issued in the current cycle
  /// @return Micro-ops issued in the current cycle.
  unsigned getCurrMOps() const { return CurrMOps; }

  /// Return the latency of dependence chains leading into this zone.
  /// @return Latency of dependence chains leading into this zone.
  unsigned getDependentLatency() const { return DependentLatency; }

  /// Return latency cycles covered by instructions scheduled in this zone.
  ///
  /// This is the larger of the critical path within the zone and the number of
  /// cycles required to issue the instructions.
  /// @return Latency cycles covered by instructions scheduled in this zone.
  unsigned getScheduledLatency() const {
    return std::max(ExpectedLatency, CurrCycle);
  }

  /// Return remaining latency contributed by unscheduled unit \p SU.
  /// @param SU Unscheduled unit whose height or depth is queried.
  /// @return Remaining latency contributed by unscheduled unit SU.
  unsigned getUnscheduledLatency(SUnit *SU) const {
    return isTop() ? SU->getHeight() : SU->getDepth();
  }

  /// Return the executed count for processor resource \p ResIdx.
  /// @param ResIdx Processor resource index.
  /// @return The executed count for processor resource ResIdx.
  unsigned getResourceCount(unsigned ResIdx) const {
    return ExecutedResCounts[ResIdx];
  }

  /// Get the scaled count of scheduled micro-ops and resources, including
  /// executed resources.
  /// @return Scaled count of scheduled critical micro-ops and resources.
  unsigned getCriticalCount() const {
    if (!ZoneCritResIdx)
      return RetiredMOps * SchedModel->getMicroOpFactor();
    return getResourceCount(ZoneCritResIdx);
  }

  /// Get a scaled count for the minimum execution time of the scheduled
  /// micro-ops that are ready to execute by getExecutedCount. Notice the
  /// feedback loop.
  /// @return Scaled minimum execution count for ready scheduled micro-ops.
  unsigned getExecutedCount() const {
    return std::max(CurrCycle * SchedModel->getLatencyFactor(),
                    MaxExecutedResCount);
  }

  /// Return the critical resource index for this scheduled zone.
  /// @return The critical resource index for this scheduled zone.
  unsigned getZoneCritResIdx() const { return ZoneCritResIdx; }

  /// Return true if the scheduled zone is resource-limited.
  /// @return True if the scheduled zone is resource-limited.
  bool isResourceLimited() const { return IsResourceLimited; }

  /// Get the difference between the given SUnit's ready time and the current
  /// cycle.
  /// @param SU Scheduling unit whose ready-cycle stall is computed.
  /// @return Stall cycles until SU is ready relative to the current cycle.
  LLVM_ABI unsigned getLatencyStallCycles(SUnit *SU);

  /// Return the next cycle a resource instance can be acquired.
  /// @param InstanceIndex Index of the resource instance.
  /// @param ReleaseAtCycle Relative cycle when the resource is released.
  /// @param AcquireAtCycle Relative cycle when the resource is acquired.
  /// @return The next cycle at which the resource instance can be acquired.
  LLVM_ABI unsigned getNextResourceCycleByInstance(unsigned InstanceIndex,
                                                   unsigned ReleaseAtCycle,
                                                   unsigned AcquireAtCycle);

  /// Return the next cycle and instance for processor resource \p PIdx.
  /// @param SC Scheduling class of the candidate instruction.
  /// @param PIdx Processor resource index.
  /// @param ReleaseAtCycle Relative cycle when the resource is released.
  /// @param AcquireAtCycle Relative cycle when the resource is acquired.
  /// @return A pair of the next available cycle and resource instance index.
  LLVM_ABI std::pair<unsigned, unsigned>
  getNextResourceCycle(const MCSchedClassDesc *SC, unsigned PIdx,
                       unsigned ReleaseAtCycle, unsigned AcquireAtCycle);

  /// Return true if processor resource \p PIdx is a reserved group.
  /// @param PIdx Processor resource index.
  /// @return True if processor resource PIdx is a reserved group.
  bool isReservedGroup(unsigned PIdx) const {
    return SchedModel->getProcResource(PIdx)->SubUnitsIdxBegin &&
           !SchedModel->getProcResource(PIdx)->BufferSize;
  }

  /// Return true if scheduling \p SU would hit a hazard at this cycle.
  /// @param SU Candidate scheduling unit.
  /// @return True if scheduling SU would hit a hazard at this cycle.
  LLVM_ABI bool checkHazard(SUnit *SU);

  /// Return the maximum unscheduled latency among \p ReadySUs.
  /// @param ReadySUs Ready units to scan for latency.
  /// @return The maximum unscheduled latency among ReadySUs.
  LLVM_ABI unsigned findMaxLatency(ArrayRef<SUnit *> ReadySUs);

  /// Count non-critical remaining resources and set \p OtherCritIdx.
  /// @param OtherCritIdx Set to the other zone's critical resource index.
  /// @return The scaled count of non-critical remaining resources.
  LLVM_ABI unsigned getOtherResourceCount(unsigned &OtherCritIdx);

  /// Release \p SU into the available or pending ready queue.
  ///
  /// If it's not in hazard, remove it from pending queue (if already in) and
  /// push into available queue. Otherwise, push the SU into pending queue.
  ///
  /// @param SU The unit to be released.
  /// @param ReadyCycle Until which cycle the unit is ready.
  /// @param InPQueue Whether SU is already in pending queue.
  /// @param Idx Position offset in pending queue (if in it).
  LLVM_ABI void releaseNode(SUnit *SU, unsigned ReadyCycle, bool InPQueue,
                            unsigned Idx = 0);

  /// Advance the boundary's cycle to \p NextCycle, updating stalls.
  /// @param NextCycle Cycle to advance to.
  LLVM_ABI void bumpCycle(unsigned NextCycle);

  /// Increment executed count for resource \p PIdx by \p Count.
  /// @param PIdx Processor resource index.
  /// @param Count Scaled resource count to add.
  LLVM_ABI void incExecutedResources(unsigned PIdx, unsigned Count);

  /// Account for resource \p PIdx usage starting at \p StartAtCycle.
  /// @param SC Scheduling class of the instruction.
  /// @param PIdx Processor resource index.
  /// @param Cycles Number of cycles the resource is occupied.
  /// @param ReadyCycle Cycle when the instruction becomes ready.
  /// @param StartAtCycle Cycle when resource usage begins.
  /// @return The next cycle at which the resource is available.
  LLVM_ABI unsigned countResource(const MCSchedClassDesc *SC, unsigned PIdx,
                                  unsigned Cycles, unsigned ReadyCycle,
                                  unsigned StartAtCycle);

  /// Issue \p SU at this boundary and update cycle/resource state.
  /// @param SU Scheduling unit being issued.
  LLVM_ABI void bumpNode(SUnit *SU);

  /// Move pending units that are no longer hazardous into Available.
  LLVM_ABI void releasePending();

  /// Remove \p SU from Available or Pending if present.
  /// @param SU Scheduling unit to remove from ready queues.
  LLVM_ABI void removeReady(SUnit *SU);

  /// Return the sole available candidate, or null if there are several.
  ///
  /// Call this before applying any other heuristics to the Available queue.
  /// Updates the Available/Pending Q's if necessary and returns the single
  /// available instruction, or NULL if there are multiple candidates.
  /// @return The sole available candidate, or null if there are several.
  LLVM_ABI SUnit *pickOnlyChoice();

  /// Dump the state of the information that tracks resource usage.
  LLVM_ABI void dumpReservedCycles() const;
  /// Dump scheduled cycle and resource state for this boundary.
  LLVM_ABI void dumpScheduledState() const;
};

/// Base class for GenericScheduler candidate heuristics.
///
/// Maintains information about scheduling candidates based on TargetSchedModel,
/// making it easy to implement heuristics for either preRA or postRA
/// scheduling.
class GenericSchedulerBase : public MachineSchedStrategy {
public:
  /// Represent the type of SchedCandidate found within a single queue.
  /// pickNodeBidirectional depends on these listed by decreasing priority.
  enum CandReason : uint8_t {
    /// No candidate has been selected yet.
    NoCand,
    /// Only one candidate remains in the queue.
    Only1,
    /// Prefer a candidate that resolves a physical register copy.
    PhysReg,
    /// Prefer a candidate that reduces excess register pressure.
    RegExcess,
    /// Prefer a candidate that reduces critical register pressure.
    RegCritical,
    /// Prefer a candidate that avoids a stall.
    Stall,
    /// Prefer a candidate that continues a memory cluster.
    Cluster,
    /// Prefer a candidate that satisfies a weak edge constraint.
    Weak,
    /// Prefer a candidate that improves maximum register pressure.
    RegMax,
    /// Prefer a candidate that reduces a critical resource.
    ResourceReduce,
    /// Prefer a candidate that meets demand for a critical resource.
    ResourceDemand,
    /// Prefer a bottom candidate that reduces path height.
    BotHeightReduce,
    /// Prefer a bottom candidate that reduces critical path length.
    BotPathReduce,
    /// Prefer a top candidate that reduces path depth.
    TopDepthReduce,
    /// Prefer a top candidate that reduces critical path length.
    TopPathReduce,
    /// Fall back to original node order.
    NodeOrder,
    /// First valid candidate when no stronger heuristic applies.
    FirstValid
  };

#ifndef NDEBUG
  /// Return a debug string for candidate selection reason \p Reason.
  /// @param Reason Heuristic reason used to pick a candidate.
  /// @return A debug string naming the candidate selection reason.
  static const char *getReasonStr(GenericSchedulerBase::CandReason Reason);
#endif

  /// Policy for scheduling the next instruction in the candidate's zone.
  struct CandPolicy {
    /// Prefer candidates that reduce remaining latency.
    bool ReduceLatency = false;
    /// Resource index to reduce when comparing candidates.
    unsigned ReduceResIdx = 0;
    /// Resource index in demand when comparing candidates.
    unsigned DemandResIdx = 0;

    /// Construct a policy with all resource preferences cleared.
    CandPolicy() = default;

    /// Return true if this policy equals \p RHS.
    /// @param RHS Policy to compare against.
    /// @return True if this policy equals RHS.
    bool operator==(const CandPolicy &RHS) const {
      return ReduceLatency == RHS.ReduceLatency &&
             ReduceResIdx == RHS.ReduceResIdx &&
             DemandResIdx == RHS.DemandResIdx;
    }
    /// Return true if this policy differs from \p RHS.
    /// @param RHS Policy to compare against.
    /// @return True if this policy differs from RHS.
    bool operator!=(const CandPolicy &RHS) const {
      return !(*this == RHS);
    }
  };

  /// Status of an instruction's critical resource consumption.
  struct SchedResourceDelta {
    /// Count critical resources in the scheduled region required by SU.
    unsigned CritResources = 0;

    /// Count critical resources from another region consumed by SU.
    unsigned DemandedResources = 0;

    /// Construct a zero resource delta.
    SchedResourceDelta() = default;

    /// Return true if this delta equals \p RHS.
    /// @param RHS Resource delta to compare against.
    /// @return True if this delta equals RHS.
    bool operator==(const SchedResourceDelta &RHS) const {
      return CritResources == RHS.CritResources
        && DemandedResources == RHS.DemandedResources;
    }
    /// Return true if this delta differs from \p RHS.
    /// @param RHS Resource delta to compare against.
    /// @return True if this delta differs from RHS.
    bool operator!=(const SchedResourceDelta &RHS) const {
      return !operator==(RHS);
    }
  };

  /// Store the state used by GenericScheduler heuristics, required for the
  /// lifetime of one invocation of pickNode().
  struct SchedCandidate {
    /// Policy applied while comparing this candidate.
    CandPolicy Policy;

    /// The best SUnit candidate.
    SUnit *SU;

    /// The reason for this candidate.
    CandReason Reason;

    /// Whether this candidate should be scheduled at top/bottom.
    bool AtTop;

    /// Register pressure values for the best candidate.
    RegPressureDelta RPDelta;

    /// Critical resource consumption of the best candidate.
    SchedResourceDelta ResDelta;

    /// Construct an invalid candidate with a default policy.
    SchedCandidate() { reset(CandPolicy()); }
    /// Construct an invalid candidate using \p Policy.
    /// @param Policy Candidate comparison policy.
    SchedCandidate(const CandPolicy &Policy) { reset(Policy); }

    /// Reset this candidate using \p NewPolicy and clear selection state.
    /// @param NewPolicy Policy to apply on the next comparison.
    void reset(const CandPolicy &NewPolicy) {
      Policy = NewPolicy;
      SU = nullptr;
      Reason = NoCand;
      AtTop = false;
      RPDelta = RegPressureDelta();
      ResDelta = SchedResourceDelta();
    }

    /// Return true if a scheduling unit has been selected.
    /// @return True if a scheduling unit has been selected.
    bool isValid() const { return SU; }

    /// Copy the status of another candidate without changing policy.
    /// @param Best Candidate whose selection state is copied.
    void setBest(SchedCandidate &Best) {
      assert(Best.Reason != NoCand && "uninitialized Sched candidate");
      SU = Best.SU;
      Reason = Best.Reason;
      AtTop = Best.AtTop;
      RPDelta = Best.RPDelta;
      ResDelta = Best.ResDelta;
    }

    /// Compute resource deltas for this candidate from \p DAG and \p SchedModel.
    /// @param DAG Schedule DAG providing instruction resource usage.
    /// @param SchedModel Target scheduling model for resource indices.
    LLVM_ABI void initResourceDelta(const ScheduleDAGMI *DAG,
                                    const TargetSchedModel *SchedModel);
  };

protected:
  /// Pass context used to access target and analysis data.
  const MachineSchedContext *Context;
  /// Target scheduling model for the current subtarget.
  const TargetSchedModel *SchedModel = nullptr;
  /// Target register info used by pressure heuristics.
  const TargetRegisterInfo *TRI = nullptr;
  /// Index of the top boundary within the current region.
  unsigned TopIdx = 0;
  /// Index of the bottom boundary within the current region.
  unsigned BotIdx = 0;
  /// Number of instructions in the current scheduling region.
  unsigned NumRegionInstrs = 0;

  /// Per-region scheduling policy for this strategy.
  MachineSchedPolicy RegionPolicy;

  /// Remainder of unscheduled latency and resources.
  SchedRemainder Rem;

  /// Construct base state from scheduling context \p C.
  /// @param C Pass context providing target and analysis data.
  GenericSchedulerBase(const MachineSchedContext *C) : Context(C) {}

  /// Update \p Policy for the next pick based on zone pressure and resources.
  /// @param Policy Policy updated for the current zone.
  /// @param IsPostRA True when running the post-RA scheduler.
  /// @param CurrZone Boundary whose candidates are being compared.
  /// @param OtherZone Opposite boundary, or null if unidirectional.
  LLVM_ABI void setPolicy(CandPolicy &Policy, bool IsPostRA,
                          SchedBoundary &CurrZone, SchedBoundary *OtherZone);

  /// Return the current per-region scheduling policy.
  /// @return The current per-region scheduling policy.
  MachineSchedPolicy getPolicy() const override { return RegionPolicy; }

#ifndef NDEBUG
  /// Trace debug information for candidate \p Cand.
  /// @param Cand Candidate whose selection reason is printed.
  void traceCandidate(const SchedCandidate &Cand);
#endif

private:
  bool shouldReduceLatency(const CandPolicy &Policy, SchedBoundary &CurrZone,
                           bool ComputeRemLatency, unsigned &RemLatency) const;
};

// Utility functions used by heuristics in tryCandidate().
/// Compute remaining latency for the current scheduling zone.
/// @param CurrZone Boundary whose remaining latency is computed.
/// @return Remaining latency cycles for CurrZone.
LLVM_ABI unsigned computeRemLatency(SchedBoundary &CurrZone);
/// Prefer \p TryCand when \p TryVal is less than \p CandVal.
/// @param TryVal Metric for the tentative candidate.
/// @param CandVal Metric for the current best candidate.
/// @param TryCand Tentative candidate being compared.
/// @param Cand Current best candidate, updated on success.
/// @param Reason Heuristic reason recorded when TryCand wins.
/// @return True if TryCand replaced Cand because TryVal was less.
LLVM_ABI bool tryLess(int TryVal, int CandVal,
                      GenericSchedulerBase::SchedCandidate &TryCand,
                      GenericSchedulerBase::SchedCandidate &Cand,
                      GenericSchedulerBase::CandReason Reason);
/// Prefer \p TryCand when \p TryVal is greater than \p CandVal.
/// @param TryVal Metric for the tentative candidate.
/// @param CandVal Metric for the current best candidate.
/// @param TryCand Tentative candidate being compared.
/// @param Cand Current best candidate, updated on success.
/// @param Reason Heuristic reason recorded when TryCand wins.
/// @return True if TryCand replaced Cand because TryVal was greater.
LLVM_ABI bool tryGreater(int TryVal, int CandVal,
                         GenericSchedulerBase::SchedCandidate &TryCand,
                         GenericSchedulerBase::SchedCandidate &Cand,
                         GenericSchedulerBase::CandReason Reason);
/// Apply latency heuristics between \p TryCand and \p Cand in \p Zone.
/// @param TryCand Tentative candidate being compared.
/// @param Cand Current best candidate, updated on success.
/// @param Zone Boundary providing latency and ready-cycle data.
/// @return True if TryCand replaced Cand due to latency heuristics.
LLVM_ABI bool tryLatency(GenericSchedulerBase::SchedCandidate &TryCand,
                         GenericSchedulerBase::SchedCandidate &Cand,
                         SchedBoundary &Zone);
/// Apply register-pressure heuristics between \p TryP and \p CandP.
/// @param TryP Pressure change for the tentative candidate.
/// @param CandP Pressure change for the current best candidate.
/// @param TryCand Tentative candidate being compared.
/// @param Cand Current best candidate, updated on success.
/// @param Reason Heuristic reason recorded when TryCand wins.
/// @param TRI Target register info for pressure-set names.
/// @param MF Machine function providing register class context.
/// @return True if TryCand replaced Cand due to pressure heuristics.
LLVM_ABI bool tryPressure(const PressureChange &TryP,
                          const PressureChange &CandP,
                          GenericSchedulerBase::SchedCandidate &TryCand,
                          GenericSchedulerBase::SchedCandidate &Cand,
                          GenericSchedulerBase::CandReason Reason,
                          const TargetRegisterInfo *TRI,
                          const MachineFunction &MF);
/// Bias candidate selection toward physreg copies near their users.
/// @param TryCand Tentative candidate being compared.
/// @param Cand Current best candidate, updated on success.
/// @param Zone Boundary used for physreg bias, or null.
/// @param BiasPRegsExtra Enable additional physreg bias cases.
/// @return True if TryCand replaced Cand due to physreg bias.
LLVM_ABI bool tryBiasPhysRegs(GenericSchedulerBase::SchedCandidate &TryCand,
                              GenericSchedulerBase::SchedCandidate &Cand,
                              SchedBoundary *Zone, bool BiasPRegsExtra);
/// Return the number of unresolved weak edges for \p SU.
/// @param SU Scheduling unit whose weak predecessors/successors are counted.
/// @param isTop True to count weak successors; false for predecessors.
/// @return The number of unresolved weak edges for SU.
LLVM_ABI unsigned getWeakLeft(const SUnit *SU, bool isTop);
/// Return a bias score for scheduling physreg-related unit \p SU.
/// @param SU Scheduling unit being scored.
/// @param isTop True if SU would be scheduled from the top boundary.
/// @param BiasPRegsExtra Enable additional physreg bias cases.
/// @return A bias score favoring physreg-related scheduling of SU.
LLVM_ABI int biasPhysReg(const SUnit *SU, bool isTop,
                         bool BiasPRegsExtra = false);

/// GenericScheduler shrinks the unscheduled zone using heuristics to balance
/// the schedule.
class LLVM_ABI GenericScheduler : public GenericSchedulerBase {
public:
  /// Construct a pre-RA generic scheduler for context \p C.
  /// @param C Pass context providing target and analysis data.
  GenericScheduler(const MachineSchedContext *C):
    GenericSchedulerBase(C), Top(SchedBoundary::TopQID, "TopQ"),
    Bot(SchedBoundary::BotQID, "BotQ") {}

  /// Initialize per-region policy for instructions in [\p Begin, \p End).
  /// @param Begin First instruction in the region.
  /// @param End End iterator of the region.
  /// @param NumRegionInstrs Number of instructions in the region.
  void initPolicy(MachineBasicBlock::iterator Begin,
                  MachineBasicBlock::iterator End,
                  unsigned NumRegionInstrs) override;

  /// Dump the current scheduling policy to the debug stream.
  void dumpPolicy() const override;

  /// Return true if register pressure tracking is enabled for this region.
  /// @return True if register pressure tracking is enabled for this region.
  bool shouldTrackPressure() const override {
    return RegionPolicy.ShouldTrackPressure;
  }

  /// Return true if lane-mask tracking is enabled for this region.
  /// @return True if lane-mask tracking is enabled for this region.
  bool shouldTrackLaneMasks() const override {
    return RegionPolicy.ShouldTrackLaneMasks;
  }

  /// Initialize top/bottom boundaries from live schedule DAG \p dag.
  /// @param dag Live schedule DAG for the current region.
  void initialize(ScheduleDAGMI *dag) override;

  /// Pick the next node to schedule from either boundary.
  /// @param IsTopNode Set to true if the chosen node is scheduled top-down.
  /// @return The next node to schedule, or null if none remain.
  SUnit *pickNode(bool &IsTopNode) override;

  /// Update boundaries after scheduling \p SU.
  /// @param SU Newly scheduled unit.
  /// @param IsTopNode True if SU was scheduled from the top boundary.
  void schedNode(SUnit *SU, bool IsTopNode) override;

  /// Release \p SU into the top ready queues when predecessors are done.
  /// @param SU Node whose predecessors are all scheduled.
  void releaseTopNode(SUnit *SU) override {
    if (SU->isScheduled)
      return;

    Top.releaseNode(SU, SU->TopReadyCycle, false);
    TopCand.SU = nullptr;
  }

  /// Release \p SU into the bottom ready queues when successors are done.
  /// @param SU Node whose successors are all scheduled.
  void releaseBottomNode(SUnit *SU) override {
    if (SU->isScheduled)
      return;

    Bot.releaseNode(SU, SU->BotReadyCycle, false);
    BotCand.SU = nullptr;
  }

  /// Register region roots and compute acyclic latency limits.
  void registerRoots() override;

protected:
  /// Live schedule DAG for the current region.
  ScheduleDAGMILive *DAG = nullptr;

  /// State of the top scheduled instruction boundary.
  SchedBoundary Top;
  /// State of the bottom scheduled instruction boundary.
  SchedBoundary Bot;

  /// Cluster ID of the last instruction scheduled from the top.
  unsigned TopClusterID;
  /// Cluster ID of the last instruction scheduled from the bottom.
  unsigned BotClusterID;

  /// Candidate last picked from Top boundary.
  SchedCandidate TopCand;
  /// Candidate last picked from Bot boundary.
  SchedCandidate BotCand;

  /// Detect acyclic latency limits and update remainder state.
  void checkAcyclicLatency();

  /// Initialize \p Cand for \p SU at the given boundary.
  /// @param Cand Candidate state to fill.
  /// @param SU Scheduling unit being evaluated.
  /// @param AtTop True if SU is considered at the top boundary.
  /// @param RPTracker Pressure tracker for the candidate's boundary.
  /// @param TempTracker Temporary tracker used to compute pressure deltas.
  void initCandidate(SchedCandidate &Cand, SUnit *SU, bool AtTop,
                     const RegPressureTracker &RPTracker,
                     RegPressureTracker &TempTracker);

  /// Compare \p TryCand against \p Cand and keep the better one.
  /// @param Cand Current best candidate, updated on success.
  /// @param TryCand Tentative candidate being compared.
  /// @param Zone Boundary providing heuristic context, or null.
  /// @return True if TryCand replaced Cand as the better candidate.
  virtual bool tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand,
                            SchedBoundary *Zone) const;

  /// Pick from top and bottom queues and choose the better candidate.
  /// @param IsTopNode Set to true if the chosen node is scheduled top-down.
  /// @return The selected scheduling unit.
  SUnit *pickNodeBidirectional(bool &IsTopNode);

  /// Pick the best candidate from \p Zone's available queue into \p Candidate.
  /// @param Zone Boundary whose available queue is scanned.
  /// @param ZonePolicy Policy applied while comparing candidates.
  /// @param RPTracker Pressure tracker for this boundary.
  /// @param Candidate Best candidate found in the queue.
  void pickNodeFromQueue(SchedBoundary &Zone,
                         const CandPolicy &ZonePolicy,
                         const RegPressureTracker &RPTracker,
                         SchedCandidate &Candidate);

  /// Reschedule physreg copies around \p SU for better locality.
  /// @param SU Scheduled unit that may define or use physregs.
  /// @param isTop True if SU was scheduled from the top boundary.
  void reschedulePhysReg(SUnit *SU, bool isTop);
};

/// Post-RA generic scheduler driven by ScheduleDAGMI callbacks.
///
/// Callbacks from ScheduleDAGMI:
///   initPolicy -> initialize(DAG) -> registerRoots -> pickNode ...
class LLVM_ABI PostGenericScheduler : public GenericSchedulerBase {
protected:
  /// Post-RA schedule DAG for the current region.
  ScheduleDAGMI *DAG = nullptr;
  /// State of the top scheduled instruction boundary.
  SchedBoundary Top;
  /// State of the bottom scheduled instruction boundary.
  SchedBoundary Bot;

  /// Candidate last picked from Top boundary.
  SchedCandidate TopCand;
  /// Candidate last picked from Bot boundary.
  SchedCandidate BotCand;

  /// Cluster ID of the last instruction scheduled from the top.
  unsigned TopClusterID;
  /// Cluster ID of the last instruction scheduled from the bottom.
  unsigned BotClusterID;

public:
  /// Construct a post-RA generic scheduler for context \p C.
  /// @param C Pass context providing target and analysis data.
  PostGenericScheduler(const MachineSchedContext *C)
      : GenericSchedulerBase(C), Top(SchedBoundary::TopQID, "TopQ"),
        Bot(SchedBoundary::BotQID, "BotQ") {}

  /// Destroy the post-RA generic scheduler.
  ~PostGenericScheduler() override = default;

  /// Initialize per-region policy for instructions in [\p Begin, \p End).
  /// @param Begin First instruction in the region.
  /// @param End End iterator of the region.
  /// @param NumRegionInstrs Number of instructions in the region.
  void initPolicy(MachineBasicBlock::iterator Begin,
                  MachineBasicBlock::iterator End,
                  unsigned NumRegionInstrs) override;

  /// PostRA scheduling does not track pressure.
  /// @return Always false.
  bool shouldTrackPressure() const override { return false; }

  /// Initialize top/bottom boundaries from schedule DAG \p Dag.
  /// @param Dag Post-RA schedule DAG for the current region.
  void initialize(ScheduleDAGMI *Dag) override;

  /// Register region roots before picking nodes.
  void registerRoots() override;

  /// Pick the next node to schedule from either boundary.
  /// @param IsTopNode Set to true if the chosen node is scheduled top-down.
  /// @return The next node to schedule, or null if none remain.
  SUnit *pickNode(bool &IsTopNode) override;

  /// Pick from top and bottom queues and choose the better candidate.
  /// @param IsTopNode Set to true if the chosen node is scheduled top-down.
  /// @return The selected scheduling unit.
  SUnit *pickNodeBidirectional(bool &IsTopNode);

  /// Post-RA scheduling does not support subtree analysis.
  /// @param SubtreeID Unused subtree identifier.
  void scheduleTree(unsigned SubtreeID) override {
    llvm_unreachable("PostRA scheduler does not support subtree analysis.");
  }

  /// Update boundaries after scheduling \p SU.
  /// @param SU Newly scheduled unit.
  /// @param IsTopNode True if SU was scheduled from the top boundary.
  void schedNode(SUnit *SU, bool IsTopNode) override;

  /// Release \p SU into the top ready queues when predecessors are done.
  /// @param SU Node whose predecessors are all scheduled.
  void releaseTopNode(SUnit *SU) override {
    if (SU->isScheduled)
      return;
    Top.releaseNode(SU, SU->TopReadyCycle, false);
    TopCand.SU = nullptr;
  }

  /// Release \p SU into the bottom ready queues when successors are done.
  /// @param SU Node whose successors are all scheduled.
  void releaseBottomNode(SUnit *SU) override {
    if (SU->isScheduled)
      return;
    Bot.releaseNode(SU, SU->BotReadyCycle, false);
    BotCand.SU = nullptr;
  }

protected:
  /// Compare \p TryCand against \p Cand and keep the better one.
  /// @param Cand Current best candidate, updated on success.
  /// @param TryCand Tentative candidate being compared.
  /// @return True if TryCand replaced Cand as the better candidate.
  virtual bool tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand);

  /// Pick the best candidate from \p Zone's available queue into \p Cand.
  /// @param Zone Boundary whose available queue is scanned.
  /// @param Cand Best candidate found in the queue.
  void pickNodeFromQueue(SchedBoundary &Zone, SchedCandidate &Cand);
};

/// Create a DAG mutation that clusters loads for better memory locality.
///
/// If ReorderWhileClustering is set to true, no attempt will be made to
/// reduce reordering due to store clustering.
/// @param TII Target instruction info used to recognize loads.
/// @param TRI Target register info used while rewriting edges.
/// @param ReorderWhileClustering Allow freer reordering while clustering.
/// @return A unique pointer to the load-cluster DAG mutation.
LLVM_ABI std::unique_ptr<ScheduleDAGMutation>
createLoadClusterDAGMutation(const TargetInstrInfo *TII,
                             const TargetRegisterInfo *TRI,
                             bool ReorderWhileClustering = false);

/// Create a DAG mutation that clusters stores for better memory locality.
///
/// If ReorderWhileClustering is set to true, no attempt will be made to
/// reduce reordering due to store clustering.
/// @param TII Target instruction info used to recognize stores.
/// @param TRI Target register info used while rewriting edges.
/// @param ReorderWhileClustering Allow freer reordering while clustering.
/// @return A unique pointer to the store-cluster DAG mutation.
LLVM_ABI std::unique_ptr<ScheduleDAGMutation>
createStoreClusterDAGMutation(const TargetInstrInfo *TII,
                              const TargetRegisterInfo *TRI,
                              bool ReorderWhileClustering = false);

/// Create a DAG mutation that constrains copies near their uses/defs.
/// @param TII Target instruction info used to recognize copies.
/// @param TRI Target register info used while rewriting edges.
/// @return A unique pointer to the copy-constrain DAG mutation.
LLVM_ABI std::unique_ptr<ScheduleDAGMutation>
createCopyConstrainDAGMutation(const TargetInstrInfo *TII,
                               const TargetRegisterInfo *TRI);

/// Create the standard converging machine scheduler. This will be used as the
/// default scheduler if the target does not set a default.
/// Adds default DAG mutations.
/// @param C Pass context used to construct the live schedule DAG.
/// @return A new ScheduleDAGMILive with default DAG mutations.
template <typename Strategy = GenericScheduler>
ScheduleDAGMILive *createSchedLive(MachineSchedContext *C) {
  ScheduleDAGMILive *DAG =
      new ScheduleDAGMILive(C, std::make_unique<Strategy>(C));
  // Register DAG post-processors.
  //
  // FIXME: extend the mutation API to allow earlier mutations to instantiate
  // data and pass it to later mutations. Have a single mutation that gathers
  // the interesting nodes in one pass.
  DAG->addMutation(createCopyConstrainDAGMutation(DAG->TII, DAG->TRI));
  return DAG;
}

/// Create a generic scheduler with no vreg liveness or DAG mutation passes.
/// @param C Pass context used to construct the post-RA schedule DAG.
/// @return A new ScheduleDAGMI configured for post-RA scheduling.
template <typename Strategy = PostGenericScheduler>
ScheduleDAGMI *createSchedPostRA(MachineSchedContext *C) {
  return new ScheduleDAGMI(C, std::make_unique<Strategy>(C),
                           /*RemoveKillFlags=*/true);
}

/// New PM pass that runs the pre-RA machine instruction scheduler.
class MachineSchedulerPass
    : public OptionalPassInfoMixin<MachineSchedulerPass> {
  // FIXME: Remove this member once RegisterClassInfo is queryable as an
  // analysis.
  std::unique_ptr<impl_detail::MachineSchedulerImpl> Impl;
  const TargetMachine *TM;

public:
  /// Construct the pass for target machine \p TM.
  /// @param TM Target machine providing subtarget scheduling hooks.
  LLVM_ABI MachineSchedulerPass(const TargetMachine *TM);
  /// Move-construct from \p Other.
  /// @param Other Pass instance to move from.
  LLVM_ABI MachineSchedulerPass(MachineSchedulerPass &&Other);
  /// Destroy the pass and owned implementation.
  LLVM_ABI ~MachineSchedulerPass();
  /// Run pre-RA scheduling on \p MF.
  /// @param MF Machine function to schedule.
  /// @param MFAM Analysis manager providing required analyses.
  /// @return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// New PM pass that runs the post-RA machine instruction scheduler.
class PostMachineSchedulerPass
    : public OptionalPassInfoMixin<PostMachineSchedulerPass> {
  // FIXME: Remove this member once RegisterClassInfo is queryable as an
  // analysis.
  std::unique_ptr<impl_detail::PostMachineSchedulerImpl> Impl;
  const TargetMachine *TM;

public:
  /// Construct the pass for target machine \p TM.
  /// @param TM Target machine providing subtarget scheduling hooks.
  LLVM_ABI PostMachineSchedulerPass(const TargetMachine *TM);
  /// Move-construct from \p Other.
  /// @param Other Pass instance to move from.
  LLVM_ABI PostMachineSchedulerPass(PostMachineSchedulerPass &&Other);
  /// Destroy the pass and owned implementation.
  LLVM_ABI ~PostMachineSchedulerPass();
  /// Run post-RA scheduling on \p MF.
  /// @param MF Machine function to schedule.
  /// @param MFAM Analysis manager providing required analyses.
  /// @return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};
} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINESCHEDULER_H
