//===---------------------- InOrderIssueStage.h -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// InOrderIssueStage implements an in-order execution pipeline.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_STAGES_INORDERISSUESTAGE_H
#define LLVM_MCA_STAGES_INORDERISSUESTAGE_H

#include "llvm/MCA/CustomBehaviour.h"
#include "llvm/MCA/HardwareUnits/ResourceManager.h"
#include "llvm/MCA/SourceMgr.h"
#include "llvm/MCA/Stages/Stage.h"

namespace llvm {
namespace mca {
class LSUnitBase;
class RegisterFile;

/// Tracks a stalled instruction and the remaining stall duration.
struct StallInfo {
  /// Reason an instruction could not issue in the current cycle.
  enum class StallKind {
    /// Unspecified or cleared stall state.
    DEFAULT,
    /// Waiting on unresolved register data dependencies.
    REGISTER_DEPS,
    /// Waiting for dispatch or processor resources.
    DISPATCH,
    /// Delayed to preserve in-order write-back commit.
    DELAY,
    /// Blocked by the load/store unit.
    LOAD_STORE,
    /// Waiting on a target-specific custom hazard.
    CUSTOM_STALL
  };

  /// Instruction that is currently stalled, if any.
  InstRef IR;
  /// Remaining cycles before the stalled instruction may be retried.
  unsigned CyclesLeft = 0;
  /// Kind of hazard that caused the stall.
  StallKind Kind = StallKind::DEFAULT;

  /// Construct an empty stall record with no active instruction.
  StallInfo() = default;

  /// Returns the kind of stall currently recorded.
  /// \return The current stall kind.
  StallKind getStallKind() const { return Kind; }
  /// Returns the number of cycles left in the stall.
  /// \return Remaining stall cycles.
  unsigned getCyclesLeft() const { return CyclesLeft; }
  /// Returns a const reference to the stalled instruction.
  /// \return Const reference to the stalled instruction.
  const InstRef &getInstruction() const { return IR; }
  /// Returns a mutable reference to the stalled instruction.
  /// \return Mutable reference to the stalled instruction.
  InstRef &getInstruction() { return IR; }

  /// Returns true if a stalled instruction is currently recorded.
  /// \return True if a stalled instruction is present.
  bool isValid() const { return (bool)IR; }
  /// Clears the stall record and resets cycles and kind.
  LLVM_ABI void clear();
  /// Records a stall for \p Inst lasting \p Cycles with reason \p SK.
  /// \param Inst Instruction that could not issue.
  /// \param Cycles Number of cycles to wait before retrying.
  /// \param SK Stall reason associated with this hazard.
  LLVM_ABI void update(const InstRef &Inst, unsigned Cycles, StallKind SK);
  /// Decrements the remaining stall cycles at the end of a cycle.
  LLVM_ABI void cycleEnd();
};

/// Pipeline stage that issues and executes instructions in program order.
///
/// Combines issue, execute, and retire for an in-order backend: instructions
/// are issued when register, resource, memory, and custom hazards allow, and
/// writes commit in program order.
class LLVM_ABI InOrderIssueStage final : public Stage {
  const MCSubtargetInfo &STI;
  RegisterFile &PRF;
  ResourceManager RM;
  CustomBehaviour &CB;
  LSUnitBase &LSU;

  /// Instructions that were issued, but not executed yet.
  SmallVector<InstRef, 4> IssuedInst;

  /// Number of instructions issued in the current cycle.
  unsigned NumIssued;

  StallInfo SI;

  /// Instruction that is issued in more than 1 cycle.
  InstRef CarriedOver;
  /// Number of CarriedOver uops left to issue.
  unsigned CarryOver;

  /// Number of instructions that can be issued in the current cycle.
  unsigned Bandwidth;

  /// Number of cycles (counted from the current cycle) until the last write is
  /// committed. This is taken into account to ensure that writes commit in the
  /// program order.
  unsigned LastWriteBackCycle;

  InOrderIssueStage(const InOrderIssueStage &Other) = delete;
  InOrderIssueStage &operator=(const InOrderIssueStage &Other) = delete;

  /// Returns true if IR can execute during this cycle.
  /// In case of stall, it updates SI with information about the stalled
  /// instruction and the stall reason.
  bool canExecute(const InstRef &IR);

  /// Issue the instruction, or update the StallInfo.
  Error tryIssue(InstRef &IR);

  /// Update status of instructions from IssuedInst.
  void updateIssuedInst();

  /// Continue to issue the CarriedOver instruction.
  void updateCarriedOver();

  /// Notifies a stall event to the Stage listener. Stall information is
  /// obtained from the internal StallInfo field.
  void notifyStallEvent();

  void notifyInstructionIssued(const InstRef &IR,
                               ArrayRef<ResourceUse> UsedRes);
  void notifyInstructionDispatched(const InstRef &IR, unsigned Ops,
                                   ArrayRef<unsigned> UsedRegs);
  void notifyInstructionExecuted(const InstRef &IR);
  void notifyInstructionRetired(const InstRef &IR,
                                ArrayRef<unsigned> FreedRegs);

  /// Retire instruction once it is executed.
  void retireInstruction(InstRef &IR);

public:
  /// Construct an in-order issue stage for the given subtarget and units.
  /// \param STI Subtarget information providing the scheduling model.
  /// \param PRF Physical register file used for renaming and hazards.
  /// \param CB Target-specific custom behaviour hooks.
  /// \param LSU Load/store unit used for memory operations.
  InOrderIssueStage(const MCSubtargetInfo &STI, RegisterFile &PRF,
                    CustomBehaviour &CB, LSUnitBase &LSU);

  /// Returns the maximum number of micro-ops that can issue per cycle.
  /// \return Issue width in micro-ops per cycle.
  unsigned getIssueWidth() const;
  /// Returns true if \p IR can be accepted for issue this cycle.
  /// \param IR Instruction being considered for issue.
  /// \return True if this stage can accept \p IR this cycle.
  bool isAvailable(const InstRef &IR) const override;
  /// Returns true if issued, stalled, or carried-over work remains.
  /// \return True if this stage still has unfinished work.
  bool hasWorkToComplete() const override;
  /// Attempts to issue \p IR, updating stall state on failure.
  /// \param IR Instruction to issue in this stage.
  /// \return Success, or an error if issue fails unexpectedly.
  Error execute(InstRef &IR) override;
  /// Prepares bandwidth and retries stalled instructions for a new cycle.
  /// \return Success, or an error if cycle setup fails.
  Error cycleStart() override;
  /// Advances stall and write-back counters at the end of a cycle.
  /// \return Success, or an error if cycle teardown fails.
  Error cycleEnd() override;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_STAGES_INORDERISSUESTAGE_H
