//===- llvm/CodeGen/ScheduleDAG.h - Common Base Class -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Implements the ScheduleDAG class, which is used as the common base
/// class for instruction schedulers. This encapsulates the scheduling DAG,
/// which is shared between SelectionDAG and MachineInstr scheduling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULEDAG_H
#define LLVM_CODEGEN_SCHEDULEDAG_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

namespace llvm {

template <class GraphType> struct GraphTraits;
template<class Graph> class GraphWriter;
class TargetMachine;
class MachineFunction;
class MachineRegisterInfo;
class MCInstrDesc;
struct MCSchedClassDesc;
class SDNode;
class SUnit;
class ScheduleDAG;
class TargetInstrInfo;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class TargetRegisterInfo;

  /// Scheduling dependency. This represents one direction of an edge in the
  /// scheduling DAG.
  class SDep {
  public:
    /// These are the different kinds of scheduling dependencies.
    enum Kind {
      Data,        ///< Regular data dependence (aka true-dependence).
      Anti,        ///< A register anti-dependence (aka WAR).
      Output,      ///< A register output-dependence (aka WAW).
      Order        ///< Any other ordering dependency.
    };

    /// Ordering-dependence subtypes for \c SDep::Order edges.
    ///
    /// Strong dependencies must be respected by the scheduler. Artificial
    /// dependencies may be removed only if they are redundant with another
    /// strong dependence.
    ///
    /// Weak dependencies may be violated by the scheduling strategy, but only
    /// if the strategy can prove it is correct to do so.
    ///
    /// Strong OrderKinds must occur before "Weak".
    /// Weak OrderKinds must occur after "Weak".
    enum OrderKind {
      Barrier,      ///< An unknown scheduling barrier.
      MayAliasMem,  ///< Nonvolatile load/Store instructions that may alias.
      MustAliasMem, ///< Nonvolatile load/Store instructions that must alias.
      Artificial,   ///< Arbitrary strong DAG edge (no real dependence).
      Weak,         ///< Arbitrary weak DAG edge.
      Cluster       ///< Weak DAG edge linking a chain of clustered instrs.
    };

  private:
    /// A pointer to the depending/depended-on SUnit, and an enum
    /// indicating the kind of the dependency.
    PointerIntPair<SUnit *, 2, Kind> Dep;

    /// A union discriminated by the dependence kind.
    union {
      /// For Data, Anti, and Output dependencies, the associated register. For
      /// Data dependencies that don't currently have a register/ assigned, this
      /// is set to zero.
      unsigned Reg;

      /// Additional information about Order dependencies.
      unsigned OrdKind; // enum OrderKind
    } Contents;

    /// The time associated with this edge. Often this is just the value of the
    /// Latency field of the predecessor, however advanced models may provide
    /// additional information about specific edges.
    unsigned Latency = 0u;

  public:
    /// Constructs a null SDep. This is only for use by container classes which
    /// require default constructors. SUnits may not/ have null SDep edges.
    SDep() : Dep(nullptr, Data) {}

    /// Constructs an SDep with the specified values.
    /// \param S The SUnit this edge points to.
    /// \param kind Dependence kind (Data, Anti, or Output).
    /// \param Reg Associated register; must be non-zero for Anti/Output.
    SDep(SUnit *S, Kind kind, Register Reg) : Dep(S, kind), Contents() {
      switch (kind) {
      default:
        llvm_unreachable("Reg given for non-register dependence!");
      case Anti:
      case Output:
        assert(Reg && "SDep::Anti and SDep::Output must use a non-zero Reg!");
        Contents.Reg = Reg.id();
        Latency = 0;
        break;
      case Data:
        Contents.Reg = Reg.id();
        Latency = 1;
        break;
      }
    }

    /// Constructs an Order dependence of the given subtype.
    /// \param S The SUnit this edge points to.
    /// \param kind Order-dependence subtype.
    SDep(SUnit *S, OrderKind kind)
      : Dep(S, Order), Contents(), Latency(0) {
      Contents.OrdKind = kind;
    }

    /// Returns true if the specified SDep is equivalent except for latency.
    /// \param Other Dependence to compare against.
    /// \return True if the dependences match ignoring latency.
    bool overlaps(const SDep &Other) const;

    /// Returns true if this dependence equals \p Other, including latency.
    /// \param Other Dependence to compare against.
    /// \return True if the dependences are identical including latency.
    bool operator==(const SDep &Other) const {
      return overlaps(Other) && Latency == Other.Latency;
    }

    /// Returns true if this dependence differs from \p Other.
    /// \param Other Dependence to compare against.
    /// \return True if the dependences are not identical.
    bool operator!=(const SDep &Other) const {
      return !operator==(Other);
    }

    /// Returns the latency value for this edge.
    ///
    /// Latency roughly means the minimum number of cycles that must elapse
    /// between the predecessor and the successor, given that they have this
    /// edge between them.
    /// \return Latency in cycles for this edge.
    unsigned getLatency() const {
      return Latency;
    }

    /// Sets the latency for this edge.
    /// \param Lat New latency in cycles.
    void setLatency(unsigned Lat) {
      Latency = Lat;
    }

    /// Returns the SUnit to which this edge points.
    /// \return Depending/depended-on SUnit for this edge.
    SUnit *getSUnit() const;

    /// Assigns the SUnit to which this edge points.
    /// \param SU SUnit this edge should point to.
    void setSUnit(SUnit *SU);

    /// Returns an enum value representing the kind of the dependence.
    /// \return Dependence kind (Data, Anti, Output, or Order).
    Kind getKind() const;

