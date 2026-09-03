//===- MachinePipeliner.h - Machine Software Pipeliner Pass -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An implementation of the Swing Modulo Scheduling (SMS) software pipeliner.
//
// Software pipelining (SWP) is an instruction scheduling technique for loops
// that overlap loop iterations and exploits ILP via a compiler transformation.
//
// Swing Modulo Scheduling is an implementation of software pipelining
// that generates schedules that are near optimal in terms of initiation
// interval, register requirements, and stage count. See the papers:
//
// "Swing Modulo Scheduling: A Lifetime-Sensitive Approach", by J. Llosa,
// A. Gonzalez, E. Ayguade, and M. Valero. In PACT '96 Proceedings of the 1996
// Conference on Parallel Architectures and Compilation Techiniques.
//
// "Lifetime-Sensitive Modulo Scheduling in a Production Environment", by J.
// Llosa, E. Ayguade, A. Gonzalez, M. Valero, and J. Eckhardt. In IEEE
// Transactions on Computers, Vol. 50, No. 3, 2001.
//
// "An Implementation of Swing Modulo Scheduling With Extensions for
// Superblocks", by T. Lattner, Master's Thesis, University of Illinois at
// Urbana-Champaign, 2005.
//
//
// The SMS algorithm consists of three main steps after computing the minimal
// initiation interval (MII).
// 1) Analyze the dependence graph and compute information about each
//    instruction in the graph.
// 2) Order the nodes (instructions) by priority based upon the heuristics
//    described in the algorithm.
// 3) Attempt to schedule the nodes in the specified order using the MII.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_MACHINEPIPELINER_H
#define LLVM_CODEGEN_MACHINEPIPELINER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/WindowScheduler.h"

#include <deque>

namespace llvm {

class AAResults;
class NodeSet;
class SMSchedule;

/// Enable the CopyToPhi DAG mutation during swing modulo scheduling.
extern LLVM_ABI cl::opt<bool> SwpEnableCopyToPhi;
/// Force a specific issue width for SMS resource modeling; <= 0 keeps the
/// model default.
extern LLVM_ABI cl::opt<int> SwpForceIssueWidth;

/// Software pipelining policy for a loop, which a target can customize by
/// implementing TargetSubtargetInfo::overridePipelinerPolicy.
struct MachinePipelinerPolicy {
  /// Limit the register pressure of the scheduled loop, retrying at a higher
  /// II when a schedule needs too many registers.
  bool ShouldLimitRegPressure = false;
};

/// The main class in the implementation of the target independent
/// software pipeliner pass.
class LLVM_ABI MachinePipeliner : public MachineFunctionPass {
public:
  /// Machine function currently being processed.
  MachineFunction *MF = nullptr;
  /// Optimization remark emitter for this function.
  MachineOptimizationRemarkEmitter *ORE = nullptr;
  /// Machine loop info analysis for the function.
  const MachineLoopInfo *MLI = nullptr;
  /// Instruction itineraries for the current subtarget, if any.
  const InstrItineraryData *InstrItins = nullptr;
  /// Target instruction info for the current subtarget.
  const TargetInstrInfo *TII = nullptr;
  /// Register class info used for pressure estimates.
  const RegisterClassInfo *RegClassInfo = nullptr;
  /// True if pipelining was disabled for the loop by a pragma.
  bool disabledByPragma = false;
  /// Initiation interval forced by pragma, or zero if unset.
  unsigned II_setByPragma = 0;

#ifndef NDEBUG
  /// Number of schedule attempts made in debug builds.
  static int NumTries;
#endif

  /// Cache the target analysis information about the loop.
  struct LoopInfo {
    /// True successor basic block of the loop latch branch.
    MachineBasicBlock *TBB = nullptr;
    /// False successor basic block of the loop latch branch.
    MachineBasicBlock *FBB = nullptr;
    /// Condition operands of the loop latch branch.
    SmallVector<MachineOperand, 4> BrCond;
    /// Induction-variable definition used by the loop.
    MachineInstr *LoopInductionVar = nullptr;
    /// Compare instruction that computes the loop exit condition.
    MachineInstr *LoopCompare = nullptr;
    /// Target-specific pipeliner metadata for this loop.
    std::unique_ptr<TargetInstrInfo::PipelinerLoopInfo> LoopPipelinerInfo =
        nullptr;
  };
  /// Cached analysis information for the loop being pipelined.
  LoopInfo LI;

  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the software pipeliner pass.
  MachinePipeliner() : MachineFunctionPass(ID) {}

  /// Run software pipelining on \p MF.
  /// \param MF Machine function to process.
  /// \return True if the function was modified.
  bool runOnMachineFunction(MachineFunction &MF) override;

  /// Declare the analyses required and preserved by this pass.
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  void preprocessPhiNodes(MachineBasicBlock &B);
  bool canPipelineLoop(MachineLoop &L);
  bool scheduleLoop(MachineLoop &L);
  bool swingModuloScheduler(MachineLoop &L);
  void setPragmaPipelineOptions(MachineLoop &L);
  bool runWindowScheduler(MachineLoop &L);
  bool useSwingModuloScheduler();
  bool useWindowScheduler(bool Changed);
};

/// Represents a dependence between two instruction.
class SwingSchedulerDDGEdge {
  SUnit *Dst = nullptr;
  SDep Pred;
  unsigned Distance = 0;
  bool IsValidationOnly = false;

public:
  /// Create an edge from an original DAG predecessor/successor pair.
  ///
  /// Creates an edge corresponding to an edge represented by \p PredOrSucc and
  /// \p Dep in the original DAG. This pair has no information about the
  /// direction of the edge, so we need to pass an additional argument
  /// \p IsSucc.
  /// \param PredOrSucc Other endpoint of the original DAG edge.
  /// \param Dep Original SDep describing the dependence kind and latency.
  /// \param IsSucc True if \p PredOrSucc is a successor of the source node.
  /// \param IsValidationOnly True if the edge is used only for schedule
  /// validation.
  SwingSchedulerDDGEdge(SUnit *PredOrSucc, const SDep &Dep, bool IsSucc,
                        bool IsValidationOnly)
      : Dst(PredOrSucc), Pred(Dep), Distance(0u),
        IsValidationOnly(IsValidationOnly) {
    SUnit *Src = Dep.getSUnit();

    if (IsSucc) {
      std::swap(Src, Dst);
      Pred.setSUnit(Src);
    }

    // An anti-dependence to PHI means loop-carried dependence.
    if (Pred.getKind() == SDep::Anti && Src->getInstr()->isPHI()) {
      Distance = 1;
      std::swap(Src, Dst);
      auto Reg = Pred.getReg();
      Pred = SDep(Src, SDep::Kind::Data, Reg);
    }
  }

