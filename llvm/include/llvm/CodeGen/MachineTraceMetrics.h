//===- lib/CodeGen/MachineTraceMetrics.h - Super-scalar metrics -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interface for the MachineTraceMetrics analysis pass
// that estimates CPU resource usage and critical data dependency paths through
// preferred traces. This is useful for super-scalar CPUs where execution speed
// can be limited both by data dependencies and by limited execution resources.
//
// Out-of-order CPUs will often be executing instructions from multiple basic
// blocks at the same time. This makes it difficult to estimate the resource
// usage accurately in a single basic block. Resources can be estimated better
// by looking at a trace through the current basic block.
//
// For every block, the MachineTraceMetrics pass will pick a preferred trace
// that passes through the block. The trace is chosen based on loop structure,
// branch probabilities, and resource usage. The intention is to pick likely
// traces that would be the most affected by code transformations.
//
// It is expensive to compute a full arbitrary trace for every block, so to
// save some computations, traces are chosen to be convergent. This means that
// if the traces through basic blocks A and B ever cross when moving away from
// A and B, they never diverge again. This applies in both directions - If the
// traces meet above A and B, they won't diverge when going further back.
//
// Traces tend to align with loops. The trace through a block in an inner loop
// will begin at the loop entry block and end at a back edge. If there are
// nested loops, the trace may begin and end at those instead.
//
// For each trace, we compute the critical path length, which is the number of
// cycles required to execute the trace when execution is limited by data
// dependencies only. We also compute the resource height, which is the number
// of cycles required to execute all instructions in the trace when ignoring
// data dependencies.
//
// Every instruction in the current block has a slack - the number of cycles
// execution of the instruction can be delayed without extending the critical
// path.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINETRACEMETRICS_H
#define LLVM_CODEGEN_MACHINETRACEMETRICS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/TargetSchedule.h"

namespace llvm {

class AnalysisUsage;
class MachineFunction;
class MachineInstr;
class MachineLoop;
class MachineLoopInfo;
class MachineRegisterInfo;
struct MCSchedClassDesc;
class raw_ostream;
class TargetInstrInfo;
class TargetRegisterInfo;

/// Live physical register unit with its defining or reading operand.
///
/// Keep track of physreg data dependencies by recording each live register
/// unit. Associate each regunit with an instruction operand. Depending on the
/// direction instructions are scanned, it could be the operand that defined the
/// regunit, or the highest operand to read the regunit.
struct LiveRegUnit {
  /// Physical register unit being tracked.
  MCRegUnit RegUnit;
  /// Cycle at which \p RegUnit is defined or last read.
  unsigned Cycle = 0;
  /// Instruction associated with \p Op.
  const MachineInstr *MI = nullptr;
  /// Operand index of \p MI that defines or reads \p RegUnit.
  unsigned Op = 0;

  /// Return the sparse-set index for this regunit.
  /// \return Sparse-set key corresponding to \p RegUnit.
  unsigned getSparseSetIndex() const { return static_cast<unsigned>(RegUnit); }

  /// Construct a live regunit entry for \p RU.
  /// \param RU Register unit to track.
  explicit LiveRegUnit(MCRegUnit RU) : RegUnit(RU) {}
};

/// Sparse set of live register units keyed by regunit index.
using LiveRegUnitSet = SparseSet<LiveRegUnit, MCRegUnit, MCRegUnitToIndex>;

/// Strategies for selecting traces.
enum class MachineTraceStrategy {
  /// Select the trace through a block that has the fewest instructions.
  TS_MinInstrCount,
  /// Select the trace that contains only the current basic block. For instance,
  /// this strategy can be used by MachineCombiner to make better decisions when
  /// we estimate critical path for in-order cores.
  TS_Local,
  TS_NumStrategies
};

/// Analysis estimating resource use and critical paths through preferred traces.
class MachineTraceMetrics {
  const MachineFunction *MF = nullptr;
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  const MachineRegisterInfo *MRI = nullptr;
  const MachineLoopInfo *Loops = nullptr;
  TargetSchedModel SchedModel;

public:
  friend class MachineTraceMetricsWrapperPass;
  /// Strategy-specific collection of preferred traces through a function.
  friend class Ensemble;
  friend class Trace;

  class Ensemble;

  /// Construct an uninitialized metrics object for the legacy pass manager.
  MachineTraceMetrics() = default;