    /// Shorthand for getKind() != SDep::Data.
    /// \return True if this is not a Data dependence.
    bool isCtrl() const {
      return getKind() != Data;
    }

    /// Tests if this is an Order dependence between two memory accesses
    /// where both sides of the dependence access memory in non-volatile and
    /// fully modeled ways.
    /// \return True if this is a may-alias or must-alias memory Order edge.
    bool isNormalMemory() const {
      return getKind() == Order && (Contents.OrdKind == MayAliasMem
                                    || Contents.OrdKind == MustAliasMem);
    }

    /// Tests if this is an Order dependence that is marked as a barrier.
    /// \return True if this is a Barrier Order dependence.
    bool isBarrier() const {
      return getKind() == Order && Contents.OrdKind == Barrier;
    }

    /// Tests if this is could be any kind of memory dependence.
    /// \return True if this is a normal memory or barrier Order edge.
    bool isNormalMemoryOrBarrier() const {
      return (isNormalMemory() || isBarrier());
    }

    /// Tests if this is a must-alias Order dependence.
    ///
    /// This is an Order dependence that is marked as "must alias", meaning that
    /// the SUnits at either end of the edge have a memory dependence on a known
    /// memory location.
    /// \return True if this is a MustAliasMem Order dependence.
    bool isMustAlias() const {
      return getKind() == Order && Contents.OrdKind == MustAliasMem;
    }

    /// Tests if this is a weak dependence.
    ///
    /// Weak dependencies are considered DAG edges for height computation and
    /// other heuristics, but do not force ordering. Breaking a weak edge may
    /// require the scheduler to compensate, for example by inserting a copy.
    /// \return True if this is a Weak or Cluster Order dependence.
    bool isWeak() const {
      return getKind() == Order && Contents.OrdKind >= Weak;
    }

    /// Tests if this is an Order dependence that is marked as
    /// "artificial", meaning it isn't necessary for correctness.
    /// \return True if this is an Artificial Order dependence.
    bool isArtificial() const {
      return getKind() == Order && Contents.OrdKind == Artificial;
    }

    /// Tests if this is an Order dependence that is marked as "cluster",
    /// meaning it is artificial and wants to be adjacent.
    /// \return True if this is a Cluster Order dependence.
    bool isCluster() const {
      return getKind() == Order && Contents.OrdKind == Cluster;
    }

    /// Tests if this is a Data dependence that is associated with a register.
    /// \return True if this is a Data edge with a non-zero register.
    bool isAssignedRegDep() const { return getKind() == Data && Contents.Reg; }

    /// Returns the register associated with this edge.
    ///
    /// This is only valid on Data, Anti, and Output edges. On Data edges, this
    /// value may be zero, meaning there is no associated register.
    /// \return Register associated with this edge, or zero on some Data edges.
    Register getReg() const {
      assert((getKind() == Data || getKind() == Anti || getKind() == Output) &&
             "getReg called on non-register dependence edge!");
      return Contents.Reg;
    }

    /// Assigns the associated register for this edge.
    ///
    /// This is only valid on Data, Anti, and Output edges. On Anti and Output
    /// edges, this value must not be zero. On Data edges, the value may be
    /// zero, which would mean that no specific register is associated with this
    /// edge.
    /// \param Reg Register to associate with this edge.
    void setReg(Register Reg) {
      assert((getKind() == Data || getKind() == Anti || getKind() == Output) &&
             "setReg called on non-register dependence edge!");
      assert((getKind() != Anti || Reg) &&
             "SDep::Anti edge cannot use the zero register!");
      assert((getKind() != Output || Reg) &&
             "SDep::Output edge cannot use the zero register!");
      Contents.Reg = Reg.id();
    }

    /// Dump this dependence for debugging, optionally with register names.
    /// \param TRI Optional target register info for pretty-printing registers.
    LLVM_ABI void dump(const TargetRegisterInfo *TRI = nullptr) const;
  };

  /// Keep record of which SUnit are in the same cluster group.
  typedef SmallPtrSet<SUnit *, 8> ClusterInfo;
  /// Sentinel cluster id meaning "not in a cluster".
  constexpr unsigned InvalidClusterId = ~0u;

  /// Return whether the input cluster ID's are the same and valid.
  /// \param A First cluster id.
  /// \param B Second cluster id.
  /// \return True if both ids are valid and equal.
  inline bool isTheSameCluster(unsigned A, unsigned B) {
    return A != InvalidClusterId && A == B;
  }

  /// Scheduling unit. This is a node in the scheduling DAG.
  class SUnit {
  private:
    enum : unsigned { BoundaryID = ~0u };

    union {
      SDNode *Node;        ///< Representative node.
      MachineInstr *Instr; ///< Alternatively, a MachineInstr.
    };

  public:
    SUnit *OrigNode = nullptr; ///< If not this, the node from which this node
                               /// was cloned. (SD scheduling only)

    const MCSchedClassDesc *SchedClass =
        nullptr; ///< nullptr or resolved SchedClass.

    /// Destination register class for a special copy node, or nullptr.
    const TargetRegisterClass *CopyDstRC = nullptr;
    /// Source register class for a special copy node, or nullptr.
    const TargetRegisterClass *CopySrcRC = nullptr;

    SmallVector<SDep, 4> Preds;  ///< All sunit predecessors.
    SmallVector<SDep, 4> Succs;  ///< All sunit successors.

