//======----------- WindowScheduler.cpp - window scheduler -------------======//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An implementation of the Window Scheduling software pipelining algorithm.
//
// The concept of the window algorithm was first unveiled in Steven Muchnick's
// book, "Advanced Compiler Design And Implementation", and later elaborated
// upon in Venkatraman Govindaraju's report, "Implementation of Software
// Pipelining Using Window Scheduling".
//
// The window algorithm can be perceived as a modulo scheduling algorithm with a
// stage count of 2. It boasts a higher scheduling success rate in targets with
// severe resource conflicts when compared to the classic Swing Modulo
// Scheduling (SMS) algorithm. To align with the LLVM scheduling framework, we
// have enhanced the original window algorithm. The primary steps are as
// follows:
//
// 1. Instead of duplicating the original MBB twice as mentioned in the
// literature, we copy it three times, generating TripleMBB and the
// corresponding TripleDAG.
//
// 2. We establish a scheduling window on TripleMBB and execute list scheduling
// within it.
//
// 3. After multiple list scheduling, we select the best outcome and expand it
// into the final scheduling result.
//
// To cater to the needs of various targets, we have developed the window
// scheduler in a form that is easily derivable. We recommend employing this
// algorithm in targets with severe resource conflicts, and it can be utilized
// either before or after the Register Allocator (RA).
//
// The default implementation provided here is before RA. If it is to be used
// after RA, certain critical algorithm functions will need to be derived.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_WINDOWSCHEDULER_H
#define LLVM_CODEGEN_WINDOWSCHEDULER_H

#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

namespace llvm {

/// Controls whether and when the window scheduling algorithm is used.
enum WindowSchedulingFlag {
  /// Turn off the window algorithm.
  WS_Off,
  /// Use the window algorithm after the SMS algorithm fails.
  WS_On,
  /// Use the window algorithm instead of the SMS algorithm.
  WS_Force
};

/// The main class in the implementation of the target independent window
/// scheduler.
class LLVM_ABI WindowScheduler {
protected:
  /// Machine scheduling context providing analyses for window scheduling.
  MachineSchedContext *Context = nullptr;
  /// Machine function containing the loop being scheduled.
  MachineFunction *MF = nullptr;
  /// Machine basic block of the loop body being scheduled.
  MachineBasicBlock *MBB = nullptr;
  /// Machine loop being window-scheduled.
  MachineLoop &Loop;
  /// Subtarget information for the current target.
  const TargetSubtargetInfo *Subtarget = nullptr;
  /// Target instruction information used during scheduling.
  const TargetInstrInfo *TII = nullptr;
  /// Target register information used during scheduling.
  const TargetRegisterInfo *TRI = nullptr;
  /// Machine register information for the current function.
  MachineRegisterInfo *MRI = nullptr;

  /// ScheduleDAG built from three copies of the original MBB.
  ///
  /// To innovatively identify the dependencies between MIs across two trips, we
  /// construct a DAG for a new MBB, which is created by copying the original
  /// MBB three times. We refer to this new MBB as 'TripleMBB' and the
  /// corresponding DAG as 'TripleDAG'.
  /// If the dependencies are more than two trips, we avoid applying window
  /// algorithm by identifying successive phis in the old MBB.
  std::unique_ptr<ScheduleDAGInstrs> TripleDAG;
  /// OriMIs keeps the MIs removed from the original MBB.
  SmallVector<MachineInstr *> OriMIs;
  /// TriMIs keeps the MIs of TripleMBB, which is used to restore TripleMBB.
  SmallVector<MachineInstr *> TriMIs;
  /// TriToOri keeps the mappings between the MI clones in TripleMBB and their
  /// original MI.
  DenseMap<MachineInstr *, MachineInstr *> TriToOri;
  /// OriToCycle keeps the mappings between the original MI and its issue cycle.
  DenseMap<MachineInstr *, int> OriToCycle;
  /// SchedResult keeps the result of each list scheduling, and the format of
  /// the tuple is <MI pointer, Cycle, Stage, Order ID>.
  SmallVector<std::tuple<MachineInstr *, int, int, int>, 256> SchedResult;
  /// SchedPhiNum records the number of phi in the original MBB, and the
  /// scheduling starts with MI after phis.
  unsigned SchedPhiNum = 0;
  /// SchedInstrNum records the MIs involved in scheduling in the original MBB,
  /// excluding debug instructions.
  unsigned SchedInstrNum = 0;
  /// BestII and BestOffset record the characteristics of the best scheduling
  /// result and are used together with SchedResult as the final window
  /// scheduling result.
  unsigned BestII = UINT_MAX;
  /// Window offset of the best scheduling result found so far.
  unsigned BestOffset = 0;
  /// BaseII is the II obtained when the window offset is SchedPhiNum. This
  /// offset is the initial position of the sliding window.
  unsigned BaseII = 0;

public:
  /// Construct a window scheduler for the given context and loop.
  /// \param C Machine scheduling context providing analyses and the function.
  /// \param ML Machine loop to be window-scheduled.
  WindowScheduler(MachineSchedContext *C, MachineLoop &ML);
  /// Destroy the window scheduler.
  virtual ~WindowScheduler() = default;

