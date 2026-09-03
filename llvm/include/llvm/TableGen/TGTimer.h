//===- llvm/TableGen/TGTimer.h - Class for TableGen Timer -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TableGen timer class. It's a thin wrapper around timer
// support in llvm/Support/Timer.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_TGTIMER_H
#define LLVM_TABLEGEN_TGTIMER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Timer.h"
#include <memory>

namespace llvm {

/// Thin wrapper around Timer support for TableGen backends.
class TGTimer {
private:
  std::unique_ptr<TimerGroup> TimingGroup;
  std::unique_ptr<Timer> LastTimer;
  bool BackendTimer = false; // Is last timer special backend overall timer?

public:
  /// Construct a TableGen timer with timing inactive.
  TGTimer() = default;
  /// Destroy this TableGen timer.
  ~TGTimer() = default;

  /// Start phase timing; called if the --time-phases option is specified.
  void startPhaseTiming() {
    TimingGroup =
        std::make_unique<TimerGroup>("TableGen", "TableGen Phase Timing");
  }

  /// Start timing a phase. Automatically stops any previous phase timer.
  ///
  /// \param Name Name of the phase being timed.
  LLVM_ABI void startTimer(StringRef Name);

  /// Stop timing a phase.
  LLVM_ABI void stopTimer();

  /// Start timing the overall backend. If the backend itself starts a timer,
  /// then this timer is cleared.
  ///
  /// \param Name Name of the backend being timed.
  LLVM_ABI void startBackendTimer(StringRef Name);

  /// Stop timing the overall backend.
  LLVM_ABI void stopBackendTimer();

  /// Stop phase timing and print the report.
  void stopPhaseTiming() { TimingGroup.reset(); }
};

} // end namespace llvm

#endif // LLVM_TABLEGEN_TGTIMER_H
