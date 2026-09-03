//=- llvm/CodeGen/ScoreboardHazardRecognizer.h - Schedule Support -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ScoreboardHazardRecognizer class, which
// encapsulates hazard-avoidance heuristics for scheduling, based on the
// scheduling itineraries specified for the target.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCOREBOARDHAZARDRECOGNIZER_H
#define LLVM_CODEGEN_SCOREBOARDHAZARDRECOGNIZER_H

#include "llvm/ADT/bit.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/MC/MCInstrItineraries.h"
#include <cassert>
#include <cstddef>
#include <cstring>

namespace llvm {

class ScheduleDAG;
class SUnit;

/// Hazard recognizer that tracks function-unit usage with a scoreboard.
///
/// Encapsulates hazard-avoidance heuristics for scheduling based on the
/// target's instruction itineraries, maintaining reserved and required
/// scoreboards of function-unit masks across cycles.
class LLVM_ABI ScoreboardHazardRecognizer : public ScheduleHazardRecognizer {
  // Scoreboard to track function unit usage. Scoreboard[0] is a
  // mask of the FUs in use in the cycle currently being
  // schedule. Scoreboard[1] is a mask for the next cycle. The
  // Scoreboard is used as a circular buffer with the current cycle
  // indicated by Head.
  //
  // Scoreboard always counts cycles in forward execution order. If used by a
  // bottom-up scheduler, then the scoreboard cycles are the inverse of the
  // scheduler's cycles.
  class Scoreboard {
    InstrStage::FuncUnits *Data = nullptr;

    // The maximum number of cycles monitored by the Scoreboard. This
    // value is determined based on the target itineraries to ensure
    // that all hazards can be tracked.
    size_t Depth = 0;

    // Indices into the Scoreboard that represent the current cycle.
    size_t Head = 0;

  public:
    Scoreboard() = default;
    Scoreboard &operator=(const Scoreboard &other) = delete;
    Scoreboard(const Scoreboard &other) = delete;
    ~Scoreboard() {
      delete[] Data;
    }

    size_t getDepth() const { return Depth; }

    InstrStage::FuncUnits& operator[](size_t idx) const {
      // Depth is expected to be a power-of-2.
      assert(llvm::has_single_bit(Depth) &&
             "Scoreboard was not initialized properly!");

      return Data[(Head + idx) & (Depth-1)];
    }

    void reset(size_t d = 1) {
      if (!Data) {
        Depth = d;
        Data = new InstrStage::FuncUnits[Depth];
      }

      memset(Data, 0, Depth * sizeof(Data[0]));
      Head = 0;
    }

    void advance() {
      Head = (Head + 1) & (Depth-1);
    }

    void recede() {
      Head = (Head - 1) & (Depth-1);
    }

    // Print the scoreboard.
    LLVM_ABI void dump() const;
  };

  // Support for tracing ScoreboardHazardRecognizer as a component within
  // another module.
  const char *DebugType;

  // Itinerary data for the target.
  const InstrItineraryData *ItinData;

  const ScheduleDAG *DAG;

  /// IssueWidth - Max issue per cycle. 0=Unknown.
  unsigned IssueWidth = 0;

  /// IssueCount - Count instructions issued in this cycle.
  unsigned IssueCount = 0;

  Scoreboard ReservedScoreboard;
  Scoreboard RequiredScoreboard;

public:
  /// Construct a scoreboard hazard recognizer for the given itinerary and DAG.
  ///
  /// \param II Target instruction itinerary data used to size and update the
  ///   scoreboards.
  /// \param DAG Schedule DAG that owns the scheduling units being recognized.
  /// \param ParentDebugType Optional debug-type string for tracing this
  ///   recognizer as a component of another module.
  ScoreboardHazardRecognizer(const InstrItineraryData *II,
                             const ScheduleDAG *DAG,
                             const char *ParentDebugType = "");

  /// atIssueLimit - Return true if no more instructions may be issued in this
  /// cycle.
  /// \return True if no more instructions may be issued in this cycle.
  bool atIssueLimit() const override;

  /// Return the hazard type of emitting \p SU after \p Stalls cycles.
  ///
  /// \param SU Scheduling unit being considered for issue.
  /// \param Stalls Cycle offset at which \p SU will be scheduled; negative for
  ///   bottom-up scheduling.
  /// \return The HazardType for emitting \p SU after \p Stalls cycles.
  HazardType getHazardType(SUnit *SU, int Stalls) override;
  /// Reset the scoreboards and issue state for a new block of instructions.
  void Reset() override;
  /// Update the scoreboards to reflect that \p SU has been emitted.
  /// \param SU Scheduling unit that was emitted.
  void EmitInstruction(SUnit *SU) override;
  /// Advance the scoreboard to the next cycle for top-down scheduling.
  void AdvanceCycle() override;
  /// Recede the scoreboard to the previous cycle for bottom-up scheduling.
  void RecedeCycle() override;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SCOREBOARDHAZARDRECOGNIZER_H