    /// Mutable iterator over predecessor dependence edges.
    typedef SmallVectorImpl<SDep>::iterator pred_iterator;
    /// Mutable iterator over successor dependence edges.
    typedef SmallVectorImpl<SDep>::iterator succ_iterator;
    /// Const iterator over predecessor dependence edges.
    typedef SmallVectorImpl<SDep>::const_iterator const_pred_iterator;
    /// Const iterator over successor dependence edges.
    typedef SmallVectorImpl<SDep>::const_iterator const_succ_iterator;

    unsigned NodeNum = BoundaryID;     ///< Entry # of node in the node vector.
    unsigned NodeQueueId = 0;          ///< Queue id of node.
    unsigned NumPreds = 0;             ///< # of SDep::Data preds.
    unsigned NumSuccs = 0;             ///< # of SDep::Data sucss.
    unsigned NumPredsLeft = 0;         ///< # of preds not scheduled.
    unsigned NumSuccsLeft = 0;         ///< # of succs not scheduled.
    unsigned WeakPredsLeft = 0;        ///< # of weak preds not scheduled.
    unsigned WeakSuccsLeft = 0;        ///< # of weak succs not scheduled.
    unsigned TopReadyCycle = 0; ///< Cycle relative to start when node is ready.
    unsigned BotReadyCycle = 0; ///< Cycle relative to end when node is ready.

    unsigned ParentClusterIdx = InvalidClusterId; ///< The parent cluster id.

  private:
    unsigned Depth = 0;  ///< Node depth.
    unsigned Height = 0; ///< Node height.

  public:
    bool isVRegCycle      : 1;         ///< May use and def the same vreg.
    bool isCall           : 1;         ///< Is a function call.
    bool isCallOp         : 1;         ///< Is a function call operand.
    bool isTwoAddress     : 1;         ///< Is a two-address instruction.
    bool isCommutable     : 1;         ///< Is a commutable instruction.
    bool hasPhysRegUses   : 1;         ///< Has physreg uses.
    bool hasPhysRegDefs   : 1;         ///< Has physreg defs that are being used.
    bool hasPhysRegClobbers : 1;       ///< Has any physreg defs, used or not.
    bool isPending        : 1;         ///< True once pending.
    bool isAvailable      : 1;         ///< True once available.
    bool isScheduled      : 1;         ///< True once scheduled.
    bool isScheduleHigh   : 1;         ///< True if preferable to schedule high.
    bool isScheduleLow    : 1;         ///< True if preferable to schedule low.
    bool isCloned         : 1;         ///< True if this node has been cloned.
    bool isUnbuffered     : 1;         ///< Uses an unbuffered resource.
    bool hasReservedResource : 1;      ///< Uses a reserved resource.
    unsigned short NumRegDefsLeft = 0; ///< # of reg defs with no scheduled use.
    unsigned short Latency = 0;        ///< Node latency.

  private:
    bool isDepthCurrent   : 1;         ///< True if Depth is current.
    bool isHeightCurrent  : 1;         ///< True if Height is current.
    bool isNode : 1; ///< True if the representative is an SDNode
    bool isInst : 1; ///< True if the representative is a MachineInstr

  public:
    Sched::Preference SchedulingPref : 4; ///< Scheduling preference.
    static_assert(Sched::Preference::Last <= (1 << 4),
                  "not enough bits in bitfield");

    /// Constructs an SUnit for pre-regalloc scheduling to represent an
    /// SDNode and any nodes flagged to it.
    /// \param node Representative SelectionDAG node.
    /// \param nodenum Index of this SUnit in the node vector.
    SUnit(SDNode *node, unsigned nodenum)
        : Node(node), NodeNum(nodenum), isVRegCycle(false), isCall(false),
          isCallOp(false), isTwoAddress(false), isCommutable(false),
          hasPhysRegUses(false), hasPhysRegDefs(false),
          hasPhysRegClobbers(false), isPending(false), isAvailable(false),
          isScheduled(false), isScheduleHigh(false), isScheduleLow(false),
          isCloned(false), isUnbuffered(false), hasReservedResource(false),
          isDepthCurrent(false), isHeightCurrent(false), isNode(true),
          isInst(false), SchedulingPref(Sched::None) {}

    /// Constructs an SUnit for post-regalloc scheduling to represent a
    /// MachineInstr.
    /// \param instr Representative machine instruction.
    /// \param nodenum Index of this SUnit in the node vector.
    SUnit(MachineInstr *instr, unsigned nodenum)
        : Instr(instr), NodeNum(nodenum), isVRegCycle(false), isCall(false),
          isCallOp(false), isTwoAddress(false), isCommutable(false),
          hasPhysRegUses(false), hasPhysRegDefs(false),
          hasPhysRegClobbers(false), isPending(false), isAvailable(false),
          isScheduled(false), isScheduleHigh(false), isScheduleLow(false),
          isCloned(false), isUnbuffered(false), hasReservedResource(false),
          isDepthCurrent(false), isHeightCurrent(false), isNode(false),
          isInst(true), SchedulingPref(Sched::None) {}

    /// Constructs a placeholder SUnit.
    SUnit()
        : Node(nullptr), isVRegCycle(false), isCall(false), isCallOp(false),
          isTwoAddress(false), isCommutable(false), hasPhysRegUses(false),
          hasPhysRegDefs(false), hasPhysRegClobbers(false), isPending(false),
          isAvailable(false), isScheduled(false), isScheduleHigh(false),
          isScheduleLow(false), isCloned(false), isUnbuffered(false),
          hasReservedResource(false), isDepthCurrent(false),
          isHeightCurrent(false), isNode(false), isInst(false),
          SchedulingPref(Sched::None) {}