  /// Construct metrics and initialize them for \p MF.
  /// \param MF Machine function to analyze.
  /// \param LI Loop information for \p MF.
  explicit MachineTraceMetrics(MachineFunction &MF, const MachineLoopInfo &LI) {
    init(MF, LI);
  }

  /// Move-construct metrics from another instance.
  /// \param Other Metrics instance to move from.
  MachineTraceMetrics(MachineTraceMetrics &&Other) = default;

  /// Destroy the metrics object and release owned ensembles.
  LLVM_ABI ~MachineTraceMetrics();

  /// Initialize metrics for \p Func using loop info \p LI.
  /// \param Func Machine function to analyze.
  /// \param LI Loop information for \p Func.
  LLVM_ABI void init(MachineFunction &Func, const MachineLoopInfo &LI);
  /// Release all cached metrics and ensembles.
  LLVM_ABI void clear();

  /// Per-basic block information that doesn't depend on the trace through the
  /// block.
  struct FixedBlockInfo {
    /// The number of non-trivial instructions in the block.
    /// Doesn't count PHI and COPY instructions that are likely to be removed.
    unsigned InstrCount = ~0u;

    /// True when the block contains calls.
    bool HasCalls = false;

    /// Construct an invalid fixed-block info entry.
    FixedBlockInfo() = default;

    /// Returns true when resource information for this block has been computed.
    /// \return True if fixed resource info for this block is valid.
    bool hasResources() const { return InstrCount != ~0u; }

    /// Invalidate resource information.
    void invalidate() { InstrCount = ~0u; }
  };

  /// Get the fixed resource information about MBB. Compute it on demand.
  /// \param MBB Basic block whose fixed resources are requested.
  /// \return Fixed per-block resource info for \p MBB.
  LLVM_ABI const FixedBlockInfo *getResources(const MachineBasicBlock *MBB);

  /// Get the scaled cycles used per processor resource in a basic block.
  ///
  /// This is an array with SchedModel.getNumProcResourceKinds() entries.
  /// The getResources() function above must have been called first.
  ///
  /// These numbers have already been scaled by SchedModel.getResourceFactor().
  /// \param MBBNum Block number whose scaled resource cycles are requested.
  /// \return Scaled release-at-cycle counts for each processor resource.
  LLVM_ABI ArrayRef<unsigned> getProcReleaseAtCycles(unsigned MBBNum) const;

  /// A virtual register or regunit required by a basic block or its trace
  /// successors.
  struct LiveInReg {
    /// The virtual register required, or a register unit.
    VirtRegOrUnit VRegOrUnit;

    /// For virtual registers: Minimum height of the defining instruction.
    /// For regunits: Height of the highest user in the trace.
    unsigned Height;

    /// Construct a live-in register entry.
    /// \param VRegOrUnit Virtual register or register unit that is live-in.
    /// \param Height Associated height of the defining instruction or user.
    explicit LiveInReg(VirtRegOrUnit VRegOrUnit, unsigned Height = 0)
        : VRegOrUnit(VRegOrUnit), Height(Height) {}
  };

  /// Per-block information for one trace through that block.
  ///
  /// Convergent traces means that only one of these is required per block in a
  /// trace ensemble.
  struct TraceBlockInfo {
    /// Trace predecessor, or NULL for the first block in the trace.
    /// Valid when hasValidDepth().
    const MachineBasicBlock *Pred = nullptr;

    /// Trace successor, or NULL for the last block in the trace.
    /// Valid when hasValidHeight().
    const MachineBasicBlock *Succ = nullptr;

    /// The block number of the head of the trace. (When hasValidDepth()).
    unsigned Head;

    /// The block number of the tail of the trace. (When hasValidHeight()).
    unsigned Tail;

    /// Accumulated number of instructions in the trace above this block.
    /// Does not include instructions in this block.
    unsigned InstrDepth = ~0u;

    /// Accumulated number of instructions in the trace below this block.
    /// Includes instructions in this block.
    unsigned InstrHeight = ~0u;

    /// Construct an invalid trace-block info entry.
    TraceBlockInfo() = default;

    /// Returns true if the depth resources have been computed from the trace
    /// above this block.
    /// \return True when depth resources for this block are valid.
    bool hasValidDepth() const { return InstrDepth != ~0u; }

    /// Returns true if the height resources have been computed from the trace
    /// below this block.
    /// \return True when height resources for this block are valid.
    bool hasValidHeight() const { return InstrHeight != ~0u; }

    /// Invalidate depth resources when some block above this one has changed.
    void invalidateDepth() { InstrDepth = ~0u; HasValidInstrDepths = false; }

