//===---------------------- RetireStage.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the retire stage of a default instruction pipeline.
/// The RetireStage represents the process logic that interacts with the
/// simulated RetireControlUnit hardware.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_STAGES_RETIRESTAGE_H
#define LLVM_MCA_STAGES_RETIRESTAGE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MCA/HardwareUnits/LSUnit.h"
#include "llvm/MCA/HardwareUnits/RegisterFile.h"
#include "llvm/MCA/HardwareUnits/RetireControlUnit.h"
#include "llvm/MCA/Stages/Stage.h"

namespace llvm {
namespace mca {

/// Implements the retire logic of an instruction pipeline.
///
/// The RetireStage interacts with the simulated RetireControlUnit hardware.
/// It marks instructions as executed, retires them in order, frees physical
/// registers, and notifies listeners when an instruction retires.
class LLVM_ABI RetireStage final : public Stage {
  // Owner will go away when we move listeners/eventing to the stages.
  RetireControlUnit &RCU;
  RegisterFile &PRF;
  LSUnitBase &LSU;

  RetireStage(const RetireStage &Other) = delete;
  RetireStage &operator=(const RetireStage &Other) = delete;

public:
  /// Construct a retire stage for the given hardware units.
  /// \param R Retire control unit that tracks reorder-buffer entries.
  /// \param F Physical register file used when freeing register writes.
  /// \param LS Load/store unit notified when memory ops retire.
  RetireStage(RetireControlUnit &R, RegisterFile &F, LSUnitBase &LS)
      : RCU(R), PRF(F), LSU(LS) {}

  /// Returns true if the retire control unit still has in-flight instructions.
  /// \return True if the retire control unit is not empty.
  bool hasWorkToComplete() const override { return !RCU.isEmpty(); }
  /// Retires executed instructions from the reorder buffer at cycle start.
  /// \return Success after retiring executed instructions.
  Error cycleStart() override;
  /// Performs end-of-cycle bookkeeping on the physical register file.
  /// \return Success after end-of-cycle register-file bookkeeping.
  Error cycleEnd() override;
  /// Marks \p IR as executed in the register file and retire control unit.
  /// \param IR Instruction that has finished executing.
  /// \return Success after marking \p IR as executed.
  Error execute(InstRef &IR) override;
  /// Notify listeners that \p IR has retired and free its resources.
  /// \param IR Instruction that was retired.
  void notifyInstructionRetired(const InstRef &IR) const;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_STAGES_RETIRESTAGE_H
