//===---------------------- MicroOpQueueStage.h -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a stage that implements a queue of micro opcodes.
/// It can be used to simulate a hardware micro-op queue that serves opcodes to
/// the out of order backend.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_STAGES_MICROOPQUEUESTAGE_H
#define LLVM_MCA_STAGES_MICROOPQUEUESTAGE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MCA/Stages/Stage.h"

namespace llvm {
namespace mca {

/// A stage that simulates a queue of instruction opcodes.
class LLVM_ABI MicroOpQueueStage : public Stage {
  SmallVector<InstRef, 8> Buffer;
  unsigned NextAvailableSlotIdx;
  unsigned CurrentInstructionSlotIdx;

  // Limits the number of instructions that can be written to this buffer every
  // cycle. A value of zero means that there is no limit to the instruction
  // throughput in input.
  const unsigned MaxIPC;
  unsigned CurrentIPC;

  // Number of entries that are available during this cycle.
  unsigned AvailableEntries;

  // True if instructions dispatched to this stage don't need to wait for the
  // next cycle before moving to the next stage.
  // False if this buffer acts as a one cycle delay in the execution pipeline.
  bool IsZeroLatencyStage;

  MicroOpQueueStage(const MicroOpQueueStage &Other) = delete;
  MicroOpQueueStage &operator=(const MicroOpQueueStage &Other) = delete;

  // By default, an instruction consumes a number of buffer entries equal to its
  // number of micro opcodes (see field `InstrDesc::NumMicroOpcodes`).  The
  // number of entries consumed by an instruction is normalized to the
  // minimum value between NumMicroOpcodes and the buffer size. This is to avoid
  // problems with (microcoded) instructions that generate a number of micro
  // opcodes than doesn't fit in the buffer.
  unsigned getNormalizedOpcodes(const InstRef &IR) const {
    unsigned NormalizedOpcodes =
        std::min(static_cast<unsigned>(Buffer.size()),
                 IR.getInstruction()->getDesc().NumMicroOps);
    return NormalizedOpcodes ? NormalizedOpcodes : 1U;
  }

  Error moveInstructions();

public:
  /// Construct a micro-op queue stage with the given capacity and throughput.
  /// \param Size Number of buffer entries in the micro-op queue.
  /// \param IPC Maximum instructions accepted per cycle; zero means unlimited.
  /// \param ZeroLatencyStage If true, instructions can leave in the same cycle;
  ///        if false, the queue adds a one-cycle delay.
  MicroOpQueueStage(unsigned Size, unsigned IPC = 0,
                    bool ZeroLatencyStage = true);

  /// Returns true if \p IR can be enqueued during this cycle.
  /// \param IR Instruction to check for queue availability.
  /// \return True if \p IR can be enqueued during this cycle.
  bool isAvailable(const InstRef &IR) const override {
    if (MaxIPC && CurrentIPC == MaxIPC)
      return false;
    unsigned NormalizedOpcodes = getNormalizedOpcodes(IR);
    if (NormalizedOpcodes > AvailableEntries)
      return false;
    return true;
  }

  /// Returns true if the queue still holds instructions awaiting completion.
  /// \return True if the queue still holds instructions awaiting completion.
  bool hasWorkToComplete() const override {
    return AvailableEntries != Buffer.size();
  }

  /// Enqueues \p IR into the micro-op buffer if space and IPC allow.
  /// \param IR Instruction to place into the queue.
  /// \return Success, or an error if enqueueing or handing off fails.
  Error execute(InstRef &IR) override;

  /// Resets per-cycle IPC tracking at the beginning of a cycle.
  /// \return Success after resetting per-cycle IPC tracking.
  Error cycleStart() override;

  /// Advances queued instructions and frees buffer entries at cycle end.
  /// \return Success, or an error if advancing queued instructions fails.
  Error cycleEnd() override;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_STAGES_MICROOPQUEUESTAGE_H