    /// Invalidate height resources when a block below this one has changed.
    void invalidateHeight() { InstrHeight = ~0u; HasValidInstrHeights = false; }

    /// Return true if this dominator has useful instruction depths for TBI.
    ///
    /// Assuming that this is a dominator of TBI, determine if it contains
    /// useful instruction depths. A dominating block can be above the current
    /// trace head, and any dependencies from such a far away dominator are not
    /// expected to affect the critical path.
    ///
    /// Also returns true when TBI == this.
    /// \param TBI Trace-block info of the dominated block.
    /// \return True if this block's instruction depths are useful for \p TBI.
    bool isUsefulDominator(const TraceBlockInfo &TBI) const {
      // The trace for TBI may not even be calculated yet.
      if (!hasValidDepth() || !TBI.hasValidDepth())
        return false;
      // Instruction depths are only comparable if the traces share a head.
      if (Head != TBI.Head)
        return false;
      // It is almost always the case that TBI belongs to the same trace as
      // this block, but rare convoluted cases involving irreducible control
      // flow, a dominator may share a trace head without actually being on the
      // same trace as TBI. This is not a big problem as long as it doesn't
      // increase the instruction depth.
      return HasValidInstrDepths && InstrDepth <= TBI.InstrDepth;
    }

    // Data-dependency-related information. Per-instruction depth and height
    // are computed from data dependencies in the current trace, using
    // itinerary data.

    /// Instruction depths have been computed. This implies hasValidDepth().
    bool HasValidInstrDepths = false;

    /// Instruction heights have been computed. This implies hasValidHeight().
    bool HasValidInstrHeights = false;

    /// Length of the longest data-dependency chain through the trace.
    ///
    /// Critical path length. This is the number of cycles in the longest data
    /// dependency chain through the trace. This is only valid when both
    /// HasValidInstrDepths and HasValidInstrHeights are set.
    unsigned CriticalPath;

    /// Registers defined above this block and used by it or below it.
    ///
    /// Live-in registers. These registers are defined above the current block
    /// and used by this block or a block below it. This does not include PHI
    /// uses in the current block, but it does include PHI uses in deeper
    /// blocks.
    SmallVector<LiveInReg, 4> LiveIns;

    /// Print this trace-block info to \p OS.
    /// \param OS Output stream receiving the printed representation.
    LLVM_ABI void print(raw_ostream &OS) const;
    /// Dump this trace-block info to the debug stream.
    void dump() const { print(dbgs()); }
  };

  /// InstrCycles represents the cycle height and depth of an instruction in a
  /// trace.
  struct InstrCycles {
    /// Earliest issue cycle from data deps and latencies in the trace.
    ///
    /// Earliest issue cycle as determined by data dependencies and instruction
    /// latencies from the beginning of the trace. Data dependencies from
    /// before the trace are not included.
    unsigned Depth;

    /// Minimum number of cycles from this instruction is issued to the of the
    /// trace, as determined by data dependencies and instruction latencies.
    unsigned Height;
  };

  /// Handle to a preferred sequence of blocks through a center block.
  ///
  /// A trace represents a plausible sequence of executed basic blocks that
  /// passes through the current basic block one. The Trace class serves as a
  /// handle to internal cached data structures.
  class Trace {
    Ensemble &TE;
    TraceBlockInfo &TBI;

    unsigned getBlockNum() const { return &TBI - &TE.BlockInfo[0]; }

  public:
    /// Construct a trace handle for \p tbi in ensemble \p te.
    /// \param te Ensemble that owns the cached trace data.
    /// \param tbi Per-block trace info for the center block.
    explicit Trace(Ensemble &te, TraceBlockInfo &tbi) : TE(te), TBI(tbi) {}

    /// Print this trace to \p OS.
    /// \param OS Output stream receiving the printed representation.
    LLVM_ABI void print(raw_ostream &OS) const;
    /// Dump this trace to the debug stream.
    void dump() const { print(dbgs()); }

    /// Compute the total number of instructions in the trace.
    /// \return Instruction count from the trace head through the tail.
    unsigned getInstrCount() const {
      return TBI.InstrDepth + TBI.InstrHeight;
    }

    /// Return the resource depth of the top or bottom of the center block.
    ///
    /// This is the number of cycles required to execute all instructions from
    /// the trace head to the trace center block. The resource depth only
    /// considers execution resources, it ignores data dependencies. When Bottom
    /// is set, instructions in the trace center block are included.
    /// \param Bottom If true, include instructions in the trace center block.
    /// \return Resource-limited depth in cycles to the center block.
    LLVM_ABI unsigned getResourceDepth(bool Bottom) const;