  /// Run the window scheduling algorithm on the loop.
  /// \return True if window scheduling succeeded and was applied.
  bool run();

protected:
  /// Two types of ScheduleDAGs are needed, one for creating dependency graphs
  /// only, and the other for list scheduling as determined by the target.
  /// \param OnlyBuildGraph If true, create a DAG used only to build
  ///        dependencies; otherwise create a target list-scheduling DAG.
  /// \return A ScheduleDAG for dependency analysis or target list scheduling.
  virtual ScheduleDAGInstrs *
  createMachineScheduler(bool OnlyBuildGraph = false);
  /// Initializes the algorithm and determines if it can be executed.
  /// \return True if the window algorithm can proceed on this loop.
  virtual bool initialize();
  /// Add some related processing before running window scheduling.
  virtual void preProcess();
  /// Add some related processing after running window scheduling.
  virtual void postProcess();
  /// Back up the MIs in the original MBB and remove them from MBB.
  void backupMBB();
  /// Erase the MIs in current MBB and restore the original MIs.
  void restoreMBB();
  /// Make three copies of the original MBB to generate a new TripleMBB.
  virtual void generateTripleMBB();
  /// Restore the order of MIs in TripleMBB after each list scheduling.
  virtual void restoreTripleMBB();
  /// Give the folding position in the window algorithm, where different
  /// heuristics can be used. It determines the performance and compilation time
  /// of the algorithm.
  /// \param SearchNum Number of folding positions to consider.
  /// \param SearchRatio Ratio used to select folding positions within the
  ///        window.
  /// \return Indexes of the folding positions to try during the search.
  virtual SmallVector<unsigned> getSearchIndexes(unsigned SearchNum,
                                                 unsigned SearchRatio);
  /// Calculate MIs execution cycle after list scheduling.
  /// \param DAG Schedule DAG produced by list scheduling.
  /// \param Offset Window offset used for this scheduling attempt.
  /// \return The maximum MI execution cycle after list scheduling.
  virtual int calculateMaxCycle(ScheduleDAGInstrs &DAG, unsigned Offset);
  /// Calculate the stall cycle between two trips after list scheduling.
  /// \param Offset Window offset used for this scheduling attempt.
  /// \param MaxCycle Maximum MI execution cycle from list scheduling.
  /// \return The stall cycle count needed between two trips.
  virtual int calculateStallCycle(unsigned Offset, int MaxCycle);
  /// Analyzes the II value after each list scheduling.
  /// \param DAG Schedule DAG produced by list scheduling.
  /// \param Offset Window offset used for this scheduling attempt.
  /// \return The initiation interval for this scheduling attempt.
  virtual unsigned analyseII(ScheduleDAGInstrs &DAG, unsigned Offset);
  /// Phis are scheduled separately after each list scheduling.
  /// \param Offset Window offset used for this scheduling attempt.
  /// \param II Initiation interval to update while scheduling phis.
  virtual void schedulePhi(int Offset, unsigned &II);
  /// Get the final issue order of all scheduled MIs including phis.
  /// \param Offset Window offset used for this scheduling attempt.
  /// \param II Initiation interval of the current schedule.
  /// \return A map from each scheduled MI to its issue order index.
  DenseMap<MachineInstr *, int> getIssueOrder(unsigned Offset, unsigned II);
  /// Update the scheduling result after each list scheduling.
  /// \param Offset Window offset used for this scheduling attempt.
  /// \param II Initiation interval of the current schedule.
  virtual void updateScheduleResult(unsigned Offset, unsigned II);
  /// Check whether the final result of window scheduling is valid.
  /// \return True if a valid window schedule was found.
  virtual bool isScheduleValid() { return BestOffset != SchedPhiNum; }
  /// Using the scheduling infrastructure to expand the results of window
  /// scheduling. It is usually necessary to add prologue and epilogue MBBs.
  virtual void expand();
  /// Update the live intervals for all registers used within MBB.
  virtual void updateLiveIntervals();
  /// Estimate a II value at which all MIs will be scheduled successfully.
  /// \param DAG Schedule DAG used to estimate a feasible II.
  /// \return An estimated initiation interval for successful scheduling.
  int getEstimatedII(ScheduleDAGInstrs &DAG);
  /// Gets the iterator range of MIs in the scheduling window.
  /// \param Offset Starting offset of the scheduling window.
  /// \param Num Number of instructions in the scheduling window.
  /// \return An iterator range over the MIs in the scheduling window.
  iterator_range<MachineBasicBlock::iterator> getScheduleRange(unsigned Offset,
                                                               unsigned Num);
  /// Get the issue cycle of the new MI based on the cycle of the original MI.
  /// \param NewMI Cloned MI whose issue cycle is requested.
  /// \return The issue cycle of the original MI corresponding to \p NewMI.
  int getOriCycle(MachineInstr *NewMI);
  /// Get the original MI from which the new MI is cloned.
  /// \param NewMI Cloned MI whose original MI is requested.
  /// \return The original MI from which \p NewMI was cloned.
  MachineInstr *getOriMI(MachineInstr *NewMI);
  /// Get the scheduling stage, where the stage of the new MI is identical to
  /// the original MI.
  /// \param OriMI Original MI whose scheduling stage is requested.
  /// \param Offset Window offset used to compute the stage.
  /// \return The scheduling stage of \p OriMI for the given window offset.
  unsigned getOriStage(MachineInstr *OriMI, unsigned Offset);
  /// Gets the register in phi which is generated from the current MBB.
  /// \param Phi Phi instruction whose anti-dependent register is requested.
  /// \return The register from the current MBB, or an invalid register if none.
  Register getAntiRegister(MachineInstr *Phi);
};
} // namespace llvm
#endif