  /// Returns the SUnit from which the edge comes (source node).
  /// \return Source SUnit of the edge.
  SUnit *getSrc() const { return Pred.getSUnit(); }

  /// Returns the SUnit to which the edge points (destination node).
  /// \return Destination SUnit of the edge.
  SUnit *getDst() const { return Dst; }

  /// Returns the latency value for the edge.
  /// \return Latency of the edge in cycles.
  unsigned getLatency() const { return Pred.getLatency(); }

  /// Sets the latency for the edge.
  /// \param Latency New edge latency in cycles.
  void setLatency(unsigned Latency) { Pred.setLatency(Latency); }

  /// Returns the distance value for the edge.
  /// \return Iteration distance carried by the edge.
  unsigned getDistance() const { return Distance; }

  /// Sets the distance value for the edge.
  /// \param D Iteration distance carried by the edge.
  void setDistance(unsigned D) { Distance = D; }

  /// Returns the register associated with the edge.
  /// \return Register associated with the edge.
  Register getReg() const { return Pred.getReg(); }

  /// Returns true if the edge represents anti dependence.
  /// \return True if the edge is an anti dependence.
  bool isAntiDep() const { return Pred.getKind() == SDep::Kind::Anti; }

  /// Returns true if the edge represents output dependence.
  /// \return True if the edge is an output dependence.
  bool isOutputDep() const { return Pred.getKind() == SDep::Kind::Output; }

  /// Returns true if the edge represents a dependence that is not data, anti or
  /// output dependence.
  /// \return True if the edge is an order dependence.
  bool isOrderDep() const { return Pred.getKind() == SDep::Kind::Order; }

  /// Returns true if the edge represents unknown scheduling barrier.
  /// \return True if the edge is an unknown scheduling barrier.
  bool isBarrier() const { return Pred.isBarrier(); }

  /// Returns true if the edge represents an artificial dependence.
  /// \return True if the edge is an artificial dependence.
  bool isArtificial() const { return Pred.isArtificial(); }

  /// Tests if this is a Data dependence that is associated with a register.
  /// \return True if this is a register-associated Data dependence.
  bool isAssignedRegDep() const { return Pred.isAssignedRegDep(); }

  /// Return true if this edge should be ignored when computing cost functions.
  ///
  /// Returns true for DDG nodes that we ignore when computing the cost
  /// functions. We ignore the back-edge recurrence in order to avoid unbounded
  /// recursion in the calculation of the ASAP, ALAP, etc functions.
  /// \param IgnoreAnti True to also ignore anti-dependence edges.
  /// \return True if this edge should be ignored for cost functions.
  LLVM_ABI bool ignoreDependence(bool IgnoreAnti) const;

  /// Returns true if this edge is intended to be used only for validating the
  /// schedule.
  /// \return True if this edge is used only for schedule validation.
  bool isValidationOnly() const { return IsValidationOnly; }
};

/// Loop-carried dependencies tracked separately from the acyclic SUnit DAG.
///
/// Because SwingSchedulerDAG doesn't assume cycle dependencies as the name
/// suggests, such dependencies must be handled separately. After DAG
/// construction is finished, these dependencies are added to SwingSchedulerDDG.
/// TODO: Also handle output-dependencies introduced by physical registers.
struct LoopCarriedEdges {
  /// Set of SUnits that have a loop-carried order dependence on a key node.
  using OrderDep = SmallSetVector<SUnit *, 8>;
  /// Map from an SUnit to the set of nodes with loop-carried order deps on it.
  using OrderDepsType = DenseMap<SUnit *, OrderDep>;

  /// Loop-carried order dependences keyed by the dependent SUnit.
  OrderDepsType OrderDeps;

  /// Return the order-dependence set for \p Key, or nullptr if none exist.
  /// \param Key SUnit whose loop-carried order predecessors are requested.
  /// \return Order-dependence set for \p Key, or nullptr if none exist.
  const OrderDep *getOrderDepOrNull(SUnit *Key) const {
    auto Ite = OrderDeps.find(Key);
    if (Ite == OrderDeps.end())
      return nullptr;
    return &Ite->second;
  }

  /// Add DAG edges that encode historically represented loop-carried deps.
  ///
  /// Adds some edges to the original DAG that correspond to loop-carried
  /// dependencies. Historically, loop-carried edges are represented by using
  /// non-loop-carried edges in the original DAG. This function appends such
  /// edges to preserve the previous behavior.
  /// \param SUnits Scheduling units of the region being mutated.
  /// \param TII Target instruction info used while creating edges.
  LLVM_ABI void modifySUnits(std::vector<SUnit> &SUnits,
                             const TargetInstrInfo *TII);

  /// Dump loop-carried edges involving \p SU.
  /// \param SU Scheduling unit to dump edges for.
  /// \param TRI Target register info for printing registers.
  /// \param MRI Machine register info for printing virtual registers.
  LLVM_ABI void dump(SUnit *SU, const TargetRegisterInfo *TRI,
                     const MachineRegisterInfo *MRI) const;
};

/// Wrapper around SUnit that exposes loop-carried dependence edges.
///
/// This class provides APIs to retrieve edges from/to an SUnit node, with a
/// particular focus on loop-carried dependencies. Since SUnit is not designed
/// to represent such edges, handling them directly using its APIs has required
/// non-trivial logic in the past. This class serves as a wrapper around SUnit,
/// offering a simpler interface for managing these dependencies.
class SwingSchedulerDDG {
  using EdgesType = SmallVector<SwingSchedulerDDGEdge, 4>;

  struct SwingSchedulerDDGEdges {
    EdgesType Preds;
    EdgesType Succs;

