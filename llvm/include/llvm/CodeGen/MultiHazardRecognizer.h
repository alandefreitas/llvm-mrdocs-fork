//=- llvm/CodeGen/MultiHazardRecognizer.h - Scheduling Support ----*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the MultiHazardRecognizer class, which is a wrapper
// for a set of ScheduleHazardRecognizer instances
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MULTIHAZARDRECOGNIZER_H
#define LLVM_CODEGEN_MULTIHAZARDRECOGNIZER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"

namespace llvm {

class MachineInstr;
class SUnit;

/// Composite hazard recognizer that delegates to a set of recognizers.
///
/// Owns multiple ScheduleHazardRecognizer instances and forwards scheduling
/// callbacks to each of them, combining their hazard results.
class LLVM_ABI MultiHazardRecognizer : public ScheduleHazardRecognizer {
  SmallVector<std::unique_ptr<ScheduleHazardRecognizer>, 4> Recognizers;

public:
  /// Construct an empty multi-hazard recognizer with no child recognizers.
  MultiHazardRecognizer() = default;

  /// Deleted copy constructor; MultiHazardRecognizer cannot be copied.
  /// \param Other Unused source recognizer.
  MultiHazardRecognizer(const MultiHazardRecognizer &Other) = delete;
  /// Deleted copy assignment; MultiHazardRecognizer cannot be copied.
  /// \param Other Unused source recognizer.
  MultiHazardRecognizer &operator=(const MultiHazardRecognizer &Other) = delete;

  /// Take ownership of \p Recognizer and add it to this composite.
  ///
  /// Updates MaxLookAhead to the maximum among all owned recognizers.
  /// \param Recognizer Hazard recognizer to own and consult.
  void AddHazardRecognizer(std::unique_ptr<ScheduleHazardRecognizer> &&Recognizer);

  /// Return true if any owned recognizer reports that no more instructions
  /// may be issued in this cycle.
  /// \return True if no more instructions may be issued in this cycle.
  bool atIssueLimit() const override;
  /// Return the most severe hazard type among owned recognizers for \p SU.
  ///
  /// Returns the first non-NoHazard result from a child recognizer, or
  /// NoHazard if none report a hazard.
  /// \param SU Scheduling unit being considered for issue.
  /// \param Stalls Cycle offset at which \p SU would be scheduled.
  /// \return The most severe HazardType reported by any owned recognizer.
  HazardType getHazardType(SUnit *SU, int Stalls = 0) override;
  /// Reset all owned recognizers to an initialized state.
  void Reset() override;
  /// Notify all owned recognizers that \p SU has been emitted.
  /// \param SU Scheduling unit that was emitted.
  void EmitInstruction(SUnit *SU) override;
  /// Notify all owned recognizers that \p MI has been emitted.
  /// \param MI Machine instruction that was emitted.
  void EmitInstruction(MachineInstr *MI) override;
  /// Return the maximum number of noops any owned recognizer requires before \p SU.
  /// \param SU Scheduling unit about to be emitted.
  /// \return Maximum noop count required before emitting \p SU.
  unsigned PreEmitNoops(SUnit *SU) override;
  /// Return the maximum number of noops any owned recognizer requires before \p MI.
  /// \param MI Machine instruction about to be emitted.
  /// \return Maximum noop count required before emitting \p MI.
  unsigned PreEmitNoops(MachineInstr *MI) override;
  /// Return true if any owned recognizer prefers another instruction over \p SU.
  /// \param SU Scheduling unit under consideration.
  /// \return True if another instruction should be preferred over \p SU.
  bool ShouldPreferAnother(SUnit *SU) const override;
  /// Advance the cycle in all owned recognizers.
  void AdvanceCycle() override;
  /// Recede the cycle in all owned recognizers.
  void RecedeCycle() override;
  /// Notify all owned recognizers that a noop was emitted.
  void EmitNoop() override;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MULTIHAZARDRECOGNIZER_H
