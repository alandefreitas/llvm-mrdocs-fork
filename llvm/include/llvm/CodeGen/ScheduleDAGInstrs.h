//===- ScheduleDAGInstrs.h - MachineInstr Scheduling ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Implements the ScheduleDAGInstrs class, which implements scheduling
/// for a MachineInstr-based dependency graph.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULEDAGINSTRS_H
#define LLVM_CODEGEN_SCHEDULEDAGINSTRS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseMultiSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <list>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

  class AAResults;
  class LiveIntervals;
  class MachineFrameInfo;
  class MachineFunction;
  class MachineInstr;
  class MachineLoopInfo;
  class MachineOperand;
  struct MCSchedClassDesc;
  class PressureDiffs;
  class PseudoSourceValue;
  class RegPressureTracker;
  class UndefValue;
  class Value;

  /// An individual mapping from virtual register number to SUnit.
  struct VReg2SUnit {
    Register VirtReg;   ///< Virtual register number being tracked.
    LaneBitmask LaneMask; ///< Lanes of \p VirtReg associated with this entry.
    SUnit *SU;          ///< Scheduling unit that defines or uses the register.

    /// Constructs a mapping from \p VReg to \p SU for the given lanes.
    /// \param VReg Virtual register number.
    /// \param LaneMask Lanes of \p VReg associated with this entry.
    /// \param SU Scheduling unit that defines or uses the register.
    VReg2SUnit(Register VReg, LaneBitmask LaneMask, SUnit *SU)
      : VirtReg(VReg), LaneMask(LaneMask), SU(SU) {}

    /// Returns the sparse-set index for this virtual register.
    /// \return Sparse-set index for this virtual register.
    unsigned getSparseSetIndex() const {
      return VirtReg.virtRegIndex();
    }
  };

  /// Mapping from virtual register to SUnit including an operand index.
  struct VReg2SUnitOperIdx : public VReg2SUnit {
    unsigned OperandIndex; ///< Operand index within the machine instruction.

    /// Constructs a mapping from \p VReg to \p SU with an operand index.
    /// \param VReg Virtual register number.
    /// \param LaneMask Lanes of \p VReg associated with this entry.
    /// \param OperandIndex Operand index within the machine instruction.
    /// \param SU Scheduling unit that defines or uses the register.
    VReg2SUnitOperIdx(Register VReg, LaneBitmask LaneMask,
                      unsigned OperandIndex, SUnit *SU)
      : VReg2SUnit(VReg, LaneMask, SU), OperandIndex(OperandIndex) {}
  };

  /// Record a physical register access.
  /// For non-data-dependent uses, OpIdx == -1.
  struct PhysRegSUOper {
    SUnit *SU;          ///< Scheduling unit that accesses the register unit.
    int OpIdx;          ///< Operand index, or -1 for non-data-dependent uses.
    MCRegUnit RegUnit;  ///< Physical register unit being accessed.

    /// Constructs a physical-register access record.
    /// \param su Scheduling unit that accesses the register unit.
    /// \param op Operand index, or -1 for non-data-dependent uses.
    /// \param R Physical register unit being accessed.
    PhysRegSUOper(SUnit *su, int op, MCRegUnit R)
        : SU(su), OpIdx(op), RegUnit(R) {}

    /// Returns the sparse-set index for this register unit.
    /// \return Sparse-set index for this register unit.
    unsigned getSparseSetIndex() const {
      return static_cast<unsigned>(RegUnit);
    }
  };

  /// Use a SparseMultiSet to track physical registers. Storage is only
  /// allocated once for the pass. It can be cleared in constant time and reused
  /// without any frees.
  using RegUnit2SUnitsMap =
      SparseMultiSet<PhysRegSUOper, MCRegUnit, MCRegUnitToIndex, uint16_t>;

  /// Sparse multi-map from virtual registers to their local SUnit uses.
  ///
  /// These uses are gathered by the DAG builder and may be consulted by the
  /// scheduler to avoid iterating an entire vreg use list.
  using VReg2SUnitMultiMap =
      SparseMultiSet<VReg2SUnit, Register, VirtReg2IndexFunctor>;

  /// Sparse multi-map from virtual registers to SUnits with operand indices.
  using VReg2SUnitOperIdxMultiMap =
      SparseMultiSet<VReg2SUnitOperIdx, Register, VirtReg2IndexFunctor>;

  /// Pointer to an IR Value or PseudoSourceValue used as a memory location key.
  using ValueType = PointerUnion<const Value *, const PseudoSourceValue *>;

  /// An underlying memory object paired with whether it may alias others.
  struct UnderlyingObject : PointerIntPair<ValueType, 1, bool> {
    /// Constructs an underlying object from \p V and aliasability.
    /// \param V Value or PseudoSourceValue identifying the object.
    /// \param MayAlias True if this object may alias other memory objects.
    UnderlyingObject(ValueType V, bool MayAlias)
        : PointerIntPair<ValueType, 1, bool>(V, MayAlias) {}

    /// Returns the Value or PseudoSourceValue identifying this object.
    /// \return Value or PseudoSourceValue identifying this object.
    ValueType getValue() const { return getPointer(); }
    /// Returns true if this object may alias other memory objects.
    /// \return True if this object may alias other memory objects.
    bool mayAlias() const { return getInt(); }
  };

  /// Small vector of underlying memory objects for a memory operand.
  using UnderlyingObjectsVector = SmallVector<UnderlyingObject, 4>;

  /// A ScheduleDAG for scheduling lists of MachineInstr.
  class LLVM_ABI ScheduleDAGInstrs : public ScheduleDAG {
  protected:
    const MachineLoopInfo *MLI = nullptr; ///< Loop information for the function.
    const MachineFrameInfo &MFI;          ///< Frame information for the function.

    /// TargetSchedModel provides an interface to the machine model.
    TargetSchedModel SchedModel;

    /// True if the DAG builder should remove kill flags (in preparation for
    /// rescheduling).
    bool RemoveKillFlags;

    /// True if regions with a single MI should be scheduled.
    bool ScheduleSingleMIRegions = false;

    /// True if this scheduler can safely include terminators as DAG nodes.
    ///
    /// The standard DAG builder does not normally include terminators as DAG
    /// nodes because it does not create the necessary dependencies to prevent
    /// reordering. A specialized scheduler can override
    /// TargetInstrInfo::isSchedulingBoundary then enable this flag to indicate
    /// it has taken responsibility for scheduling the terminator correctly.
    bool CanHandleTerminators = false;

    /// Whether lane masks should get tracked.
    bool TrackLaneMasks = false;

    // State specific to the current scheduling region.
    // ------------------------------------------------

    /// The block in which to insert instructions
    MachineBasicBlock *BB = nullptr;

    /// The beginning of the range to be scheduled.
    MachineBasicBlock::iterator RegionBegin;

    /// The end of the range to be scheduled.
    MachineBasicBlock::iterator RegionEnd;

    /// Instructions in this region (distance(RegionBegin, RegionEnd)).
    unsigned NumRegionInstrs = 0;

    /// After calling BuildSchedGraph, each machine instruction in the current
    /// scheduling region is mapped to an SUnit.
    DenseMap<MachineInstr*, SUnit*> MISUnitMap;

    /// Number of memory operations processed while building the DAG.
    unsigned MemOpsProcessed = 0;

    // State internal to DAG building.
    // -------------------------------

    /// Map from register units to SUnits that define them in the current walk.
    ///
    /// Remember where defs of each register are as we iterate upward through
    /// the instructions. This is allocated here instead of inside
    /// BuildSchedGraph to avoid the need for it to be initialized and
    /// destructed for each block.
    RegUnit2SUnitsMap Defs;
    /// Map from register units to SUnits that use them in the current walk.
    RegUnit2SUnitsMap Uses;

    /// Tracks the last instruction(s) in this region defining each virtual
    /// register. There may be multiple current definitions for a register with
    /// disjunct lanemasks.
    VReg2SUnitMultiMap CurrentVRegDefs;
    /// Tracks the last instructions in this region using each virtual register.
    VReg2SUnitOperIdxMultiMap CurrentVRegUses;

    /// Optional batch alias-analysis results used while adding memory deps.
    mutable std::optional<BatchAAResults> AAForDep;

    /// Generic side-effecting instruction that acts as a scheduling barrier.
    ///
    /// No other SU ever gets scheduled around it (except in the special case of
    /// a huge region that gets reduced).
    SUnit *BarrierChain = nullptr;

    /// Clusters of related SUnits discovered while building the DAG.
    SmallVector<ClusterInfo> Clusters;

  public:
    /// List of SUnits associated with a memory value during DAG construction.
    ///
    /// Used in Value2SUsMap. Note: to gain speed it might be worth
    /// investigating an optimized implementation of this data structure, such
    /// as a singly linked list with a memory pool (SmallVector was tried but
    /// slow and SparseSet is not applicable).
    using SUList = std::list<SUnit *>;

    /// The direction that should be used to dump the scheduled Sequence.
    enum DumpDirection {
      TopDown,       ///< Dump the sequence from top to bottom.
      BottomUp,      ///< Dump the sequence from bottom to top.
      Bidirectional, ///< Dump the sequence in both directions.
      NotSet,        ///< Dump direction has not been configured.
    };

    /// Sets the direction used when dumping the scheduled sequence.
    /// \param D Dump direction to use.
    void setDumpDirection(DumpDirection D) { DumpDir = D; }

  protected:
    DumpDirection DumpDir = NotSet; ///< Direction used when dumping the schedule.

    /// A map from ValueType to SUList, used during DAG construction, as
    /// a means of remembering which SUs depend on which memory locations.
    class Value2SUsMap;

    /// Returns a (possibly null) pointer to the current BatchAAResults.
    /// \return Pointer to the current BatchAAResults, or nullptr if none.
    BatchAAResults *getAAForDep() const {
      if (AAForDep.has_value())
        return &AAForDep.value();
      return nullptr;
    }

    /// Adds a chain edge between SUa and SUb, but only if both
    /// AAResults and Target fail to deny the dependency.
    /// \param SUa Predecessor scheduling unit.
    /// \param SUb Successor scheduling unit.
    /// \param Latency Edge latency in cycles.
    void addChainDependency(SUnit *SUa, SUnit *SUb,
                            unsigned Latency = 0);

    /// Adds dependencies as needed from all SUs in list to SU.
    /// \param SU Successor scheduling unit receiving the dependencies.
    /// \param SUs Predecessor scheduling units to depend on.
    /// \param Latency Edge latency in cycles.
    void addChainDependencies(SUnit *SU, SUList &SUs, unsigned Latency) {
      for (SUnit *Entry : SUs)
        addChainDependency(SU, Entry, Latency);
    }

    /// Adds dependencies as needed from all SUs in map, to SU.
    /// \param SU Successor scheduling unit receiving the dependencies.
    /// \param Val2SUsMap Map of memory values to predecessor SUnits.
    void addChainDependencies(SUnit *SU, Value2SUsMap &Val2SUsMap);

    /// Adds dependencies as needed to SU, from all SUs mapped to V.
    /// \param SU Successor scheduling unit receiving the dependencies.
    /// \param Val2SUsMap Map of memory values to predecessor SUnits.
    /// \param V Memory value whose associated SUnits become predecessors.
    void addChainDependencies(SUnit *SU, Value2SUsMap &Val2SUsMap,
                              ValueType V);

    /// Adds barrier-chain edges from all SUs in \p map, then clears the map.
    ///
    /// This is equivalent to insertBarrierChain(), but optimized for the common
    /// case where the new BarrierChain (a global memory object) has a higher
    /// NodeNum than all SUs in map. It is assumed BarrierChain has been set
    /// before calling this.
    /// \param map Map of memory values whose SUnits must order after the
    /// barrier.
    void addBarrierChain(Value2SUsMap &map);

    /// For an unanalyzable memory access, this Value is used in maps.
    UndefValue *UnknownValue;


    /// Topo - A topological ordering for SUnits which permits fast IsReachable
    /// and similar queries.
    ScheduleDAGTopologicalSort Topo;

    /// Pairs of DBG_VALUE instructions and the instructions they follow.
    using DbgValueVector =
        std::vector<std::pair<MachineInstr *, MachineInstr *>>;
    /// Remember instruction that precedes DBG_VALUE.
    /// These are generated by buildSchedGraph but persist so they can be
    /// referenced when emitting the final schedule.
    DbgValueVector DbgValues;
    /// First DBG_VALUE instruction preceding the scheduled region, if any.
    MachineInstr *FirstDbgValue = nullptr;

    /// Set of live physical registers for updating kill flags.
    LiveRegUnits LiveRegs;

  public:
    /// Constructs a MachineInstr scheduling DAG for \p mf.
    /// \param mf Machine function being scheduled.
    /// \param mli Optional loop information for the function.
    /// \param RemoveKillFlags If true, kill flags are cleared for rescheduling.
    explicit ScheduleDAGInstrs(MachineFunction &mf,
                               const MachineLoopInfo *mli,
                               bool RemoveKillFlags = false);

    /// Destroys the scheduling DAG.
    ~ScheduleDAGInstrs() override = default;

    /// Gets the machine model for instruction scheduling.
    /// \return Pointer to the target scheduling model.
    const TargetSchedModel *getSchedModel() const { return &SchedModel; }

    /// Resolves and cache a resolved scheduling class for an SUnit.
    /// \param SU Scheduling unit whose scheduling class is resolved.
    /// \return Cached or newly resolved scheduling class for \p SU.
    const MCSchedClassDesc *getSchedClass(SUnit *SU) const {
      if (!SU->SchedClass && SchedModel.hasInstrSchedModel())
        SU->SchedClass = SchedModel.resolveSchedClass(SU->getInstr());
      return SU->SchedClass;
    }

    /// IsReachable - Checks if SU is reachable from TargetSU.
    /// \param SU Candidate successor scheduling unit.
    /// \param TargetSU Candidate predecessor scheduling unit.
    /// \return True if \p SU is reachable from \p TargetSU.
    bool IsReachable(SUnit *SU, SUnit *TargetSU) {
      return Topo.IsReachable(SU, TargetSU);
    }

    /// Whether regions with a single MI should be scheduled.
    /// \return True if regions with a single MI should be scheduled.
    bool shouldScheduleSingleMIRegions() const {
      return ScheduleSingleMIRegions;
    }

    /// Returns an iterator to the top of the current scheduling region.
    /// \return Iterator to the first instruction in the current region.
    MachineBasicBlock::iterator begin() const { return RegionBegin; }

    /// Returns an iterator to the bottom of the current scheduling region.
    /// \return Iterator past the last instruction in the current region.
    MachineBasicBlock::iterator end() const { return RegionEnd; }

    /// Creates a new SUnit and return a ptr to it.
    /// \param MI Machine instruction represented by the new SUnit.
    /// \return Pointer to the newly created SUnit.
    SUnit *newSUnit(MachineInstr *MI);

    /// Returns an existing SUnit for this MI, or nullptr.
    /// \param MI Machine instruction to look up.
    /// \return Existing SUnit for \p MI, or nullptr if none.
    SUnit *getSUnit(MachineInstr *MI) const;

    /// Returns true if MBB scheduling regions should be handled top-down.
    ///
    /// If this method returns true, handling of the scheduling regions
    /// themselves (in case of a scheduling boundary in MBB) will be done
    /// beginning with the topmost region of MBB.
    /// \return True if MBB scheduling regions should be handled top-down.
    virtual bool doMBBSchedRegionsTopDown() const { return false; }

    /// Prepares to perform scheduling in the given block.
    /// \param BB Basic block about to be scheduled.
    virtual void startBlock(MachineBasicBlock *BB);

    /// Cleans up after scheduling in the given block.
    virtual void finishBlock();

    /// Initializes DAG and scheduler state for a new scheduling region.
    ///
    /// This does not actually create the DAG, only clears it. The scheduling
    /// driver may call BuildSchedGraph multiple times per scheduling region.
    /// \param bb Basic block containing the region.
    /// \param begin Iterator to the first instruction in the region.
    /// \param end Iterator past the last instruction in the region.
    /// \param regioninstrs Number of instructions in the region.
    virtual void enterRegion(MachineBasicBlock *bb,
                             MachineBasicBlock::iterator begin,
                             MachineBasicBlock::iterator end,
                             unsigned regioninstrs);

    /// Called when the scheduler has finished scheduling the current region.
    virtual void exitRegion();

    /// Builds SUnits for the current scheduling region.
    ///
    /// If \p RPTracker is non-null, compute register pressure as a side effect.
    /// The DAG builder is an efficient place to do it because it already visits
    /// operands.
    /// \param AA Alias analysis results used for memory dependence edges.
    /// \param RPTracker Optional register-pressure tracker updated as a side
    /// effect.
    /// \param PDiffs Optional per-instruction pressure diffs to populate.
    /// \param LIS Optional live-interval info consulted while building deps.
    /// \param TrackLaneMasks Whether lane masks should be tracked for vregs.
    void buildSchedGraph(AAResults *AA,
                         RegPressureTracker *RPTracker = nullptr,
                         PressureDiffs *PDiffs = nullptr,
                         LiveIntervals *LIS = nullptr,
                         bool TrackLaneMasks = false);

    /// Adds dependencies from region instructions to the scheduling barrier.
    ///
    /// We want to make sure instructions which define registers that are either
    /// used by the terminator or are live-out are properly scheduled. This is
    /// especially important when the definition latency of the return value(s)
    /// are too high to be hidden by the branch or when the liveout registers
    /// used by instructions in the fallthrough block.
    void addSchedBarrierDeps();

    /// Orders nodes according to selected style.
    ///
    /// Typically, a scheduling algorithm will implement schedule() without
    /// overriding enterRegion() or exitRegion().
    virtual void schedule() = 0;

    /// Allow targets to perform final scheduling actions at the level of the
    /// whole MachineFunction. By default does nothing.
    virtual void finalizeSchedule() {}

    /// Dumps a single scheduling unit for debugging.
    /// \param SU Scheduling unit to dump.
    void dumpNode(const SUnit &SU) const override;
    /// Dumps the scheduling DAG for debugging.
    void dump() const override;

    /// Returns a label for a DAG node that points to an instruction.
    /// \param SU Scheduling unit whose graph label is requested.
    /// \return Graph label string for the instruction pointed to by \p SU.
    std::string getGraphNodeLabel(const SUnit *SU) const override;

    /// Returns a label for the region of code covered by the DAG.
    /// \return Label describing the region of code covered by the DAG.
    std::string getDAGName() const override;

    /// Fixes register kill flags that scheduling has made invalid.
    /// \param MBB Basic block whose kill flags are repaired.
    void fixupKills(MachineBasicBlock &MBB);

    /// True if an edge can be added from PredSU to SuccSU without creating
    /// a cycle.
    /// \param SuccSU Candidate successor scheduling unit.
    /// \param PredSU Candidate predecessor scheduling unit.
    /// \return True if the edge can be added without creating a cycle.
    bool canAddEdge(SUnit *SuccSU, SUnit *PredSU);

    /// Add a DAG edge to the given SU with the given predecessor
    /// dependence data.
    ///
    /// \param SuccSU Successor scheduling unit receiving the edge.
    /// \param PredDep Predecessor dependence describing the edge to add.
    /// \returns true if the edge may be added without creating a cycle OR if an
    /// equivalent edge already existed (false indicates failure).
    bool addEdge(SUnit *SuccSU, const SDep &PredDep);

    /// Returns the array of the clusters.
    /// \return Reference to the vector of clusters discovered while building.
    SmallVector<ClusterInfo> &getClusters() { return Clusters; }

    /// Get the specific cluster, return nullptr for InvalidClusterId.
    /// \param Idx Cluster index, or InvalidClusterId for none.
    /// \return Pointer to the cluster at \p Idx, or nullptr for InvalidClusterId.
    ClusterInfo *getCluster(unsigned Idx) {
      return Idx != InvalidClusterId ? &Clusters[Idx] : nullptr;
    }

  protected:
    /// Creates an SUnit for each instruction in the current region.
    void initSUnits();
    /// Adds data dependencies for the physical-register operand at \p OperIdx.
    /// \param SU Scheduling unit whose operand is analyzed.
    /// \param OperIdx Operand index of the physical-register access.
    void addPhysRegDataDeps(SUnit *SU, unsigned OperIdx);
    /// Adds all physical-register dependencies for the operand at \p OperIdx.
    /// \param SU Scheduling unit whose operand is analyzed.
    /// \param OperIdx Operand index of the physical-register access.
    void addPhysRegDeps(SUnit *SU, unsigned OperIdx);
    /// Adds virtual-register definition dependencies for the operand at
    /// \p OperIdx.
    /// \param SU Scheduling unit whose operand is analyzed.
    /// \param OperIdx Operand index of the virtual-register definition.
    void addVRegDefDeps(SUnit *SU, unsigned OperIdx);
    /// Adds virtual-register use dependencies for the operand at \p OperIdx.
    /// \param SU Scheduling unit whose operand is analyzed.
    /// \param OperIdx Operand index of the virtual-register use.
    void addVRegUseDeps(SUnit *SU, unsigned OperIdx);

    /// Returns a mask for which lanes get read/written by the given (register)
    /// machine operand.
    /// \param MO Machine operand whose lane mask is computed.
    /// \return Lane mask of lanes read or written by \p MO.
    LaneBitmask getLaneMaskForMO(const MachineOperand &MO) const;

    /// Returns true if the def register in \p MO has no uses.
    /// \param MO Def machine operand to test for deadness without uses.
    /// \return True if the def register in \p MO has no uses.
    bool deadDefHasNoUse(const MachineOperand &MO);
  };

  /// Creates a new SUnit and return a ptr to it.
  /// \param MI Machine instruction represented by the new SUnit.
  /// \return Pointer to the newly created SUnit.
  inline SUnit *ScheduleDAGInstrs::newSUnit(MachineInstr *MI) {
#ifndef NDEBUG
    const SUnit *Addr = SUnits.empty() ? nullptr : &SUnits[0];
#endif
    SUnits.emplace_back(MI, (unsigned)SUnits.size());
    assert((Addr == nullptr || Addr == &SUnits[0]) &&
           "SUnits std::vector reallocated on the fly!");
    return &SUnits.back();
  }

  /// Returns an existing SUnit for this MI, or nullptr.
  /// \param MI Machine instruction to look up.
  /// \return Existing SUnit for \p MI, or nullptr if none.
  inline SUnit *ScheduleDAGInstrs::getSUnit(MachineInstr *MI) const {
    return MISUnitMap.lookup(MI);
  }

} // end namespace llvm

#endif // LLVM_CODEGEN_SCHEDULEDAGINSTRS_H