    /// This field is a subset of ValidationOnlyEdges. These edges are used only
    /// by specific heuristics, mainly for cycle detection. Although they are
    /// unnecessary in theory (i.e., ignoring them should still yield a valid
    /// schedule), they are retained to preserve the existing behavior. Since we
    /// only need which extra edges exist from a given SUnit, we only store the
    /// destination SUnits.
    SmallVector<SUnit *, 4> ExtraSuccs;
  };

  void initEdges(SUnit *SU);

  SUnit *EntrySU;
  SUnit *ExitSU;

  std::vector<SwingSchedulerDDGEdges> EdgesVec;
  SwingSchedulerDDGEdges EntrySUEdges;
  SwingSchedulerDDGEdges ExitSUEdges;

  /// Edges that are used only when validating the schedule. These edges are
  /// not considered to drive the optimization heuristics.
  SmallVector<SwingSchedulerDDGEdge, 8> ValidationOnlyEdges;

  /// Adds a NON-validation-only edge to the DDG. Assumes to be called only by
  /// the ctor.
  void addEdge(const SUnit *SU, const SwingSchedulerDDGEdge &Edge);

  SwingSchedulerDDGEdges &getEdges(const SUnit *SU);
  const SwingSchedulerDDGEdges &getEdges(const SUnit *SU) const;

public:
  /// Construct a DDG over \p SUnits using loop-carried edges from \p LCE.
  /// \param SUnits Scheduling units for the region.
  /// \param EntrySU Synthetic entry node of the schedule DAG.
  /// \param ExitSU Synthetic exit node of the schedule DAG.
  /// \param LCE Loop-carried order dependences collected during DAG
  /// construction.
  LLVM_ABI SwingSchedulerDDG(std::vector<SUnit> &SUnits, SUnit *EntrySU,
                             SUnit *ExitSU, const LoopCarriedEdges &LCE);

  /// Return the incoming edges of \p SU.
  /// \param SU Node whose predecessors are requested.
  /// \return Incoming edges of \p SU.
  LLVM_ABI const EdgesType &getInEdges(const SUnit *SU) const;

  /// Return the outgoing edges of \p SU.
  /// \param SU Node whose successors are requested.
  /// \return Outgoing edges of \p SU.
  LLVM_ABI const EdgesType &getOutEdges(const SUnit *SU) const;

  /// Return extra validation-only successor nodes of \p SU.
  /// \param SU Node whose extra successors are requested.
  /// \return Extra validation-only successor nodes of \p SU.
  LLVM_ABI ArrayRef<SUnit *> getExtraOutEdges(const SUnit *SU) const;

  /// Return true if \p Schedule satisfies the DDG's dependence constraints.
  /// \param Schedule Candidate SMS schedule to validate.
  /// \return True if \p Schedule satisfies dependence constraints.
  LLVM_ABI bool isValidSchedule(const SMSchedule &Schedule) const;
};

/// This class builds the dependence graph for the instructions in a loop,
/// and attempts to schedule the instructions using the SMS algorithm.
class LLVM_ABI SwingSchedulerDAG : public ScheduleDAGInstrs {
  MachinePipeliner &Pass;

  std::unique_ptr<SwingSchedulerDDG> DDG;

  /// The minimum initiation interval between iterations for this schedule.
  unsigned MII = 0;
  /// The maximum initiation interval between iterations for this schedule.
  unsigned MAX_II = 0;
  /// Set to true if a valid pipelined schedule is found for the loop.
  bool Scheduled = false;
  MachineLoop &Loop;
  LiveIntervals &LIS;
  const RegisterClassInfo &RegClassInfo;
  unsigned II_setByPragma = 0;
  TargetInstrInfo::PipelinerLoopInfo *LoopPipelinerInfo = nullptr;

  /// Policy for this loop, after target and command line overrides.
  MachinePipelinerPolicy Policy;

  /// A topological ordering of the SUnits, which is needed for changing
  /// dependences and iterating over the SUnits.
  ScheduleDAGTopologicalSort Topo;

  struct NodeInfo {
    int ASAP = 0;
    int ALAP = 0;
    int ZeroLatencyDepth = 0;
    int ZeroLatencyHeight = 0;

    NodeInfo() = default;
  };
  /// Computed properties for each node in the graph.
  std::vector<NodeInfo> ScheduleInfo;

  enum OrderKind { BottomUp = 0, TopDown = 1 };
  /// Computed node ordering for scheduling.
  SetVector<SUnit *> NodeOrder;

  using NodeSetType = SmallVector<NodeSet, 8>;
  using ValueMapTy = DenseMap<unsigned, unsigned>;
  using MBBVectorTy = SmallVectorImpl<MachineBasicBlock *>;
  using InstrMapTy = DenseMap<MachineInstr *, MachineInstr *>;

  /// Instructions to change when emitting the final schedule.
  DenseMap<SUnit *, std::pair<Register, int64_t>> InstrChanges;

  /// We may create a new instruction, so remember it because it
  /// must be deleted when the pass is finished.
  DenseMap<MachineInstr*, MachineInstr *> NewMIs;

  /// Ordered list of DAG postprocessing steps.
  std::vector<std::unique_ptr<ScheduleDAGMutation>> Mutations;

  /// Used to compute single-iteration dependencies (i.e., buildSchedGraph).
  AliasAnalysis *AA;

  /// Used to compute loop-carried dependencies (i.e.,
  /// addLoopCarriedDependences).
  BatchAAResults BAA;

  /// Helper class to implement Johnson's circuit finding algorithm.
  class Circuits {
    std::vector<SUnit> &SUnits;
    SetVector<SUnit *> Stack;
    BitVector Blocked;
    SmallVector<SmallPtrSet<SUnit *, 4>, 10> B;
    SmallVector<SmallVector<int, 4>, 16> AdjK;
    // Node to Index from ScheduleDAGTopologicalSort
    std::vector<int> *Node2Idx;
    unsigned NumPaths = 0u;
    static unsigned MaxPaths;

  public:
    Circuits(std::vector<SUnit> &SUs, ScheduleDAGTopologicalSort &Topo)
        : SUnits(SUs), Blocked(SUs.size()), B(SUs.size()), AdjK(SUs.size()) {
      Node2Idx = new std::vector<int>(SUs.size());
      unsigned Idx = 0;
      for (const auto &NodeNum : Topo)
        Node2Idx->at(NodeNum) = Idx++;
    }
    Circuits &operator=(const Circuits &other) = delete;
    Circuits(const Circuits &other) = delete;
    ~Circuits() { delete Node2Idx; }

