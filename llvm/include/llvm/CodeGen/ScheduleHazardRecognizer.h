//=- llvm/CodeGen/ScheduleHazardRecognizer.h - Scheduling Support -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ScheduleHazardRecognizer class, which implements
// hazard-avoidance heuristics for scheduling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULEHAZARDRECOGNIZER_H
#define LLVM_CODEGEN_SCHEDULEHAZARDRECOGNIZER_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class MachineInstr;
class SUnit;

/// HazardRecognizer - This determines whether or not an instruction can be
/// issued this cycle, and whether or not a noop needs to be inserted to handle
/// the hazard.
class LLVM_ABI ScheduleHazardRecognizer {
protected:
  /// Number of cycles tracked in the scoreboard state.
  ///
  /// Important to restore the state after backtracking. Additionally,
  /// MaxLookAhead=0 identifies a fake recognizer, allowing the client to
  /// bypass virtual calls. Currently the PostRA scheduler ignores it.
  unsigned MaxLookAhead = 0;

public:
  /// Construct a disabled hazard recognizer with no look-ahead.
  ScheduleHazardRecognizer() = default;
  /// Destroy the hazard recognizer.
  virtual ~ScheduleHazardRecognizer();

  /// Classification of whether an instruction can issue in the current cycle.
  enum HazardType {
    /// The instruction can be emitted at this cycle.
    NoHazard,
    /// The instruction can't be emitted at this cycle.
    Hazard,
    /// The instruction can't be emitted and needs noops.
    NoopHazard
  };

  /// Return the number of cycles in the scoreboard look-ahead.
  /// \return Number of cycles tracked in the scoreboard look-ahead.
  unsigned getMaxLookAhead() const { return MaxLookAhead; }

  /// Return true if this recognizer is active (MaxLookAhead is non-zero).
  /// \return True if MaxLookAhead is non-zero.
  bool isEnabled() const { return MaxLookAhead != 0; }

  /// atIssueLimit - Return true if no more instructions may be issued in this
  /// cycle.
  ///
  /// FIXME: remove this once MachineScheduler is the only client.
  /// \return True if no more instructions may be issued in this cycle.
  virtual bool atIssueLimit() const { return false; }

  /// Return the hazard type of emitting this node.
  ///
  /// There are three possible results. Either:
  ///  * NoHazard: it is legal to issue this instruction on this cycle.
  ///  * Hazard: issuing this instruction would stall the machine.  If some
  ///     other instruction is available, issue it first.
  ///  * NoopHazard: issuing this instruction would break the program.  If
  ///     some other instruction can be issued, do so, otherwise issue a noop.
  /// \param SU Scheduling unit being considered for issue.
  /// \param Stalls Cycle offset at which \p SU would be scheduled.
  /// \return Hazard classification for issuing \p SU at the given stall offset.
  virtual HazardType getHazardType(SUnit *SU, int Stalls = 0) {
    return NoHazard;
  }

  /// Reset - This callback is invoked when a new block of
  /// instructions is about to be schedule. The hazard state should be
  /// set to an initialized state.
  virtual void Reset() {}

  /// EmitInstruction - This callback is invoked when an instruction is
  /// emitted, to advance the hazard state.
  /// \param SU Scheduling unit that was emitted.
  virtual void EmitInstruction(SUnit *SU) {}

  /// This overload will be used when the hazard recognizer is being used
  /// by a non-scheduling pass, which does not use SUnits.
  /// \param MI Machine instruction that was emitted.
  virtual void EmitInstruction(MachineInstr *MI) {}

  /// Return the number of noops to emit before the given instruction.
  ///
  /// This callback is invoked prior to emitting an instruction. It should
  /// return the number of noops to emit prior to the provided instruction.
  /// Note: This is only used during PostRA scheduling. EmitNoop is not called
  /// for these noops.
  /// \param SU Scheduling unit about to be emitted.
  /// \return Number of noops to emit before \p SU.
  virtual unsigned PreEmitNoops(SUnit *SU) {
    return 0;
  }

  /// This overload will be used when the hazard recognizer is being used
  /// by a non-scheduling pass, which does not use SUnits.
  /// \param MI Machine instruction about to be emitted.
  /// \return Number of noops to emit before \p MI.
  virtual unsigned PreEmitNoops(MachineInstr *MI) {
    return 0;
  }

  /// Return true if another available instruction should be preferred.
  ///
  /// This callback may be invoked if getHazardType returns NoHazard. If, even
  /// though there is no hazard, it would be better to schedule another
  /// available instruction, this callback should return true.
  /// \param SU Scheduling unit under consideration.
  /// \return True if another available instruction should be preferred.
  virtual bool ShouldPreferAnother(SUnit *SU) const { return false; }

  /// Advance the recognizer by one cycle for top-down scheduling.
  ///
  /// This callback is invoked whenever the next top-down instruction to be
  /// scheduled cannot issue in the current cycle, either because of latency or
  /// resource conflicts. This should increment the internal state of the
  /// hazard recognizer so that previously "Hazard" instructions will now not
  /// be hazards.
  virtual void AdvanceCycle() {}

  /// Recede the recognizer by one cycle for bottom-up scheduling.
  ///
  /// This callback is invoked whenever the next bottom-up instruction to be
  /// scheduled cannot issue in the current cycle, either because of latency or
  /// resource conflicts.
  virtual void RecedeCycle() {}

  /// EmitNoop - This callback is invoked when a noop was added to the
  /// instruction stream.
  virtual void EmitNoop() {
    // Default implementation: count it as a cycle.
    AdvanceCycle();
  }

  /// EmitNoops - This callback is invoked when noops were added to the
  /// instruction stream.
  /// \param Quantity Number of noops that were added.
  virtual void EmitNoops(unsigned Quantity) {
    // Default implementation: count it as a cycle.
    for (unsigned i = 0; i < Quantity; ++i)
      EmitNoop();
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SCHEDULEHAZARDRECOGNIZER_H
