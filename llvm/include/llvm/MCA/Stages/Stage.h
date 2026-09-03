//===---------------------- Stage.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a stage.
/// A chain of stages compose an instruction pipeline.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_STAGES_STAGE_H
#define LLVM_MCA_STAGES_STAGE_H

#include "llvm/MCA/HWEventListener.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <set>

namespace llvm {
namespace mca {

class InstRef;

/// A single stage in an instruction pipeline.
///
/// Stages are chained together to simulate the hardware pipeline. Each stage
/// processes instructions, optionally notifies listeners of hardware events,
/// and hands work off to its successor via \c moveToTheNextStage.
class LLVM_ABI Stage {
  Stage *NextInSequence = nullptr;
  std::set<HWEventListener *> Listeners;

  Stage(const Stage &Other) = delete;
  Stage &operator=(const Stage &Other) = delete;

protected:
  /// Returns the set of hardware event listeners registered with this stage.
  /// \return The registered hardware event listeners.
  const std::set<HWEventListener *> &getListeners() const { return Listeners; }

public:
  /// Construct a stage with no successor and no listeners.
  Stage() = default;
  /// Destroy this stage.
  virtual ~Stage();

  /// Returns true if it can execute IR during this cycle.
  /// \param IR Instruction to check for availability.
  /// \return True if this stage can accept \p IR during this cycle.
  virtual bool isAvailable(const InstRef &IR) const { return true; }

  /// Returns true if some instructions are still executing this stage.
  /// \return True if this stage still has unfinished work.
  virtual bool hasWorkToComplete() const = 0;

  /// Called once at the start of each cycle.  This can be used as a setup
  /// phase to prepare for the executions during the cycle.
  /// \return Success (\c ErrorSuccess) unless a derived stage reports failure.
  virtual Error cycleStart() { return ErrorSuccess(); }

  /// Called after the pipeline is resumed from pausing state.
  /// \return Success (\c ErrorSuccess) unless a derived stage reports failure.
  virtual Error cycleResume() { return ErrorSuccess(); }

  /// Called once at the end of each cycle.
  /// \return Success (\c ErrorSuccess) unless a derived stage reports failure.
  virtual Error cycleEnd() { return ErrorSuccess(); }

  /// The primary action that this stage performs on instruction IR.
  /// \param IR Instruction to process in this stage.
  /// \return Success, or an error if this stage fails to process \p IR.
  virtual Error execute(InstRef &IR) = 0;

  /// Set the successor stage that receives instructions from this stage.
  /// \param NextStage Next stage in the pipeline sequence.
  void setNextInSequence(Stage *NextStage) {
    assert(!NextInSequence && "This stage already has a NextInSequence!");
    NextInSequence = NextStage;
  }

  /// Returns true if the next stage exists and can accept \p IR this cycle.
  /// \param IR Instruction to check against the successor stage.
  /// \return True if a successor exists and is available for \p IR.
  bool checkNextStage(const InstRef &IR) const {
    return NextInSequence && NextInSequence->isAvailable(IR);
  }

  /// Called when an instruction is ready to move the next pipeline stage.
  ///
  /// Stages are responsible for moving instructions to their immediate
  /// successor stages.
  /// \param IR Instruction to hand off to the next stage.
  /// \return Success, or an error from the successor stage's execute.
  Error moveToTheNextStage(InstRef &IR) {
    assert(checkNextStage(IR) && "Next stage is not ready!");
    return NextInSequence->execute(IR);
  }

  /// Add a listener to receive callbacks during the execution of this stage.
  /// \param Listener Hardware event listener to register.
  void addListener(HWEventListener *Listener);

  /// Notify listeners of a particular hardware event.
  /// \param Event Hardware event forwarded to each registered listener.
  template <typename EventT> void notifyEvent(const EventT &Event) const {
    for (HWEventListener *Listener : Listeners)
      Listener->onEvent(Event);
  }
};

/// This is actually not an error but a marker to indicate that
/// the instruction stream is paused.
struct InstStreamPause : public ErrorInfo<InstStreamPause> {
  /// RTTI identifier used by ErrorInfo::classID.
  LLVM_ABI static char ID;

  /// Convert this error to a \c std::error_code.
  ///
  /// \return An inconvertible error code.
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }
  /// Write a pause message to \p OS.
  ///
  /// \param OS Stream to receive the message.
  void log(raw_ostream &OS) const override { OS << "Stream is paused"; }
};
} // namespace mca
} // namespace llvm
#endif // LLVM_MCA_STAGES_STAGE_H