    /// Reset the data structures used in the circuit algorithm.
    void reset() {
      Stack.clear();
      Blocked.reset();
      B.assign(SUnits.size(), SmallPtrSet<SUnit *, 4>());
      NumPaths = 0;
    }

    LLVM_ABI void createAdjacencyStructure(SwingSchedulerDDG *DDG);
    LLVM_ABI bool circuit(int V, int S, NodeSetType &NodeSets,
                          const SwingSchedulerDAG *DAG,
                          bool HasBackedge = false);
    LLVM_ABI void unblock(int U);
  };

  struct LLVM_ABI CopyToPhiMutation : public ScheduleDAGMutation {
    void apply(ScheduleDAGInstrs *DAG) override;
  };

public:
  /// Construct a swing scheduler DAG for loop \p L in pass \p P.
  /// \param P Owning MachinePipeliner pass.
  /// \param L Machine loop being scheduled.
  /// \param lis Live interval analysis for the function.
  /// \param rci Register class information for pressure estimates.
  /// \param II Initiation interval forced by pragma, or zero if unset.
  /// \param PLI Target-specific pipeliner loop info.
  /// \param AA Alias analysis used to build dependences.
  SwingSchedulerDAG(MachinePipeliner &P, MachineLoop &L, LiveIntervals &lis,
                    const RegisterClassInfo &rci, unsigned II,
                    TargetInstrInfo::PipelinerLoopInfo *PLI, AliasAnalysis *AA)
      : ScheduleDAGInstrs(*P.MF, P.MLI, false), Pass(P), Loop(L), LIS(lis),
        RegClassInfo(rci), II_setByPragma(II), LoopPipelinerInfo(PLI),
        Topo(SUnits, &ExitSU), AA(AA), BAA(*AA) {
    initPolicy();
    P.MF->getSubtarget().getSMSMutations(Mutations);
    if (SwpEnableCopyToPhi)
      Mutations.push_back(std::make_unique<CopyToPhiMutation>());
    BAA.enableCrossIterationMode();
  }

  /// Build the dependence graph and attempt to schedule the loop with SMS.
  void schedule() override;
  /// Finish the current basic block after scheduling.
  void finishBlock() override;

  /// Return true if the loop kernel has been scheduled.
  /// \return True if a valid pipelined schedule was found.
  bool hasNewSchedule() { return Scheduled; }

  /// Return the earliest time an instruction may be scheduled.
  /// \param Node Scheduling unit whose ASAP value is requested.
  /// \return Earliest legal schedule time for \p Node.
  int getASAP(SUnit *Node) { return ScheduleInfo[Node->NodeNum].ASAP; }

  /// Return the latest time an instruction my be scheduled.
  /// \param Node Scheduling unit whose ALAP value is requested.
  /// \return Latest legal schedule time for \p Node.
  int getALAP(SUnit *Node) { return ScheduleInfo[Node->NodeNum].ALAP; }

  /// The mobility function, which the number of slots in which
  /// an instruction may be scheduled.
  /// \param Node Scheduling unit whose mobility is requested.
  /// \return Mobility of \p Node (ALAP minus ASAP).
  int getMOV(SUnit *Node) { return getALAP(Node) - getASAP(Node); }

  /// The depth, in the dependence graph, for a node.
  /// \param Node Scheduling unit whose depth is requested.
  /// \return Depth of \p Node in the dependence graph.
  unsigned getDepth(SUnit *Node) { return Node->getDepth(); }

  /// The maximum unweighted length of a path from an arbitrary node to the
  /// given node in which each edge has latency 0
  /// \param Node Scheduling unit whose zero-latency depth is requested.
  /// \return Zero-latency depth of \p Node.
  int getZeroLatencyDepth(SUnit *Node) {
    return ScheduleInfo[Node->NodeNum].ZeroLatencyDepth;
  }

  /// The height, in the dependence graph, for a node.
  /// \param Node Scheduling unit whose height is requested.
  /// \return Height of \p Node in the dependence graph.
  unsigned getHeight(SUnit *Node) { return Node->getHeight(); }

  /// The maximum unweighted length of a path from the given node to an
  /// arbitrary node in which each edge has latency 0
  /// \param Node Scheduling unit whose zero-latency height is requested.
  /// \return Zero-latency height of \p Node.
  int getZeroLatencyHeight(SUnit *Node) {
    return ScheduleInfo[Node->NodeNum].ZeroLatencyHeight;
  }

  /// Apply a deferred instruction change for \p MI using \p Schedule.
  /// \param MI Instruction to rewrite.
  /// \param Schedule Final SMS schedule providing stage and cycle info.
  void applyInstrChange(MachineInstr *MI, SMSchedule &Schedule);

  /// Fix register overlaps among the scheduled instructions in \p Instrs.
  /// \param Instrs Instructions in one schedule cycle that may need
  /// rematerialization.
  void fixupRegisterOverlaps(std::deque<SUnit *> &Instrs);

  /// Return the new base register that was stored away for the changed
  /// instruction.
  /// \param SU Instruction whose rewritten base register is requested.
  /// \return Rewritten base register for \p SU, or an empty Register.
  Register getInstrBaseReg(SUnit *SU) const {
    DenseMap<SUnit *, std::pair<Register, int64_t>>::const_iterator It =
        InstrChanges.find(SU);
    if (It != InstrChanges.end())
      return It->second.first;
    return Register();
  }

  /// Append a post-processing DAG mutation.
  /// \param Mutation Mutation to apply after the dependence graph is built.
  void addMutation(std::unique_ptr<ScheduleDAGMutation> Mutation) {
    Mutations.push_back(std::move(Mutation));
  }

  /// Return true if \p DAG is a SwingSchedulerDAG.
  /// \param DAG Schedule DAG to classify.
  /// \return True; every ScheduleDAGInstrs passed here is a SwingSchedulerDAG.
  static bool classof(const ScheduleDAGInstrs *DAG) { return true; }

  /// Return the swing scheduler dependence graph.
  /// \return Pointer to the swing scheduler dependence graph.
  const SwingSchedulerDDG *getDDG() const { return DDG.get(); }