    /// Boundary nodes are placeholders for the boundary of the
    /// scheduling region.
    ///
    /// BoundaryNodes can have DAG edges, including Data edges, but they do not
    /// correspond to schedulable entities (e.g. instructions) and do not have a
    /// valid ID. Consequently, always check for boundary nodes before accessing
    /// an associative data structure keyed on node ID.
    /// \return True if this SUnit is a boundary placeholder node.
    bool isBoundaryNode() const { return NodeNum == BoundaryID; }

    /// Assigns the representative SDNode for this SUnit. This may be used
    /// during pre-regalloc scheduling.
    /// \param N Representative SelectionDAG node.
    void setNode(SDNode *N) {
      assert(!isInst && "Setting SDNode of SUnit with MachineInstr!");
      Node = N;
      isNode = true;
    }

    /// Returns the representative SDNode for this SUnit. This may be used
    /// during pre-regalloc scheduling.
    /// \return Representative SelectionDAG node, or null if unset.
    SDNode *getNode() const {
      assert(!isInst && (isNode || !Instr) &&
             "Reading SDNode of SUnit without SDNode!");
      return Node;
    }

    /// Returns true if this SUnit refers to a machine instruction as
    /// opposed to an SDNode.
    /// \return True if this SUnit wraps a MachineInstr.
    bool isInstr() const { return isInst && Instr; }

    /// Assigns the instruction for the SUnit. This may be used during
    /// post-regalloc scheduling.
    /// \param MI Representative machine instruction.
    void setInstr(MachineInstr *MI) {
      assert(!isNode && "Setting MachineInstr of SUnit with SDNode!");
      Instr = MI;
      isInst = true;
    }

    /// Returns the representative MachineInstr for this SUnit. This may be used
    /// during post-regalloc scheduling.
    /// \return Representative machine instruction, or null if unset.
    MachineInstr *getInstr() const {
      assert(!isNode && (isInst || !Node) &&
             "Reading MachineInstr of SUnit without MachineInstr!");
      return Instr;
    }

    /// Adds the specified edge as a pred of the current node if not already.
    /// It also adds the current node as a successor of the specified node.
    /// \param D Predecessor edge to add.
    /// \param Required If true, counts toward outstanding strong preds/succs.
    /// \return True if the predecessor edge was newly added.
    LLVM_ABI bool addPred(const SDep &D, bool Required = true);

    /// Adds a barrier edge to SU by calling addPred(), with latency 0
    /// generally or latency 1 for a store followed by a load.
    /// \param SU Predecessor SUnit for the barrier edge.
    /// \return True if the barrier predecessor edge was newly added.
    bool addPredBarrier(SUnit *SU) {
      SDep Dep(SU, SDep::Barrier);
      unsigned TrueMemOrderLatency =
        ((SU->getInstr()->mayStore() && this->getInstr()->mayLoad()) ? 1 : 0);
      Dep.setLatency(TrueMemOrderLatency);
      return addPred(Dep);
    }

    /// Removes the specified edge as a pred of the current node if it exists.
    /// It also removes the current node as a successor of the specified node.
    /// \param D Predecessor edge to remove.
    LLVM_ABI void removePred(const SDep &D);

    /// Returns the depth of this node, which is the length of the maximum path
    /// up to any node which has no predecessors.
    /// \return Depth of this node in the scheduling DAG.
    unsigned getDepth() const {
      if (!isDepthCurrent)
        const_cast<SUnit *>(this)->ComputeDepth();
      return Depth;
    }

    /// Returns the height of this node, which is the length of the
    /// maximum path down to any node which has no successors.
    /// \return Height of this node in the scheduling DAG.
    unsigned getHeight() const {
      if (!isHeightCurrent)
        const_cast<SUnit *>(this)->ComputeHeight();
      return Height;
    }

    /// If NewDepth is greater than this node's depth value, sets it to
    /// be the new depth value. This also recursively marks successor nodes
    /// dirty.
    /// \param NewDepth Candidate depth; applied only if greater than current.
    LLVM_ABI void setDepthToAtLeast(unsigned NewDepth);

    /// If NewHeight is greater than this node's height value, set it to be
    /// the new height value. This also recursively marks predecessor nodes
    /// dirty.
    /// \param NewHeight Candidate height; applied only if greater than current.
    LLVM_ABI void setHeightToAtLeast(unsigned NewHeight);

    /// Sets a flag in this node to indicate that its stored Depth value
    /// will require recomputation the next time getDepth() is called.
    LLVM_ABI void setDepthDirty();

    /// Sets a flag in this node to indicate that its stored Height value
    /// will require recomputation the next time getHeight() is called.
    LLVM_ABI void setHeightDirty();

    /// Tests if node N is a predecessor of this node.
    /// \param N Candidate predecessor node.
    /// \return True if \p N is among this node's predecessors.
    bool isPred(const SUnit *N) const {
      for (const SDep &Pred : Preds)
        if (Pred.getSUnit() == N)
          return true;
      return false;
    }

    /// Tests if node N is a successor of this node.
    /// \param N Candidate successor node.
    /// \return True if \p N is among this node's successors.
    bool isSucc(const SUnit *N) const {
      for (const SDep &Succ : Succs)
        if (Succ.getSUnit() == N)
          return true;
      return false;
    }

    /// Returns true if all predecessors have been scheduled.
    /// \return True if no unscheduled predecessors remain.
    bool isTopReady() const {
      return NumPredsLeft == 0;
    }
    /// Returns true if all successors have been scheduled.
    /// \return True if no unscheduled successors remain.
    bool isBottomReady() const {
      return NumSuccsLeft == 0;
    }