    /// Return the resource length of the trace in cycles.
    ///
    /// This is the number of cycles required to execute the instructions in the
    /// trace if they were all independent, exposing the maximum
    /// instruction-level parallelism.
    ///
    /// Any blocks in Extrablocks are included as if they were part of the
    /// trace. Likewise, extra resources required by the specified scheduling
    /// classes are included. For the caller to account for extra machine
    /// instructions, it must first resolve each instruction's scheduling class.
    /// \param Extrablocks Extra blocks treated as part of the trace.
    /// \param ExtraInstrs Extra scheduling classes whose resources are added.
    /// \param RemoveInstrs Scheduling classes whose resources are subtracted.
    /// \return Resource-limited length of the trace in cycles.
    LLVM_ABI unsigned getResourceLength(
        ArrayRef<const MachineBasicBlock *> Extrablocks = {},
        ArrayRef<const MCSchedClassDesc *> ExtraInstrs = {},
        ArrayRef<const MCSchedClassDesc *> RemoveInstrs = {}) const;

    /// Return the length of the (data dependency) critical path through the
    /// trace.
    /// \return Number of cycles on the longest data-dependency chain.
    unsigned getCriticalPath() const { return TBI.CriticalPath; }

    /// Return the depth and height of instruction \p MI in this trace.
    ///
    /// The depth is only valid for instructions in or above the trace center
    /// block. The height is only valid for instructions in or below the trace
    /// center block.
    /// \param MI Instruction whose cycles are requested.
    /// \return Depth and height cycles of \p MI in this trace.
    InstrCycles getInstrCycles(const MachineInstr &MI) const {
      return TE.Cycles.lookup(&MI);
    }

    /// Return how many cycles \p MI can be delayed before the critical path grows.
    ///
    /// Return the slack of MI. This is the number of cycles MI can be delayed
    /// before the critical path becomes longer. MI must be an instruction in
    /// the trace center block.
    /// \param MI Center-block instruction whose slack is requested.
    /// \return Cycles \p MI can be delayed without lengthening the critical path.
    LLVM_ABI unsigned getInstrSlack(const MachineInstr &MI) const;

    /// Return the Depth of a PHI instruction in a trace center block successor.
    /// The PHI does not have to be part of the trace.
    /// \param PHI PHI instruction whose depth is requested.
    /// \return Depth cycle of \p PHI relative to the trace.
    LLVM_ABI unsigned getPHIDepth(const MachineInstr &PHI) const;

    /// Return true if \p DefMI is on the same preferred trace as \p UseMI.
    ///
    /// A dependence is useful if the basic block of the defining instruction
    /// is part of the trace of the user instruction. It is assumed that DefMI
    /// dominates UseMI (see also isUsefulDominator).
    /// \param DefMI Defining instruction of the dependence.
    /// \param UseMI User instruction of the dependence.
    /// \return True if \p DefMI's block is on the preferred trace of \p UseMI.
    LLVM_ABI bool isDepInTrace(const MachineInstr &DefMI,
                               const MachineInstr &UseMI) const;
  };

  /// Collection of traces selected with one shared strategy.
  ///
  /// A trace ensemble is a collection of traces selected using the same
  /// strategy, for example 'minimum resource height'. There is one trace for
  /// every block in the function.
  class LLVM_ABI Ensemble {
    friend class Trace;

    SmallVector<TraceBlockInfo, 4> BlockInfo;
    DenseMap<const MachineInstr*, InstrCycles> Cycles;
    SmallVector<unsigned, 0> ProcResourceDepths;
    SmallVector<unsigned, 0> ProcResourceHeights;

    void computeTrace(const MachineBasicBlock*);
    void computeDepthResources(const MachineBasicBlock*);
    void computeHeightResources(const MachineBasicBlock*);
    unsigned computeCrossBlockCriticalPath(const TraceBlockInfo&);
    void computeInstrDepths(const MachineBasicBlock*);
    void computeInstrHeights(const MachineBasicBlock*);
    void addLiveIns(const MachineInstr *DefMI, unsigned DefOp,
                    ArrayRef<const MachineBasicBlock*> Trace);

  protected:
    /// Owning MachineTraceMetrics analysis instance.
    MachineTraceMetrics &MTM;

    /// Construct an ensemble bound to analysis \p MTM.
    /// \param MTM Analysis that owns this ensemble.
    explicit Ensemble(MachineTraceMetrics *MTM);

