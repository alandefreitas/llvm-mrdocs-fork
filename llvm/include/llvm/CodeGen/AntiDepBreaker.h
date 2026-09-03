//===- llvm/CodeGen/AntiDepBreaker.h - Anti-Dependence Breaking -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the AntiDepBreaker class, which implements
// anti-dependence breaking heuristics for post-register-allocation scheduling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ANTIDEPBREAKER_H
#define LLVM_CODEGEN_ANTIDEPBREAKER_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/Compiler.h"
#include <utility>
#include <vector>

namespace llvm {

class RegisterClassInfo;

/// This class works in conjunction with the post-RA scheduler to rename
/// registers to break register anti-dependencies (WAR hazards).
class LLVM_ABI AntiDepBreaker {
public:
  /// Pairs of DBG_VALUE (or DBG_PHI) instructions with the parent instruction
  /// whose register rename may affect them.
  using DbgValueVector =
      std::vector<std::pair<MachineInstr *, MachineInstr *>>;

  /// Virtual destructor.
  virtual ~AntiDepBreaker();

  /// Initialize anti-dep breaking for a new basic block.
  ///
  /// \param BB Basic block for which anti-dep breaking is started.
  virtual void StartBlock(MachineBasicBlock *BB) = 0;

  /// Identify anti-dependencies within a basic-block region and break them by
  /// renaming registers.
  ///
  /// \param SUnits Schedule units for the region being considered.
  /// \param Begin First instruction in the region.
  /// \param End One-past-the-last instruction in the region.
  /// \param InsertPosIndex Index of the schedule insert position within the
  ///        region.
  /// \param DbgValues DBG_VALUE / DBG_PHI instructions that may need updating
  ///        when registers are renamed.
  /// \return The number of anti-dependencies broken.
  virtual unsigned BreakAntiDependencies(const std::vector<SUnit> &SUnits,
                                         MachineBasicBlock::iterator Begin,
                                         MachineBasicBlock::iterator End,
                                         unsigned InsertPosIndex,
                                         DbgValueVector &DbgValues) = 0;

  /// Update liveness information to account for the current
  /// instruction, which will not be scheduled.
  ///
  /// \param MI Instruction that will not be scheduled.
  /// \param Count Position of \p MI within the scheduling region.
  /// \param InsertPosIndex Index of the schedule insert position within the
  ///        region.
  virtual void Observe(MachineInstr &MI, unsigned Count,
                       unsigned InsertPosIndex) = 0;

  /// Finish anti-dep breaking for a basic block.
  virtual void FinishBlock() = 0;

  /// Update DBG_VALUE or DBG_PHI if dependency breaker is updating
  /// other machine instruction to use NewReg.
  ///
  /// \param MI DBG_VALUE or DBG_PHI instruction to update.
  /// \param OldReg Register being replaced.
  /// \param NewReg Replacement register.
  void UpdateDbgValue(MachineInstr &MI, MCRegister OldReg, MCRegister NewReg) {
    if (MI.isDebugValue()) {
      if (MI.getDebugOperand(0).isReg() &&
          MI.getDebugOperand(0).getReg() == OldReg)
        MI.getDebugOperand(0).setReg(NewReg);
    } else if (MI.isDebugPHI()) {
      if (MI.getOperand(0).isReg() &&
          MI.getOperand(0).getReg() == OldReg)
        MI.getOperand(0).setReg(NewReg);
    } else {
      llvm_unreachable("MI is not DBG_VALUE / DBG_PHI!");
    }
  }

  /// Update all DBG_VALUE instructions that may be affected by the dependency
  /// breaker's update of ParentMI to use NewReg.
  ///
  /// \param DbgValues DBG_VALUE / DBG_PHI instructions paired with the parent
  ///        instructions that may affect them.
  /// \param ParentMI Instruction whose register rename may affect debug
  ///        values.
  /// \param OldReg Register being replaced.
  /// \param NewReg Replacement register.
  void UpdateDbgValues(const DbgValueVector &DbgValues, MachineInstr *ParentMI,
                       MCRegister OldReg, MCRegister NewReg) {
    // The following code is dependent on the order in which the DbgValues are
    // constructed in ScheduleDAGInstrs::buildSchedGraph.
    MachineInstr *PrevDbgMI = nullptr;
    for (const auto &DV : make_range(DbgValues.crbegin(), DbgValues.crend())) {
      MachineInstr *PrevMI = DV.second;
      if ((PrevMI == ParentMI) || (PrevMI == PrevDbgMI)) {
        MachineInstr *DbgMI = DV.first;
        UpdateDbgValue(*DbgMI, OldReg, NewReg);
        PrevDbgMI = DbgMI;
      } else if (PrevDbgMI) {
        break; // If no match and already found a DBG_VALUE, we're done.
      }
    }
  }
};

/// Create an aggressive anti-dependence breaker.
///
/// \param MFi Machine function for which anti-dependencies are broken.
/// \param RCI Register class information used by the breaker.
/// \param CriticalPathRCs Register classes considered along the critical path.
/// \return A new aggressive anti-dependence breaker.
LLVM_ABI AntiDepBreaker *createAggressiveAntiDepBreaker(
    MachineFunction &MFi, const RegisterClassInfo &RCI,
    TargetSubtargetInfo::RegClassVector &CriticalPathRCs);

/// Create a critical-path anti-dependence breaker.
///
/// \param MFi Machine function for which anti-dependencies are broken.
/// \param RCI Register class information used by the breaker.
/// \return A new critical-path anti-dependence breaker.
LLVM_ABI AntiDepBreaker *
createCriticalAntiDepBreaker(MachineFunction &MFi,
                             const RegisterClassInfo &RCI);

} // end namespace llvm

#endif // LLVM_CODEGEN_ANTIDEPBREAKER_H