    /// Orders this node's predecessor edges such that the critical path
    /// edge occurs first.
    LLVM_ABI void biasCriticalPath();

    /// Returns true if this SUnit belongs to a cluster group.
    /// \return True if this SUnit has a valid parent cluster id.
    bool isClustered() const { return ParentClusterIdx != InvalidClusterId; }

    /// Dump scheduling attributes of this SUnit for debugging.
    LLVM_ABI void dumpAttributes() const;

  private:
    LLVM_ABI void ComputeDepth();
    LLVM_ABI void ComputeHeight();
  };

  /// Returns true if the specified SDep is equivalent except for latency.
  /// \param Other Dependence to compare against.
  /// \return True if the dependences match ignoring latency.
  inline bool SDep::overlaps(const SDep &Other) const {
    if (Dep != Other.Dep)
      return false;
    switch (Dep.getInt()) {
    case Data:
    case Anti:
    case Output:
      return Contents.Reg == Other.Contents.Reg;
    case Order:
      return Contents.OrdKind == Other.Contents.OrdKind;
    }
    llvm_unreachable("Invalid dependency kind!");
  }

  /// Returns the SUnit to which this edge points.
  /// \return Depending/depended-on SUnit for this edge.
  inline SUnit *SDep::getSUnit() const { return Dep.getPointer(); }

  /// Assigns the SUnit to which this edge points.
  /// \param SU SUnit this edge should point to.
  inline void SDep::setSUnit(SUnit *SU) { Dep.setPointer(SU); }

  /// Returns an enum value representing the kind of the dependence.
  /// \return Dependence kind (Data, Anti, Output, or Order).
  inline SDep::Kind SDep::getKind() const { return Dep.getInt(); }

  //===--------------------------------------------------------------------===//

  /// Priority queue interface for plugging heuristics into the list scheduler.
  ///
  /// This interface is used to plug different priorities computation algorithms
  /// into the list scheduler. It implements the interface of a standard
  /// priority queue, where nodes are inserted in arbitrary order and returned
  /// in priority order. The computation of the priority and the representation
  /// of the queue are totally up to the implementation to decide.
  class LLVM_ABI SchedulingPriorityQueue {
    virtual void anchor();

    unsigned CurCycle = 0;
    bool HasReadyFilter;

  public:
    /// Construct a priority queue, optionally with a ready filter.
    /// \param rf If true, the queue filters nodes that are not yet ready.
    SchedulingPriorityQueue(bool rf = false) :  HasReadyFilter(rf) {}

    /// Virtual destructor.
    virtual ~SchedulingPriorityQueue() = default;

    /// Returns true if this queue schedules bottom-up.
    /// \return True if the queue is used for bottom-up scheduling.
    virtual bool isBottomUp() const = 0;

    /// Initialize the queue from the DAG's SUnit vector.
    /// \param SUnits Scheduling units owned by the ScheduleDAG.
    virtual void initNodes(std::vector<SUnit> &SUnits) = 0;
    /// Notify the queue that \p SU was added to the DAG.
    /// \param SU Newly added scheduling unit.
    virtual void addNode(const SUnit *SU) = 0;
    /// Notify the queue that \p SU's priority-relevant state changed.
    /// \param SU Scheduling unit whose priority may need recomputation.
    virtual void updateNode(const SUnit *SU) = 0;
    /// Release any queue state held between scheduling regions.
    virtual void releaseState() = 0;

    /// Returns true if the queue contains no nodes.
    /// \return True if the queue is empty.
    virtual bool empty() const = 0;

    /// Returns true if this queue filters nodes that are not ready.
    /// \return True if a ready filter is enabled.
    bool hasReadyFilter() const { return HasReadyFilter; }

    /// Returns true if this queue tracks register pressure.
    /// \return True if register pressure is tracked by this queue.
    virtual bool tracksRegPressure() const { return false; }

    /// Returns true if \p SU is considered ready to schedule.
    /// \param SU Node to test for readiness.
    /// \return True if \p SU is ready (default always true without a filter).
    virtual bool isReady(SUnit *SU) const {
      assert(!HasReadyFilter && "The ready filter must override isReady()");
      return true;
    }

    /// Push scheduling unit \p U onto the queue.
    /// \param U Node to insert.
    virtual void push(SUnit *U) = 0;

    /// Push every node in \p Nodes onto the queue.
    /// \param Nodes Nodes to insert.
    void push_all(const std::vector<SUnit *> &Nodes) {
      for (SUnit *SU : Nodes)
        push(SU);
    }

    /// Remove and return the highest-priority ready node.
    /// \return Highest-priority ready SUnit removed from the queue.
    virtual SUnit *pop() = 0;

    /// Remove \p SU from the queue without returning it.
    /// \param SU Node to remove.
    virtual void remove(SUnit *SU) = 0;

    /// Dump queue contents for debugging.
    /// \param DAG Owning schedule DAG, if needed by the dump implementation.
    virtual void dump(ScheduleDAG *DAG) const {}

    /// As each node is scheduled, this method is invoked.  This allows the
    /// priority function to adjust the priority of related unscheduled nodes,
    /// for example.
    /// \param SU Node that was just scheduled.
    virtual void scheduledNode(SUnit *SU) {}

    /// Notify the queue that \p SU was unscheduled.
    /// \param SU Node that was removed from the schedule.
    virtual void unscheduledNode(SUnit *SU) {}

    /// Set the current scheduling cycle used by readiness heuristics.
    /// \param Cycle Current cycle number.
    void setCurCycle(unsigned Cycle) {
      CurCycle = Cycle;
    }