    /// Pick the preferred trace predecessor of \p MBB.
    /// \param MBB Block whose preferred predecessor is requested.
    /// \return Preferred predecessor of \p MBB, or nullptr if none.
    virtual const MachineBasicBlock *
    pickTracePred(const MachineBasicBlock *MBB) = 0;
    /// Pick the preferred trace successor of \p MBB.
    /// \param MBB Block whose preferred successor is requested.
    /// \return Preferred successor of \p MBB, or nullptr if none.
    virtual const MachineBasicBlock *
    pickTraceSucc(const MachineBasicBlock *MBB) = 0;
    /// Return the loop containing \p MBB, if any.
    /// \param MBB Basic block whose enclosing loop is requested.
    /// \return Innermost loop containing \p MBB, or nullptr if none.
    const MachineLoop *getLoopFor(const MachineBasicBlock *MBB) const;
    /// Return depth resources for \p MBB, if computed.
    /// \param MBB Basic block whose depth resources are requested.
    /// \return Trace-block depth info for \p MBB, or nullptr if not computed.
    const TraceBlockInfo *getDepthResources(const MachineBasicBlock *MBB) const;
    /// Return height resources for \p MBB, if computed.
    /// \param MBB Basic block whose height resources are requested.
    /// \return Trace-block height info for \p MBB, or nullptr if not computed.
    const TraceBlockInfo *
    getHeightResources(const MachineBasicBlock *MBB) const;
    /// Return scaled processor resource depths for block \p MBBNum.
    /// \param MBBNum Block number whose resource depths are requested.
    /// \return Scaled processor resource depth cycles for \p MBBNum.
    ArrayRef<unsigned> getProcResourceDepths(unsigned MBBNum) const;
    /// Return scaled processor resource heights for block \p MBBNum.
    /// \param MBBNum Block number whose resource heights are requested.
    /// \return Scaled processor resource height cycles for \p MBBNum.
    ArrayRef<unsigned> getProcResourceHeights(unsigned MBBNum) const;

  public:
    /// Destroy the ensemble and release cached trace data.
    virtual ~Ensemble();

    /// Return a printable name for this ensemble's selection strategy.
    /// \return Null-terminated name of the selection strategy.
    virtual const char *getName() const = 0;
    /// Print this ensemble to \p OS.
    /// \param OS Output stream receiving the printed representation.
    void print(raw_ostream &OS) const;
    /// Dump this ensemble to the debug stream.
    void dump() const { print(dbgs()); }
    /// Invalidate cached trace information involving \p MBB.
    /// \param MBB Basic block whose related traces must be recomputed.
    void invalidate(const MachineBasicBlock *MBB);
    /// Verify internal ensemble invariants.
    void verify() const;

    /// Get the trace that passes through MBB.
    /// The trace is computed on demand.
    /// \param MBB Center block of the requested trace.
    /// \return Trace handle centered on \p MBB.
    Trace getTrace(const MachineBasicBlock *MBB);

    /// Updates the depth of an machine instruction, given RegUnits.
    /// \param TBI Trace-block info for the instruction's block.
    /// \param MI Instruction whose depth is updated.
    /// \param RegUnits Live register units used to compute data-dep depths.
    void updateDepth(TraceBlockInfo &TBI, const MachineInstr &MI,
                     LiveRegUnitSet &RegUnits);
    /// Update the depth of \p MI in \p MBB using \p RegUnits.
    /// \param MBB Basic block containing \p MI.
    /// \param MI Instruction whose depth is updated.
    /// \param RegUnits Live register units used to compute data-dep depths.
    void updateDepth(const MachineBasicBlock *MBB, const MachineInstr &MI,
                     LiveRegUnitSet &RegUnits);

    /// Updates the depth of the instructions from Start to End.
    /// \param Start First instruction in the half-open range to update.
    /// \param End End of the half-open instruction range to update.
    /// \param RegUnits Live register units used to compute data-dep depths.
    void updateDepths(MachineBasicBlock::iterator Start,
                      MachineBasicBlock::iterator End,
                      LiveRegUnitSet &RegUnits);
  };

  /// Get the trace ensemble for the given selection strategy.
  ///
  /// The returned Ensemble object is owned by the MachineTraceMetrics analysis,
  /// and valid for the lifetime of the analysis pass.
  /// \param Strategy Trace selection strategy that identifies the ensemble.
  /// \return Ensemble for \p Strategy, owned by this analysis.
  LLVM_ABI Ensemble *getEnsemble(MachineTraceStrategy Strategy);

