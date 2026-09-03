//===- BlockPrinter.h - FDR Block Pretty Printer -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An implementation of the RecordVisitor which formats a block of records for
// easier human consumption.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_BLOCKPRINTER_H
#define LLVM_XRAY_BLOCKPRINTER_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/FDRRecords.h"
#include "llvm/XRay/RecordPrinter.h"

namespace llvm::xray {

/// RecordVisitor that pretty-prints an FDR block of records for humans.
///
/// Formats a sequence of FDR records as a readable block, using a RecordPrinter
/// for individual record text and inserting section markers between record
/// kinds.
class LLVM_ABI BlockPrinter : public RecordVisitor {
  enum class State {
    Start,
    Preamble,
    Metadata,
    Function,
    Arg,
    CustomEvent,
    End,
  };

  raw_ostream &OS;
  RecordPrinter &RP;
  State CurrentState = State::Start;

public:
  /// Construct a block printer writing to \p O via record printer \p P.
  /// \param O Output stream that receives formatted block text.
  /// \param P Printer used to format individual FDR records.
  explicit BlockPrinter(raw_ostream &O, RecordPrinter &P) : OS(O), RP(P) {}

  /// Visit a buffer-extents record and open a new formatted block.
  /// \param R Buffer extents record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(BufferExtents &R) override;
  /// Visit a wall-clock record and print it as part of the preamble.
  /// \param R Wall-clock record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(WallclockRecord &R) override;
  /// Visit a new-CPU-ID record and print it as metadata in the block body.
  /// \param R New CPU ID record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(NewCPUIDRecord &R) override;
  /// Visit a TSC-wrap record and print it as metadata in the block body.
  /// \param R TSC wrap record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(TSCWrapRecord &R) override;
  /// Visit a custom-event record and print it like a function event.
  /// \param R Custom event record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(CustomEventRecord &R) override;
  /// Visit a call-argument record and print it after its function record.
  /// \param R Call argument record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(CallArgRecord &R) override;
  /// Visit a PID record and print it as part of the preamble.
  /// \param R PID record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(PIDRecord &R) override;
  /// Visit a new-buffer record and print the preamble section header.
  /// \param R New buffer record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(NewBufferRecord &R) override;
  /// Visit an end-of-buffer record and mark the formatted block as finished.
  /// \param R End-of-buffer record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(EndBufferRecord &R) override;
  /// Visit a function record and print it as a function call line.
  /// \param R Function record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(FunctionRecord &R) override;
  /// Visit a v5 custom-event record and print it like a function event.
  /// \param R V5 custom event record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(CustomEventRecordV5 &R) override;
  /// Visit a typed-event record and print it like a function event.
  /// \param R Typed event record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(TypedEventRecord &R) override;

  /// Reset the printer state so the next visit starts a new block.
  void reset() { CurrentState = State::Start; }
};

} // namespace llvm::xray

#endif // LLVM_XRAY_BLOCKPRINTER_H
