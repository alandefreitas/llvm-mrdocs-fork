//===- BlockVerifier.h - FDR Block Verifier -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An implementation of the RecordVisitor which verifies a sequence of records
// associated with a block, following the FDR mode log format's specifications.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_BLOCKVERIFIER_H
#define LLVM_XRAY_BLOCKVERIFIER_H

#include "llvm/Support/Compiler.h"
#include "llvm/XRay/FDRRecords.h"

namespace llvm::xray {

/// RecordVisitor that verifies FDR block record sequences.
///
/// Checks that records in a block follow the FDR mode log format's
/// specifications by enforcing valid state transitions between record kinds.
class LLVM_ABI BlockVerifier : public RecordVisitor {
public:
  /// States in the FDR block record transition automaton.
  ///
  /// We force State elements to be size_t, to be used as indices for containers.
  enum class State : std::size_t {
    /// Initial state before any record has been visited.
    Unknown,
    /// A BufferExtents metadata record has been seen.
    BufferExtents,
    /// A NewBuffer metadata record has been seen.
    NewBuffer,
    /// A WallClockTime metadata record has been seen.
    WallClockTime,
    /// A PID entry metadata record has been seen.
    PIDEntry,
    /// A NewCPUId metadata record has been seen.
    NewCPUId,
    /// A TSCWrap metadata record has been seen.
    TSCWrap,
    /// A CustomEvent metadata record has been seen.
    CustomEvent,
    /// A TypedEvent metadata record has been seen.
    TypedEvent,
    /// A Function record has been seen.
    Function,
    /// A CallArg metadata record has been seen.
    CallArg,
    /// An EndOfBuffer metadata record has been seen.
    EndOfBuffer,
    /// One-past-last sentinel; not a real record state.
    StateMax,
  };

private:
  // We keep track of the current record seen by the verifier.
  State CurrentRecord = State::Unknown;

  // Transitions the current record to the new record, records an error on
  // invalid transitions.
  Error transition(State To);

public:
  /// Visit a buffer-extents record and advance the verifier state.
  /// \param R Buffer extents record being visited.
  /// \return Success, or an error if the state transition is invalid.
  Error visit(BufferExtents &R) override;
  /// Visit a wall-clock record and advance the verifier state.
  /// \param R Wall-clock record being visited.
  /// \return Success, or an error if the state transition is invalid.
  Error visit(WallclockRecord &R) override;
  /// Visit a new-CPU-ID record and advance the verifier state.
  /// \param R New CPU ID record being visited.
  /// \return Success, or an error if the state transition is invalid.
  Error visit(NewCPUIDRecord &R) override;
  /// Visit a TSC-wrap record and advance the verifier state.
  /// \param R TSC wrap record being visited.
  /// \return Success, or an error if the state transition is invalid.
  Error visit(TSCWrapRecord &R) override;
  /// Visit a custom-event record and advance the verifier state.
  /// \param R Custom event record being visited.
  /// \return Success, or an error if the state transition is invalid.
  Error visit(CustomEventRecord &R) override;
  /// Visit a call-argument record and advance the verifier state.
  /// \param R Call argument record being visited.
  /// \return Success, or an error if the state transition is invalid.
  Error visit(CallArgRecord &R) override;
  /// Visit a PID record and advance the verifier state.
  /// \param R PID record being visited.
  /// \return Success, or an error if the state transition is invalid.
  Error visit(PIDRecord &R) override;
  /// Visit a new-buffer record and advance the verifier state.
  /// \param R New buffer record being visited.
  /// \return Success, or an error if the state transition is invalid.
  Error visit(NewBufferRecord &R) override;
  /// Visit an end-of-buffer record and advance the verifier state.
  /// \param R End-of-buffer record being visited.
  /// \return Success, or an error if the state transition is invalid.
  Error visit(EndBufferRecord &R) override;
  /// Visit a function record and advance the verifier state.
  /// \param R Function record being visited.
  /// \return Success, or an error if the state transition is invalid.
  Error visit(FunctionRecord &R) override;
  /// Visit a v5 custom-event record and advance the verifier state.
  /// \param R V5 custom event record being visited.
  /// \return Success, or an error if the state transition is invalid.
  Error visit(CustomEventRecordV5 &R) override;
  /// Visit a typed-event record and advance the verifier state.
  /// \param R Typed event record being visited.
  /// \return Success, or an error if the state transition is invalid.
  Error visit(TypedEventRecord &R) override;

  /// Check that the visited sequence ended in a valid terminal state.
  /// \return Success, or an error if the sequence did not end in a valid
  /// terminal state.
  Error verify();
  /// Reset the verifier to the initial Unknown state.
  void reset();
};

} // namespace llvm::xray

#endif // LLVM_XRAY_BLOCKVERIFIER_H