  /// Return true if \p BaseMI and \p OtherMI may overlap in a later iteration.
  /// \param BaseMI First memory instruction.
  /// \param OtherMI Second memory instruction.
  /// \return True if the instructions may overlap in a later iteration.
  bool mayOverlapInLaterIter(const MachineInstr *BaseMI,
                             const MachineInstr *OtherMI) const;

private:
  /// Set the policy for this loop, allowing the target to override it.
  void initPolicy();
  LoopCarriedEdges addLoopCarriedDependences();
  void updatePhiDependences();
  void changeDependences();
  unsigned calculateResMII();
  unsigned calculateRecMII(NodeSetType &RecNodeSets);
  void findCircuits(NodeSetType &NodeSets);
  void fuseRecs(NodeSetType &NodeSets);
  void removeDuplicateNodes(NodeSetType &NodeSets);
  void computeNodeFunctions(NodeSetType &NodeSets);
  void registerPressureFilter(NodeSetType &NodeSets);
  void colocateNodeSets(NodeSetType &NodeSets);
  void checkNodeSets(NodeSetType &NodeSets);
  void groupRemainingNodes(NodeSetType &NodeSets);
  void addConnectedNodes(SUnit *SU, NodeSet &NewSet,
                         SetVector<SUnit *> &NodesAdded);
  void computeNodeOrder(NodeSetType &NodeSets);
  void checkValidNodeOrder(const NodeSetType &Circuits) const;
  bool schedulePipeline(SMSchedule &Schedule);
  bool computeDelta(const MachineInstr &MI, int &Delta) const;
  MachineInstr *findDefInLoop(Register Reg);
  bool canUseLastOffsetValue(MachineInstr *MI, unsigned &BasePos,
                             unsigned &OffsetPos, Register &NewBase,
                             int64_t &NewOffset);
  void postProcessDAG();
  /// Set the Minimum Initiation Interval for this schedule attempt.
  void setMII(unsigned ResMII, unsigned RecMII);
  /// Set the Maximum Initiation Interval for this schedule attempt.
  void setMAX_II();
};

/// A NodeSet contains a set of SUnit DAG nodes with additional information
/// that assigns a priority to the set.
class NodeSet {
  SetVector<SUnit *> Nodes;
  bool HasRecurrence = false;
  unsigned RecMII = 0;
  int MaxMOV = 0;
  unsigned MaxDepth = 0;
  unsigned Colocate = 0;
  SUnit *ExceedPressure = nullptr;
  unsigned Latency = 0;

public:
  /// Const iterator over the SUnits in this node set.
  using iterator = SetVector<SUnit *>::const_iterator;

  /// Construct an empty node set.
  NodeSet() = default;

  /// Construct a recurrence node set from the SUnit range [\p S, \p E).
  ///
  /// Also computes the recurrence latency lower bound for the circuit using
  /// dependence edges from \p DAG.
  /// \param S Begin iterator of the recurrence circuit.
  /// \param E End iterator of the recurrence circuit.
  /// \param DAG Swing scheduler DAG providing the dependence graph.
  NodeSet(iterator S, iterator E, const SwingSchedulerDAG *DAG)
      : Nodes(S, E), HasRecurrence(true) {
    // Calculate the latency of this node set.
    // Example to demonstrate the calculation:
    // Given: N0 -> N1 -> N2 -> N0
    // Edges:
    // (N0 -> N1, 3)
    // (N0 -> N1, 5)
    // (N1 -> N2, 2)
    // (N2 -> N0, 1)
    // The total latency which is a lower bound of the recurrence MII is the
    // longest path from N0 back to N0 given only the edges of this node set.
    // In this example, the latency is: 5 + 2 + 1 = 8.
    //
    // Hold a map from each SUnit in the circle to the maximum distance from the
    // source node by only considering the nodes.
    const SwingSchedulerDDG *DDG = DAG->getDDG();
    DenseMap<SUnit *, unsigned> SUnitToDistance;
    for (auto *Node : Nodes)
      SUnitToDistance[Node] = 0;

    for (unsigned I = 1, E = Nodes.size(); I <= E; ++I) {
      SUnit *U = Nodes[I - 1];
      SUnit *V = Nodes[I % Nodes.size()];
      for (const SwingSchedulerDDGEdge &Succ : DDG->getOutEdges(U)) {
        SUnit *SuccSUnit = Succ.getDst();
        if (V != SuccSUnit)
          continue;
        unsigned &DU = SUnitToDistance[U];
        unsigned &DV = SUnitToDistance[V];
        if (DU + Succ.getLatency() > DV)
          DV = DU + Succ.getLatency();
      }
    }
    // Handle a back-edge in loop carried dependencies
    SUnit *FirstNode = Nodes[0];
    SUnit *LastNode = Nodes[Nodes.size() - 1];

    for (SUnit *SU : DDG->getExtraOutEdges(LastNode)) {
      // If we have an order dep that is potentially loop carried then a
      // back-edge exists between the last node and the first node in extra
      // edges. Handle it manually by adding 1 to the distance of the last node.
      if (SU != FirstNode)
        continue;
      unsigned &First = SUnitToDistance[FirstNode];
      unsigned Last = SUnitToDistance[LastNode];
      First = std::max(First, Last + 1);
    }

    // The latency is the distance from the source node to itself.
    Latency = SUnitToDistance[Nodes.front()];
  }

  /// Insert scheduling unit \p SU into the set.
  /// \param SU Node to insert.
  /// \return True if \p SU was newly inserted.
  bool insert(SUnit *SU) { return Nodes.insert(SU); }

  /// Insert the SUnit range [\p S, \p E) into the set.
  /// \param S Begin iterator of nodes to insert.
  /// \param E End iterator of nodes to insert.
  void insert(iterator S, iterator E) { Nodes.insert(S, E); }

  /// Remove every node for which predicate \p P returns true.
  /// \param P Unary predicate applied to each SUnit.
  /// \return True if any node was removed.
  template <typename UnaryPredicate> bool remove_if(UnaryPredicate P) {
    return Nodes.remove_if(P);
  }

  /// Return how many times \p SU occurs in the set (0 or 1).
  /// \param SU Node to count.
  /// \return 1 if \p SU is in the set, otherwise 0.
  unsigned count(SUnit *SU) const { return Nodes.count(SU); }

