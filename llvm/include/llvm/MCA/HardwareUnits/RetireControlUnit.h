//===---------------------- RetireControlUnit.h -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file simulates the hardware responsible for retiring instructions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_HARDWAREUNITS_RETIRECONTROLUNIT_H
#define LLVM_MCA_HARDWAREUNITS_RETIRECONTROLUNIT_H

#include "llvm/MC/MCSchedule.h"
#include "llvm/MCA/HardwareUnits/HardwareUnit.h"
#include "llvm/MCA/Instruction.h"
#include <vector>

namespace llvm {
namespace mca {

/// Tracks which instructions are in-flight in the out-of-order backend.
///
/// This class checks on every cycle if/which instructions can be retired.
/// Instructions are retired in program order.
/// In the event of an instruction being retired, the pipeline that owns
/// this RetireControlUnit (RCU) gets notified.
///
/// On instruction retired, register updates are all architecturally
/// committed, and any physical registers previously allocated for the
/// retired instruction are freed.
struct RetireControlUnit : public HardwareUnit {
  /// A token representing a dispatched instruction in the RCU queue.
  ///
  /// A RUToken is created by the RCU for every instruction dispatched to the
  /// schedulers.  These "tokens" are managed by the RCU in its token Queue.
  ///
  /// On every cycle ('cycleEvent'), the RCU iterates through the token queue
  /// looking for any token with its 'Executed' flag set.  If a token has that
  /// flag set, then the instruction has reached the write-back stage and will
  /// be retired by the RCU.
  ///
  /// 'NumSlots' represents the number of entries consumed by the instruction in
  /// the reorder buffer. Those entries will become available again once the
  /// instruction is retired.
  ///
  /// Note that the size of the reorder buffer is defined by the scheduling
  /// model via field 'NumMicroOpBufferSize'.
  struct RUToken {
    /// Instruction reference associated with this token.
    InstRef IR;
    /// Number of reorder buffer slots reserved for this instruction.
    unsigned NumSlots;
    /// True if the instruction is past the write-back stage.
    bool Executed;
  };

private:
  unsigned NextAvailableSlotIdx;
  unsigned CurrentInstructionSlotIdx;
  unsigned NumROBEntries;
  unsigned AvailableEntries;
  unsigned MaxRetirePerCycle; // 0 means no limit.
  std::vector<RUToken> Queue;

  unsigned normalizeQuantity(unsigned Quantity) const {
    // Some instructions may declare a number of uOps which exceeds the size
    // of the reorder buffer. To avoid problems, cap the amount of slots to
    // the size of the reorder buffer.
    Quantity = std::min(Quantity, NumROBEntries);

    // Further normalize the number of micro opcodes for instructions that
    // declare zero opcodes. This should match the behavior of method
    // reserveSlot().
    return std::max(Quantity, 1U);
  }

  unsigned computeNextSlotIdx() const;

public:
  /// Construct a retire control unit from the scheduling model.
  /// @param SM Machine scheduling model that defines reorder buffer size.
  LLVM_ABI RetireControlUnit(const MCSchedModel &SM);

  /// Returns true if the reorder buffer currently holds no instructions.
  /// @return True if the reorder buffer is empty.
  bool isEmpty() const { return AvailableEntries == NumROBEntries; }

  /// Returns true if at least Quantity reorder buffer slots are available.
  /// @param Quantity Number of slots requested; normalized to at least one.
  /// @return True if enough reorder buffer slots are available.
  bool isAvailable(unsigned Quantity = 1) const {
    return AvailableEntries >= normalizeQuantity(Quantity);
  }

  /// Returns the maximum number of instructions that may retire per cycle.
  /// @return Maximum retire count per cycle, or 0 if unlimited.
  unsigned getMaxRetirePerCycle() const { return MaxRetirePerCycle; }

  /// Reserves reorder buffer slots and returns a new token identifier.
  /// @param IS Instruction being dispatched to the schedulers.
  /// @return Token identifier for the dispatched instruction.
  LLVM_ABI unsigned dispatch(const InstRef &IS);

  /// Returns the current token from the RCU's circular token queue.
  /// @return Reference to the current RUToken.
  LLVM_ABI const RUToken &getCurrentToken() const;

  /// Returns the next token after the current one without consuming it.
  /// @return Reference to the next RUToken.
  LLVM_ABI const RUToken &peekNextToken() const;

  /// Advances the pointer to the next token in the circular token queue.
  LLVM_ABI void consumeCurrentToken();

  /// Marks the RCU token as executed after write-back completes.
  /// @param TokenID Identifier of the token to update.
  LLVM_ABI void onInstructionExecuted(unsigned TokenID);

#ifndef NDEBUG
  /// Dumps the retire control unit state for debugging.
  void dump() const;
#endif

  /// Token ID assigned to instructions that are not handled by the RCU.
  static const unsigned UnhandledTokenID = ~0U;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_HARDWAREUNITS_RETIRECONTROLUNIT_H
