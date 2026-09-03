//===----------------------- DispatchStage.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file models the dispatch component of an instruction pipeline.
///
/// The DispatchStage is responsible for updating instruction dependencies
/// and communicating to the simulated instruction scheduler that an instruction
/// is ready to be scheduled for execution.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_STAGES_DISPATCHSTAGE_H
#define LLVM_MCA_STAGES_DISPATCHSTAGE_H

#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MCA/HardwareUnits/RegisterFile.h"
#include "llvm/MCA/HardwareUnits/RetireControlUnit.h"
#include "llvm/MCA/Instruction.h"
#include "llvm/MCA/Stages/Stage.h"

namespace llvm {
namespace mca {

/// Implements the hardware dispatch logic of an instruction pipeline.
///
/// This class is responsible for the dispatch stage, in which instructions are
/// dispatched in groups to the Scheduler.  An instruction can be dispatched if
/// the following conditions are met:
///  1) There are enough entries in the reorder buffer (see class
///     RetireControlUnit) to write the opcodes associated with the instruction.
///  2) There are enough physical registers to rename output register operands.
///  3) There are enough entries available in the used buffered resource(s).
///
/// The number of micro opcodes that can be dispatched in one cycle is limited by
/// the value of field 'DispatchWidth'. A "dynamic dispatch stall" occurs when
/// processor resources are not available. Dispatch stall events are counted
/// during the entire execution of the code, and displayed by the performance
/// report when flag '-dispatch-stats' is specified.
///
/// If the number of micro opcodes exceedes DispatchWidth, then the instruction
/// is dispatched in multiple cycles.
class LLVM_ABI DispatchStage final : public Stage {
  unsigned DispatchWidth;
  unsigned AvailableEntries;
  unsigned CarryOver;
  InstRef CarriedOver;
  const MCSubtargetInfo &STI;
  RetireControlUnit &RCU;
  RegisterFile &PRF;

  bool checkRCU(const InstRef &IR) const;
  bool checkPRF(const InstRef &IR) const;
  bool canDispatch(const InstRef &IR) const;
  Error dispatch(InstRef IR);

  void notifyInstructionDispatched(const InstRef &IR,
                                   ArrayRef<unsigned> UsedPhysRegs,
                                   unsigned uOps) const;

public:
  /// Construct a dispatch stage for the given subtarget and hardware units.
  /// \param Subtarget Subtarget info used when updating register reads.
  /// \param MRI Target register info.
  /// \param MaxDispatchWidth Maximum micro-ops dispatched per cycle; zero means
  ///        use the subtarget IssueWidth.
  /// \param R Retire control unit that tracks reorder-buffer entries.
  /// \param F Physical register file used for register renaming.
  DispatchStage(const MCSubtargetInfo &Subtarget, const MCRegisterInfo &MRI,
                unsigned MaxDispatchWidth, RetireControlUnit &R,
                RegisterFile &F);

  /// Returns true if \p IR can be dispatched during this cycle.
  /// \param IR Instruction to check for dispatch availability.
  /// \return True if \p IR can be dispatched this cycle.
  bool isAvailable(const InstRef &IR) const override;

  /// Returns false because this stage never buffers work across cycles.
  ///
  /// The dispatch logic internally doesn't buffer instructions. So there is
  /// never work to do at the beginning of every cycle.
  /// \return Always false.
  bool hasWorkToComplete() const override { return false; }

  /// Resets dispatch bandwidth and continues multi-cycle dispatches.
  /// \return Success after updating available dispatch entries.
  Error cycleStart() override;

  /// Dispatches \p IR after verifying that it can move to the next stage.
  /// \param IR Instruction to dispatch.
  /// \return Success, or an error if dispatch or the next stage fails.
  Error execute(InstRef &IR) override;

#ifndef NDEBUG
  /// Dumps the physical register file and retire control unit state.
  void dump() const;
#endif
};
} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_STAGES_DISPATCHSTAGE_H