  /// Return true if this set represents a recurrence circuit.
  /// \return True if this set represents a recurrence circuit.
  bool hasRecurrence() { return HasRecurrence; };

  /// Return the number of nodes in the set.
  /// \return Number of nodes in the set.
  unsigned size() const { return Nodes.size(); }

  /// Return true if the set contains no nodes.
  /// \return True if the set contains no nodes.
  bool empty() const { return Nodes.empty(); }

  /// Return the node at index \p i.
  /// \param i Zero-based index into the set.
  /// \return Scheduling unit at index \p i.
  SUnit *getNode(unsigned i) const { return Nodes[i]; };

  /// Set the recurrence MII for this node set.
  /// \param mii Recurrence-constrained minimum initiation interval.
  void setRecMII(unsigned mii) { RecMII = mii; };

  /// Set the colocate group identifier used as a sort tie-breaker.
  /// \param c Colocate group value; zero means unset.
  void setColocate(unsigned c) { Colocate = c; };

  /// Record that scheduling \p SU would exceed register pressure.
  /// \param SU Node that causes the pressure limit to be exceeded.
  void setExceedPressure(SUnit *SU) { ExceedPressure = SU; }

  /// Return true if \p SU is the node that exceeds register pressure.
  /// \param SU Node to compare against the recorded pressure offender.
  /// \return True if \p SU is the recorded pressure offender.
  bool isExceedSU(SUnit *SU) { return ExceedPressure == SU; }

  /// Compare this set's recurrence MII against \p RHS.
  /// \param RHS Other node set.
  /// \return Difference of recurrence MIIs (this minus \p RHS).
  int compareRecMII(NodeSet &RHS) { return RecMII - RHS.RecMII; }

  /// Return the recurrence-constrained minimum initiation interval.
  /// \return Recurrence-constrained minimum initiation interval.
  int getRecMII() { return RecMII; }

  /// Summarize node functions for the entire node set.
  /// \param SSD Swing scheduler DAG providing per-node ASAP/ALAP metrics.
  void computeNodeSetInfo(SwingSchedulerDAG *SSD) {
    for (SUnit *SU : *this) {
      MaxMOV = std::max(MaxMOV, SSD->getMOV(SU));
      MaxDepth = std::max(MaxDepth, SSD->getDepth(SU));
    }
  }

  /// Return the recurrence latency computed for this node set.
  /// \return Recurrence latency for this node set.
  unsigned getLatency() { return Latency; }

  /// Return the maximum dependence-graph depth among nodes in the set.
  /// \return Maximum dependence-graph depth among nodes in the set.
  unsigned getMaxDepth() { return MaxDepth; }

  /// Clear the nodes and reset all priority metrics.
  void clear() {
    Nodes.clear();
    RecMII = 0;
    HasRecurrence = false;
    MaxMOV = 0;
    MaxDepth = 0;
    Colocate = 0;
    ExceedPressure = nullptr;
  }

  /// Convert this node set to the underlying SetVector of SUnits.
  /// \return Reference to the underlying SetVector of SUnits.
  operator SetVector<SUnit *> &() { return Nodes; }

  /// Return true if this node set should be scheduled before \p RHS.
  ///
  /// Sort the node sets by importance. First, rank them by recurrence MII,
  /// then by mobility (least mobile done first), and finally by depth.
  /// Each node set may contain a colocate value which is used as the first
  /// tie breaker, if it's set.
  /// \param RHS Other node set in the comparison.
  /// \return True if this set has higher scheduling priority than \p RHS.
  bool operator>(const NodeSet &RHS) const {
    if (RecMII == RHS.RecMII) {
      if (Colocate != 0 && RHS.Colocate != 0 && Colocate != RHS.Colocate)
        return Colocate < RHS.Colocate;
      if (MaxMOV == RHS.MaxMOV)
        return MaxDepth > RHS.MaxDepth;
      return MaxMOV < RHS.MaxMOV;
    }
    return RecMII > RHS.RecMII;
  }

  /// Return true if this set has the same priority key as \p RHS.
  /// \param RHS Other node set in the comparison.
  /// \return True if the priority keys are equal.
  bool operator==(const NodeSet &RHS) const {
    return RecMII == RHS.RecMII && MaxMOV == RHS.MaxMOV &&
           MaxDepth == RHS.MaxDepth;
  }

  /// Return true if this set does not have the same priority key as \p RHS.
  /// \param RHS Other node set in the comparison.
  /// \return True if the priority keys differ.
  bool operator!=(const NodeSet &RHS) const { return !operator==(RHS); }

  /// Return an iterator to the first node in the set.
  /// \return Iterator to the first node.
  iterator begin() { return Nodes.begin(); }
  /// Return an iterator past the last node in the set.
  /// \return Iterator past the last node.
  iterator end() { return Nodes.end(); }
  /// Print the node set to \p os.
  /// \param os Output stream.
  LLVM_ABI void print(raw_ostream &os) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump the node set to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif
};

// 16 was selected based on the number of ProcResource kinds for all
// existing Subtargets, so that SmallVector don't need to resize too often.
static const int DefaultProcResSize = 16;

/// Tracks processor resources consumed while building an SMS schedule.
class ResourceManager {
private:
  const MCSubtargetInfo *STI;
  const MCSchedModel &SM;
  const TargetSubtargetInfo *ST;
  const TargetInstrInfo *TII;
  ScheduleDAGInstrs *DAG;
  const bool UseDFA;
  /// DFA resources for each slot
  llvm::SmallVector<std::unique_ptr<DFAPacketizer>> DFAResources;
  /// Modulo Reservation Table. When a resource with ID R is consumed in cycle
  /// C, it is counted in MRT[C mod II][R]. (Used when UseDFA == F)
  llvm::SmallVector<llvm::SmallVector<uint64_t, DefaultProcResSize>> MRT;
  /// The number of scheduled micro operations for each slot. Micro operations
  /// are assumed to be scheduled one per cycle, starting with the cycle in
  /// which the instruction is scheduled.
  llvm::SmallVector<int> NumScheduledMops;
  /// Each processor resource is associated with a so-called processor resource
  /// mask. This vector allows to correlate processor resource IDs with
  /// processor resource masks. There is exactly one element per each processor
  /// resource declared by the scheduling model.
  llvm::SmallVector<uint64_t, DefaultProcResSize> ProcResourceMasks;
  int InitiationInterval = 0;
  /// The number of micro operations that can be scheduled at a cycle.
  int IssueWidth;