    /// Return the current scheduling cycle.
    /// \return Current cycle number used by readiness heuristics.
    unsigned getCurCycle() const {
      return CurCycle;
    }
  };

  /// Base class for instruction scheduling DAGs.
  class LLVM_ABI ScheduleDAG {
  public:
    const TargetMachine &TM;            ///< Target processor
    const TargetInstrInfo *TII;         ///< Target instruction information
    const TargetRegisterInfo *TRI;      ///< Target processor register info
    MachineFunction &MF;                ///< Machine function
    MachineRegisterInfo &MRI;           ///< Virtual/real register map
    std::vector<SUnit> SUnits;          ///< The scheduling units.
    SUnit EntrySU;                      ///< Special node for the region entry.
    SUnit ExitSU;                       ///< Special node for the region exit.

#ifdef NDEBUG
    static const bool StressSched = false; ///< Always false in release builds.
#else
    bool StressSched; ///< When true, stress-test the scheduler.
#endif

    // This class is designed to be passed by reference only. Copy constructor
    // is declared as deleted here to make the derived classes have deleted
    // implicit-declared copy constructor, which suppresses the warnings from
    // static analyzer when the derived classes own resources that are freed in
    // their destructors, but don't have user-written copy constructors (rule
    // of three).
    /// Copy construction is deleted; ScheduleDAG is passed by reference only.
    /// \param Other Unused; copy construction is deleted.
    ScheduleDAG(const ScheduleDAG &Other) = delete;
    /// Copy assignment is deleted; ScheduleDAG is passed by reference only.
    /// \param Other Unused; copy assignment is deleted.
    ScheduleDAG &operator=(const ScheduleDAG &Other) = delete;

    /// Construct a ScheduleDAG for machine function \p mf.
    /// \param mf Machine function being scheduled.
    explicit ScheduleDAG(MachineFunction &mf);

    /// Virtual destructor.
    virtual ~ScheduleDAG();

    /// Clears the DAG state (between regions).
    void clearDAG();

    /// Returns the MCInstrDesc of this SUnit.
    /// Returns NULL for SDNodes without a machine opcode.
    /// \param SU Scheduling unit whose instruction descriptor is requested.
    /// \return Instruction descriptor, or null if unavailable.
    const MCInstrDesc *getInstrDesc(const SUnit *SU) const {
      if (SU->isInstr()) return &SU->getInstr()->getDesc();
      return getNodeDesc(SU->getNode());
    }

    /// Pops up a GraphViz/gv window with the ScheduleDAG rendered using 'dot'.
    /// \param Name Graph name used for the temporary file / window.
    /// \param Title Window title for the visualization.
    virtual void viewGraph(const Twine &Name, const Twine &Title);
    /// View this ScheduleDAG with a default name and title.
    virtual void viewGraph();

    /// Dump a single SUnit node for debugging.
    /// \param SU Node to dump.
    virtual void dumpNode(const SUnit &SU) const = 0;
    /// Dump the entire ScheduleDAG for debugging.
    virtual void dump() const = 0;
    /// Dump the printable name of \p SU.
    /// \param SU Node whose name is printed.
    void dumpNodeName(const SUnit &SU) const;

    /// Returns a label for an SUnit node in a visualization of the ScheduleDAG.
    /// \param SU Node to label.
    /// \return Graph label string for \p SU.
    virtual std::string getGraphNodeLabel(const SUnit *SU) const = 0;

    /// Returns a label for the region of code covered by the DAG.
    /// \return Label for the DAG's code region.
    virtual std::string getDAGName() const = 0;

    /// Adds custom features for a visualization of the ScheduleDAG.
    /// \param GW Graph writer receiving extra visualization attributes.
    virtual void addCustomGraphFeatures(GraphWriter<ScheduleDAG*> &GW) const {}

#ifndef NDEBUG
    /// Verifies that all SUnits were scheduled and that their state is
    /// consistent. Returns the number of scheduled SUnits.
    /// \param isBottomUp True if the schedule was produced bottom-up.
    /// \return Number of scheduled SUnits verified.
    unsigned VerifyScheduledDAG(bool isBottomUp);
#endif

  protected:
    /// Dump \p SU and all of its scheduling attributes.
    /// \param SU Node to dump in full.
    void dumpNodeAll(const SUnit &SU) const;

  private:
    /// Returns the MCInstrDesc of this SDNode or NULL.
    const MCInstrDesc *getNodeDesc(const SDNode *Node) const;
  };

  /// Forward iterator over the predecessor SUnits of an SUnit.
  class SUnitIterator {
    SUnit *Node;
    unsigned Operand;

    SUnitIterator(SUnit *N, unsigned Op) : Node(N), Operand(Op) {}

  public:
    /// Iterator category tag for this forward iterator.
    using iterator_category = std::forward_iterator_tag;
    /// Type of the value obtained by dereferencing this iterator.
    using value_type = SUnit;
    /// Type used to represent distances between iterators.
    using difference_type = std::ptrdiff_t;
    /// Pointer type for the iterated SUnit.
    using pointer = value_type *;
    /// Reference type for the iterated SUnit.
    using reference = value_type &;

    /// Returns true if this iterator equals \p x.
    /// \param x Iterator to compare against.
    /// \return True if both iterators refer to the same operand index.
    bool operator==(const SUnitIterator& x) const {
      return Operand == x.Operand;
    }
    /// Returns true if this iterator differs from \p x.
    /// \param x Iterator to compare against.
    /// \return True if the iterators refer to different operand indices.
    bool operator!=(const SUnitIterator& x) const { return !operator==(x); }

