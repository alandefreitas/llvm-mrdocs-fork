//===---------------------- ExecuteStage.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the execution stage of a default instruction pipeline.
///
/// The ExecuteStage is responsible for managing the hardware scheduler
/// and issuing notifications that an instruction has been executed.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_STAGES_EXECUTESTAGE_H
#define LLVM_MCA_STAGES_EXECUTESTAGE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/MCA/HardwareUnits/Scheduler.h"
#include "llvm/MCA/Instruction.h"
#include "llvm/MCA/Stages/Stage.h"

namespace llvm {
namespace mca {

/// Manages the hardware scheduler and issues execution-stage notifications.
///
/// The ExecuteStage is responsible for managing the hardware scheduler and
/// issuing notifications that an instruction has been executed.
class LLVM_ABI ExecuteStage final : public Stage {
  Scheduler &HWS;

  unsigned NumDispatchedOpcodes;
  unsigned NumIssuedOpcodes;

  // True if this stage should notify listeners of HWPressureEvents.
  bool EnablePressureEvents;

  Error issueInstruction(InstRef &IR);

  // Called at the beginning of each cycle to issue already dispatched
  // instructions to the underlying pipelines.
  Error issueReadyInstructions();

  // Used to notify instructions eliminated at register renaming stage.
  Error handleInstructionEliminated(InstRef &IR);

  ExecuteStage(const ExecuteStage &Other) = delete;
  ExecuteStage &operator=(const ExecuteStage &Other) = delete;

public:
  /// Construct an execute stage using scheduler \p S without bottleneck
  /// analysis.
  /// \param S Hardware scheduler used by this stage.
  ExecuteStage(Scheduler &S) : ExecuteStage(S, false) {}
  /// Construct an execute stage using scheduler \p S.
  /// \param S Hardware scheduler used by this stage.
  /// \param ShouldPerformBottleneckAnalysis When true, notify listeners of
  ///        HWPressureEvents.
  ExecuteStage(Scheduler &S, bool ShouldPerformBottleneckAnalysis)
      : HWS(S), NumDispatchedOpcodes(0), NumIssuedOpcodes(0),
        EnablePressureEvents(ShouldPerformBottleneckAnalysis) {}

  /// Always returns false; in-flight work is tracked by the retire stage.
  ///
  /// This stage works under the assumption that the Pipeline will eventually
  /// execute a retire stage. We don't need to check if pipelines and/or
  /// schedulers have instructions to process, because those instructions are
  /// also tracked by the retire control unit. That means,
  /// RetireControlUnit::hasWorkToComplete() is responsible for checking if there
  /// are still instructions in-flight in the out-of-order backend.
  /// \return Always false.
  bool hasWorkToComplete() const override { return false; }
  /// Return true if the scheduler can accept \p IR this cycle.
  /// \param IR Instruction to check for availability.
  /// \return True if the scheduler can accept \p IR this cycle.
  bool isAvailable(const InstRef &IR) const override;

  /// Notify the scheduler that a new cycle has started.
  ///
  /// This method is also responsible for notifying listeners about instructions
  /// state changes, and processor resources freed by the scheduler.
  /// Instructions that transitioned to the 'Executed' state are automatically
  /// moved to the next stage (i.e. RetireStage).
  /// \return Success, or an error if issuing or advancing instructions fails.
  Error cycleStart() override;
  /// Perform end-of-cycle bottleneck analysis and notify listeners if enabled.
  /// \return Success after end-of-cycle bottleneck analysis.
  Error cycleEnd() override;
  /// Schedule \p IR for execution on the hardware.
  /// \param IR Instruction to dispatch to the scheduler.
  /// \return Success, or an error if dispatching or issuing \p IR fails.
  Error execute(InstRef &IR) override;

  /// Notify listeners that \p IR has been issued to a pipeline.
  /// \param IR Instruction that was issued.
  /// \param Used Resources consumed by the issued instruction.
  void notifyInstructionIssued(const InstRef &IR,
                               MutableArrayRef<ResourceUse> Used) const;
  /// Notify listeners that \p IR has finished executing.
  /// \param IR Instruction that reached the Executed state.
  void notifyInstructionExecuted(const InstRef &IR) const;
  /// Notify listeners that \p IR is pending dependent inputs.
  /// \param IR Instruction that entered the Pending state.
  void notifyInstructionPending(const InstRef &IR) const;
  /// Notify listeners that \p IR is ready to issue.
  /// \param IR Instruction that entered the Ready state.
  void notifyInstructionReady(const InstRef &IR) const;
  /// Notify listeners that processor resource \p RR is available again.
  /// \param RR Resource that was freed.
  void notifyResourceAvailable(const ResourceRef &RR) const;

  /// Notify listeners that buffered resources have been consumed or freed.
  /// \param IR Instruction whose buffered resources changed.
  /// \param Reserved True when buffers are reserved; false when released.
  void notifyReservedOrReleasedBuffers(const InstRef &IR, bool Reserved) const;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_STAGES_EXECUTESTAGE_H