  int calculateResMIIDFA() const;
  /// Check if MRT is overbooked
  bool isOverbooked() const;
  /// Reserve resources on MRT
  void reserveResources(const MCSchedClassDesc *SCDesc, int Cycle);
  /// Unreserve resources on MRT
  void unreserveResources(const MCSchedClassDesc *SCDesc, int Cycle);

  /// Return M satisfying Dividend = Divisor * X + M, 0 < M < Divisor.
  /// The slot on MRT to reserve a resource for the cycle C is positiveModulo(C,
  /// II).
  int positiveModulo(int Dividend, int Divisor) const {
    assert(Divisor > 0);
    int R = Dividend % Divisor;
    if (R < 0)
      R += Divisor;
    return R;
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dumpMRT() const;
#endif

public:
  /// Construct a resource manager for subtarget \p ST and schedule DAG \p DAG.
  /// \param ST Target subtarget providing the scheduling model.
  /// \param DAG Schedule DAG whose instructions consume resources.
  ResourceManager(const TargetSubtargetInfo *ST, ScheduleDAGInstrs *DAG)
      : STI(ST), SM(ST->getSchedModel()), ST(ST), TII(ST->getInstrInfo()),
        DAG(DAG), UseDFA(ST->useDFAforSMS()),
        ProcResourceMasks(SM.getNumProcResourceKinds(), 0),
        IssueWidth(SM.IssueWidth) {
    initProcResourceVectors(SM, ProcResourceMasks);
    if (IssueWidth <= 0)
      // If IssueWidth is not specified, set a sufficiently large value
      IssueWidth = 100;
    if (SwpForceIssueWidth > 0)
      IssueWidth = SwpForceIssueWidth;
  }

  /// Initialize processor resource mask vectors from scheduling model \p SM.
  /// \param SM Machine scheduling model describing processor resources.
  /// \param Masks Output vector of per-resource bit masks.
  LLVM_ABI void initProcResourceVectors(const MCSchedModel &SM,
                                        SmallVectorImpl<uint64_t> &Masks);

  /// Check if the resources occupied by a machine instruction are available
  /// in the current state.
  /// \param SU Instruction whose resources are requested.
  /// \param Cycle Candidate schedule cycle.
  /// \return True if the resources for \p SU are available at \p Cycle.
  LLVM_ABI bool canReserveResources(SUnit &SU, int Cycle);

  /// Reserve the resources occupied by a machine instruction and change the
  /// current state to reflect that change.
  /// \param SU Instruction whose resources are reserved.
  /// \param Cycle Cycle at which the resources are consumed.
  LLVM_ABI void reserveResources(SUnit &SU, int Cycle);

  /// Compute the resource-constrained minimum initiation interval.
  /// \return Resource-constrained minimum initiation interval.
  LLVM_ABI int calculateResMII() const;

  /// Initialize resources with the initiation interval II.
  /// \param II Initiation interval used to size modulo resource tables.
  LLVM_ABI void init(int II);
};

/// Scheduled code for a software-pipelined loop.
///
/// The main data structure is a map from scheduled cycle to instructions.
/// During scheduling, the data structure explicitly represents all
/// stages/iterations. When the algorithm finishes, the schedule is collapsed
/// into a single stage, which represents instructions from different loop
/// iterations.
///
/// The SMS algorithm allows negative values for cycles, so the first cycle
/// in the schedule is the smallest cycle value.
class SMSchedule {
private:
  /// Map from execution cycle to instructions.
  DenseMap<int, std::deque<SUnit *>> ScheduledInstrs;

  /// Map from instruction to execution cycle.
  std::map<SUnit *, int> InstrToCycle;

  /// Keep track of the first cycle value in the schedule.  It starts
  /// as zero, but the algorithm allows negative values.
  int FirstCycle = 0;

  /// Keep track of the last cycle value in the schedule.
  int LastCycle = 0;

  /// The initiation interval (II) for the schedule.
  int InitiationInterval = 0;

  /// Target machine information.
  const TargetSubtargetInfo &ST;

  /// Virtual register information.
  MachineRegisterInfo &MRI;

  ResourceManager ProcItinResources;

public:
  /// Construct a schedule for \p mf using dependence info from \p DAG.
  /// \param mf Machine function being scheduled.
  /// \param DAG Swing scheduler DAG that owns the SUnits.
  SMSchedule(MachineFunction *mf, SwingSchedulerDAG *DAG)
      : ST(mf->getSubtarget()), MRI(mf->getRegInfo()),
        ProcItinResources(&ST, DAG) {}

  /// Clear scheduled instructions and reset cycle and II state.
  void reset() {
    ScheduledInstrs.clear();
    InstrToCycle.clear();
    FirstCycle = 0;
    LastCycle = 0;
    InitiationInterval = 0;
  }

  /// Set the initiation interval for this schedule.
  /// \param ii Initiation interval to use for resource tracking.
  void setInitiationInterval(int ii) {
    InitiationInterval = ii;
    ProcItinResources.init(ii);
  }

  /// Return the initiation interval for this schedule.
  /// \return Initiation interval for this schedule.
  int getInitiationInterval() const { return InitiationInterval; }

  /// Return the first cycle in the completed schedule.  This
  /// can be a negative value.
  /// \return First cycle in the schedule, which may be negative.
  int getFirstCycle() const { return FirstCycle; }

  /// Return the last cycle in the finalized schedule.
  /// \return Last cycle in the finalized schedule.
  int getFinalCycle() const { return FirstCycle + InitiationInterval - 1; }

  /// Compute earliest and latest legal start cycles for \p SU.
  /// \param SU Node whose schedule window is being computed.
  /// \param MaxEarlyStart Output: maximum of early-start constraints.
  /// \param MinLateStart Output: minimum of late-start constraints.
  /// \param II Initiation interval of the candidate schedule.
  /// \param DAG Dependence graph providing edge latencies and distances.
  LLVM_ABI void computeStart(SUnit *SU, int *MaxEarlyStart, int *MinLateStart,
                             int II, SwingSchedulerDAG *DAG);