    /// Dereference to the predecessor SUnit at this position.
    /// \return Pointer to the predecessor SUnit.
    pointer operator*() const {
      return Node->Preds[Operand].getSUnit();
    }
    /// Member access for the predecessor SUnit at this position.
    /// \return Pointer to the predecessor SUnit.
    pointer operator->() const { return operator*(); }

    /// Advance to the next predecessor (preincrement).
    /// \return Reference to this iterator after advancing.
    SUnitIterator& operator++() {                // Preincrement
      ++Operand;
      return *this;
    }
    /// Advance to the next predecessor (postincrement).
    /// \param Unused Unused postfix-discriminator parameter.
    /// \return Copy of the iterator before advancing.
    SUnitIterator operator++(int Unused) { // Postincrement
      SUnitIterator tmp = *this; ++*this; return tmp;
    }

    /// Return an iterator to the first predecessor of \p N.
    /// \param N SUnit whose predecessors are iterated.
    /// \return Iterator to the first predecessor of \p N.
    static SUnitIterator begin(SUnit *N) { return SUnitIterator(N, 0); }
    /// Return an iterator past the last predecessor of \p N.
    /// \param N SUnit whose predecessors are iterated.
    /// \return Iterator past the last predecessor of \p N.
    static SUnitIterator end  (SUnit *N) {
      return SUnitIterator(N, (unsigned)N->Preds.size());
    }

    /// Return the predecessor edge index within the SUnit.
    /// \return Current predecessor operand index.
    unsigned getOperand() const { return Operand; }
    /// Return the SUnit whose predecessors are being iterated.
    /// \return SUnit whose predecessor list is being walked.
    const SUnit *getNode() const { return Node; }

    /// Tests if this is not an SDep::Data dependence.
    /// \return True if the current edge is a control dependence.
    bool isCtrlDep() const {
      return getSDep().isCtrl();
    }
    /// Returns true if the current edge is an artificial Order dependence.
    /// \return True if the current edge is artificial.
    bool isArtificialDep() const {
      return getSDep().isArtificial();
    }
    /// Return the SDep edge currently pointed to.
    /// \return Reference to the current predecessor dependence edge.
    const SDep &getSDep() const {
      return Node->Preds[Operand];
    }
  };

  /// GraphTraits specialization so algorithms can walk SUnit predecessor edges.
  template <> struct GraphTraits<SUnit*> {
    /// Handle type for an SUnit graph node.
    typedef SUnit *NodeRef;
    /// Iterator type over child (predecessor) nodes.
    typedef SUnitIterator ChildIteratorType;
    /// Return \p N as the entry node of a single-node graph walk.
    /// \param N Root SUnit.
    /// \return The entry SUnit \p N.
    static NodeRef getEntryNode(SUnit *N) { return N; }
    /// Return an iterator to the first child (predecessor) of \p N.
    /// \param N Node whose children are iterated.
    /// \return Iterator to the first predecessor of \p N.
    static ChildIteratorType child_begin(NodeRef N) {
      return SUnitIterator::begin(N);
    }
    /// Return an iterator past the last child (predecessor) of \p N.
    /// \param N Node whose children are iterated.
    /// \return Iterator past the last predecessor of \p N.
    static ChildIteratorType child_end(NodeRef N) {
      return SUnitIterator::end(N);
    }
  };

  /// GraphTraits specialization for iterating all SUnits in a ScheduleDAG.
  template <> struct GraphTraits<ScheduleDAG*> : public GraphTraits<SUnit*> {
    /// Iterator over all SUnits in the schedule DAG.
    typedef pointer_iterator<std::vector<SUnit>::iterator> nodes_iterator;
    /// Return an iterator to the first SUnit in \p G.
    /// \param G ScheduleDAG whose nodes are iterated.
    /// \return Iterator to the first SUnit in \p G.
    static nodes_iterator nodes_begin(ScheduleDAG *G) {
      return nodes_iterator(G->SUnits.begin());
    }
    /// Return an iterator past the last SUnit in \p G.
    /// \param G ScheduleDAG whose nodes are iterated.
    /// \return Iterator past the last SUnit in \p G.
    static nodes_iterator nodes_end(ScheduleDAG *G) {
      return nodes_iterator(G->SUnits.end());
    }
  };

  /// This class can compute a topological ordering for SUnits and provides
  /// methods for dynamically updating the ordering as new edges are added.
  ///
  /// This allows a very fast implementation of IsReachable, for example.
  class ScheduleDAGTopologicalSort {
    /// A reference to the ScheduleDAG's SUnits.
    std::vector<SUnit> &SUnits;
    SUnit *ExitSU;

    // Have any new nodes been added?
    bool Dirty = false;

    // Outstanding added edges, that have not been applied to the ordering.
    SmallVector<std::pair<SUnit *, SUnit *>, 16> Updates;

    /// Maps topological index to the node number.
    std::vector<int> Index2Node;
    /// Maps the node number to its topological index.
    std::vector<int> Node2Index;
    /// a set of nodes visited during a DFS traversal.
    BitVector Visited;
    /// Cache of reachability queries. {A, B} -> true if B is reachable from A.
    /// The keys are SUnit NodeNums.
    DenseMap<std::pair<int, int>, bool> Reachable;

    /// Makes a DFS traversal and mark all nodes affected by the edge insertion.
    /// These nodes will later get new topological indexes by means of the Shift
    /// method.
    void DFS(const SUnit *SU, int UpperBound, bool& HasLoop);