  /// Invalidate cached information about MBB. This must be called *before* MBB
  /// is erased, or the CFG is otherwise changed.
  ///
  /// This invalidates per-block information about resource usage for MBB only,
  /// and it invalidates per-trace information for any trace that passes
  /// through MBB.
  ///
  /// Call Ensemble::getTrace() again to update any trace handles.
  /// \param MBB Basic block whose cached metrics are invalidated.
  LLVM_ABI void invalidate(const MachineBasicBlock *MBB);

  /// Handle invalidation explicitly.
  /// \param MF Machine function whose analyses may be invalidated.
  /// \param PA Set of analyses preserved by the transform that triggered this.
  /// \param Inv Invalidator used to invalidate dependent analyses.
  /// \return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(MachineFunction &MF, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);

  /// Verify internal analysis invariants.
  LLVM_ABI void verifyAnalysis() const;

private:
  // One entry per basic block, indexed by block number.
  SmallVector<FixedBlockInfo, 4> BlockInfo;

  // Cycles consumed on each processor resource per block.
  // The number of processor resource kinds is constant for a given subtarget,
  // but it is not known at compile time. The number of cycles consumed by
  // block B on processor resource R is at ProcReleaseAtCycles[B*Kinds + R]
  // where Kinds = SchedModel.getNumProcResourceKinds().
  SmallVector<unsigned, 0> ProcReleaseAtCycles;

  // One ensemble per strategy.
  std::unique_ptr<Ensemble>
      Ensembles[static_cast<size_t>(MachineTraceStrategy::TS_NumStrategies)];

  // Convert scaled resource usage to a cycle count that can be compared with
  // latencies.
  unsigned getCycles(unsigned Scaled) {
    unsigned Factor = SchedModel.getLatencyFactor();
    return (Scaled + Factor - 1) / Factor;
  }
};

/// Print Trace \p Tr to stream \p OS.
/// \param OS Output stream receiving the printed representation.
/// \param Tr Trace to print.
/// \return The output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS,
                               const MachineTraceMetrics::Trace &Tr) {
  Tr.print(OS);
  return OS;
}

/// Print Ensemble \p En to stream \p OS.
/// \param OS Output stream receiving the printed representation.
/// \param En Ensemble to print.
/// \return The output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS,
                               const MachineTraceMetrics::Ensemble &En) {
  En.print(OS);
  return OS;
}

/// Analysis pass computing MachineTraceMetrics for a machine function.
class MachineTraceMetricsAnalysis
    : public AnalysisInfoMixin<MachineTraceMetricsAnalysis> {
  friend AnalysisInfoMixin<MachineTraceMetricsAnalysis>;
  static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = MachineTraceMetrics;
  /// Run the MachineTraceMetrics analysis on \p MF.
  /// \param MF Machine function to analyze.
  /// \param MFAM Analysis manager providing required machine-function analyses.
  /// \return The computed MachineTraceMetrics for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// Verifier pass for \c MachineTraceMetrics.
struct MachineTraceMetricsVerifierPass
    : RequiredPassInfoMixin<MachineTraceMetricsVerifierPass> {
  /// Verify MachineTraceMetrics for \p MF and return preserved analyses.
  /// \param MF Machine function whose metrics are verified.
  /// \param MFAM Analysis manager providing MachineTraceMetrics.
  /// \return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// Legacy pass manager wrapper around MachineTraceMetrics.
class LLVM_ABI MachineTraceMetricsWrapperPass : public MachineFunctionPass {
public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Computed MachineTraceMetrics for the current function.
  MachineTraceMetrics MTM;

  /// Construct the legacy MachineTraceMetrics wrapper pass.
  MachineTraceMetricsWrapperPass();

  /// Record required and preserved analyses for this pass.
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Compute MachineTraceMetrics for \p MF.
  /// \param MF Machine function to analyze.
  /// \return False; this analysis pass does not modify the function.
  bool runOnMachineFunction(MachineFunction &MF) override;
  /// Release memory held by the cached metrics.
  void releaseMemory() override { MTM.clear(); }
  /// Verify the cached MachineTraceMetrics.
  void verifyAnalysis() const override { MTM.verifyAnalysis(); }
  /// Return the cached MachineTraceMetrics.
  /// \return Reference to the MachineTraceMetrics owned by this pass.
  MachineTraceMetrics &getMTM() { return MTM; }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINETRACEMETRICS_H