  /// Try to schedule \p SU in [\p StartCycle, \p EndCycle) at II \p II.
  /// \param SU Node to insert into the schedule.
  /// \param StartCycle First cycle to try.
  /// \param EndCycle One past the last cycle to try.
  /// \param II Initiation interval of the candidate schedule.
  /// \return True if a legal cycle was found and reserved.
  LLVM_ABI bool insert(SUnit *SU, int StartCycle, int EndCycle, int II);

  /// Iterators for the cycle to instruction map.
  using sched_iterator = DenseMap<int, std::deque<SUnit *>>::iterator;
  /// Const iterator over the cycle-to-instruction map.
  using const_sched_iterator =
      DenseMap<int, std::deque<SUnit *>>::const_iterator;

  /// Return true if the instruction is scheduled at the specified stage.
  /// \param SU Scheduled instruction to query.
  /// \param StageNum Stage number to compare against.
  /// \return True if \p SU is scheduled at stage \p StageNum.
  bool isScheduledAtStage(SUnit *SU, unsigned StageNum) {
    return (stageScheduled(SU) == (int)StageNum);
  }

  /// Return the stage for a scheduled instruction.  Return -1 if
  /// the instruction has not been scheduled.
  /// \param SU Instruction whose stage is requested.
  /// \return Stage of \p SU, or -1 if it has not been scheduled.
  int stageScheduled(SUnit *SU) const {
    std::map<SUnit *, int>::const_iterator it = InstrToCycle.find(SU);
    if (it == InstrToCycle.end())
      return -1;
    return (it->second - FirstCycle) / InitiationInterval;
  }

  /// Return the cycle for a scheduled instruction. This function normalizes
  /// the first cycle to be 0.
  /// \param SU Instruction whose normalized cycle is requested.
  /// \return Normalized cycle of \p SU within the initiation interval.
  unsigned cycleScheduled(SUnit *SU) const {
    std::map<SUnit *, int>::const_iterator it = InstrToCycle.find(SU);
    assert(it != InstrToCycle.end() && "Instruction hasn't been scheduled.");
    return (it->second - FirstCycle) % InitiationInterval;
  }

  /// Return the maximum stage count needed for this schedule.
  /// \return Maximum stage count for the schedule.
  unsigned getMaxStageCount() {
    return (LastCycle - FirstCycle) / InitiationInterval;
  }

  /// Return the instructions that are scheduled at the specified cycle.
  /// \param cycle Absolute schedule cycle to look up.
  /// \return Deque of instructions scheduled at \p cycle.
  std::deque<SUnit *> &getInstructions(int cycle) {
    return ScheduledInstrs[cycle];
  }

  /// Compute nodes that cannot be pipelined for this loop.
  /// \param SSD Swing scheduler DAG for the loop.
  /// \param PLI Target-specific loop pipeliner info.
  /// \return Set of SUnits that must stay in a single iteration.
  LLVM_ABI SmallPtrSet<SUnit *, 8>
  computeUnpipelineableNodes(SwingSchedulerDAG *SSD,
                             TargetInstrInfo::PipelinerLoopInfo *PLI);

  /// Reorder \p Instrs to respect dependence constraints from \p SSD.
  /// \param SSD Swing scheduler DAG providing dependence edges.
  /// \param Instrs Instructions scheduled in one cycle.
  /// \return Reordered instruction sequence for that cycle.
  LLVM_ABI std::deque<SUnit *>
  reorderInstructions(const SwingSchedulerDAG *SSD,
                      const std::deque<SUnit *> &Instrs) const;

  /// Place unpipelineable instructions into a legal single-iteration region.
  /// \param SSD Swing scheduler DAG for the loop.
  /// \param PLI Target-specific loop pipeliner info.
  /// \return True if non-pipelined instructions were normalized successfully.
  LLVM_ABI bool
  normalizeNonPipelinedInstructions(SwingSchedulerDAG *SSD,
                                    TargetInstrInfo::PipelinerLoopInfo *PLI);

  /// Return true if the current schedule satisfies dependence constraints.
  /// \param SSD Swing scheduler DAG used to validate edges.
  /// \return True if dependence constraints are satisfied.
  LLVM_ABI bool isValidSchedule(SwingSchedulerDAG *SSD);

  /// Collapse stages into a final kernel schedule and apply instruction fixes.
  /// \param SSD Swing scheduler DAG that owns instruction-change state.
  LLVM_ABI void finalizeSchedule(SwingSchedulerDAG *SSD);

  /// Insert \p SU into \p Insts in an order consistent with dependences.
  /// \param SSD Swing scheduler DAG providing dependence edges.
  /// \param SU Instruction being ordered within the cycle.
  /// \param Insts Instructions already scheduled in the same cycle.
  LLVM_ABI void orderDependence(const SwingSchedulerDAG *SSD, SUnit *SU,
                                std::deque<SUnit *> &Insts) const;

  /// Return true if PHI \p Phi has a loop-carried dependence in this schedule.
  /// \param SSD Swing scheduler DAG for the loop.
  /// \param Phi PHI instruction to inspect.
  /// \return True if \p Phi has a loop-carried dependence.
  LLVM_ABI bool isLoopCarried(const SwingSchedulerDAG *SSD,
                              MachineInstr &Phi) const;

  /// Return true if \p Def is a loop-carried definition of use operand \p MO.
  /// \param SSD Swing scheduler DAG for the loop.
  /// \param Def Defining instruction to check.
  /// \param MO Use operand that may be fed by a loop-carried def.
  /// \return True if \p Def is a loop-carried definition of \p MO.
  LLVM_ABI bool isLoopCarriedDefOfUse(const SwingSchedulerDAG *SSD,
                                      MachineInstr *Def,
                                      MachineOperand &MO) const;

  /// Return true if \p SU has only loop-carried output or order predecessors.
  /// \param SU Node whose predecessors are inspected.
  /// \param DDG Dependence graph providing predecessor edges.
  /// \return True if all predecessors are loop-carried output or order deps.
  LLVM_ABI bool
  onlyHasLoopCarriedOutputOrOrderPreds(SUnit *SU,
                                       const SwingSchedulerDDG *DDG) const;

  /// Print the schedule to \p os.
  /// \param os Output stream.
  LLVM_ABI void print(raw_ostream &os) const;

  /// Dump the schedule to the debug stream.
  LLVM_ABI void dump() const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEPIPELINER_H