    /// Reassigns topological indexes for the nodes in the DAG to
    /// preserve the topological ordering.
    void Shift(BitVector& Visited, int LowerBound, int UpperBound);

    /// Assigns the topological index to the node n.
    void Allocate(int n, int index);

    /// Fix the ordering, by either recomputing from scratch or by applying
    /// any outstanding updates. Uses a heuristic to estimate what will be
    /// cheaper.
    void FixOrder();

  public:
    /// Construct a topological sort helper for \p SUnits with exit node \p ExitSU.
    /// \param SUnits Scheduling units owned by the ScheduleDAG.
    /// \param ExitSU Special exit SUnit for the scheduling region.
    LLVM_ABI ScheduleDAGTopologicalSort(std::vector<SUnit> &SUnits,
                                        SUnit *ExitSU);

    /// Add a SUnit without predecessors to the end of the topological order. It
    /// also must be the first new node added to the DAG.
    /// \param SU New SUnit with no predecessors.
    LLVM_ABI void AddSUnitWithoutPredecessors(const SUnit *SU);

    /// Creates the initial topological ordering from the DAG to be scheduled.
    LLVM_ABI void InitDAGTopologicalSorting();

    /// Returns SUs in both the successor and predecessor subtrees.
    ///
    /// Returns an array of SUs that are both in the successor subtree of
    /// StartSU and in the predecessor subtree of TargetSU. StartSU and TargetSU
    /// are not in the array. Success is false if TargetSU is not in the
    /// successor subtree of StartSU, else it is true.
    /// \param StartSU Root of the successor subtree.
    /// \param TargetSU Root of the predecessor subtree.
    /// \param Success Set to false if TargetSU is unreachable from StartSU.
    /// \return Node numbers in both subtrees, excluding StartSU and TargetSU.
    LLVM_ABI std::vector<int> GetSubGraph(const SUnit &StartSU,
                                          const SUnit &TargetSU, bool &Success);

    /// Checks if \p SU is reachable from \p TargetSU.
    /// \param SU Node to test for reachability.
    /// \param TargetSU Potential ancestor node.
    /// \return True if \p SU is reachable from \p TargetSU.
    LLVM_ABI bool IsReachable(const SUnit *SU, const SUnit *TargetSU);

    /// Returns true if addPred(TargetSU, SU) creates a cycle.
    /// \param TargetSU Proposed successor of \p SU.
    /// \param SU Proposed predecessor of \p TargetSU.
    /// \return True if adding the edge would create a cycle.
    LLVM_ABI bool WillCreateCycle(SUnit *TargetSU, SUnit *SU);

    /// Updates the topological ordering to accommodate an edge to be
    /// added from SUnit \p X to SUnit \p Y.
    /// \param Y Successor endpoint of the new edge.
    /// \param X Predecessor endpoint of the new edge.
    LLVM_ABI void AddPred(SUnit *Y, SUnit *X);

    /// Queues an update to the topological ordering to accommodate an edge to
    /// be added from SUnit \p X to SUnit \p Y.
    /// \param Y Successor endpoint of the queued edge.
    /// \param X Predecessor endpoint of the queued edge.
    LLVM_ABI void AddPredQueued(SUnit *Y, SUnit *X);

    /// Updates the topological ordering to accommodate an edge to be
    /// removed from the specified node \p N from the predecessors of the
    /// current node \p M.
    /// \param M Successor endpoint of the removed edge.
    /// \param N Predecessor endpoint of the removed edge.
    LLVM_ABI void RemovePred(SUnit *M, SUnit *N);

    /// Mark the ordering as temporarily broken, after a new node has been
    /// added.
    void MarkDirty() { Dirty = true; }

    /// Mutable iterator over nodes in topological order.
    typedef std::vector<int>::iterator iterator;
    /// Const iterator over nodes in topological order.
    typedef std::vector<int>::const_iterator const_iterator;
    /// Return an iterator to the first node in topological order.
    /// \return Iterator to the first node in topological order.
    iterator begin() { return Index2Node.begin(); }
    /// Return a const iterator to the first node in topological order.
    /// \return Const iterator to the first node in topological order.
    const_iterator begin() const { return Index2Node.begin(); }
    /// Return an iterator past the last node in topological order.
    /// \return Iterator past the last node in topological order.
    iterator end() { return Index2Node.end(); }
    /// Return a const iterator past the last node in topological order.
    /// \return Const iterator past the last node in topological order.
    const_iterator end() const { return Index2Node.end(); }

    /// Mutable reverse iterator over nodes in topological order.
    typedef std::vector<int>::reverse_iterator reverse_iterator;
    /// Const reverse iterator over nodes in topological order.
    typedef std::vector<int>::const_reverse_iterator const_reverse_iterator;
    /// Return a reverse iterator to the last node in topological order.
    /// \return Reverse iterator to the last node in topological order.
    reverse_iterator rbegin() { return Index2Node.rbegin(); }
    /// Return a const reverse iterator to the last node in topological order.
    /// \return Const reverse iterator to the last node in topological order.
    const_reverse_iterator rbegin() const { return Index2Node.rbegin(); }
    /// Return a reverse iterator past the first node in topological order.
    /// \return Reverse iterator past the first node in topological order.
    reverse_iterator rend() { return Index2Node.rend(); }
    /// Return a const reverse iterator past the first node in topological order.
    /// \return Const reverse iterator past the first node in topological order.
    const_reverse_iterator rend() const { return Index2Node.rend(); }
  };

} // end namespace llvm

#endif // LLVM_CODEGEN_SCHEDULEDAG_H
