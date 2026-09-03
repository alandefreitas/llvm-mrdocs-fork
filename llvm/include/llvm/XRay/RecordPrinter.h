//===- RecordPrinter.h - FDR Record Printer -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An implementation of the RecordVisitor which prints an individual record's
// data in an adhoc format, suitable for human inspection.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_RECORDPRINTER_H
#define LLVM_XRAY_RECORDPRINTER_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/FDRRecords.h"

namespace llvm::xray {

/// RecordVisitor that prints an individual FDR record for human inspection.
///
/// Formats a single FDR record's data in an adhoc text form, writing to a
/// raw_ostream and optionally separating successive prints with a delimiter.
class LLVM_ABI RecordPrinter : public RecordVisitor {
  raw_ostream &OS;
  std::string Delim;

public:
  /// Construct a record printer writing to \p O with delimiter \p D.
  /// \param O Output stream that receives formatted record text.
  /// \param D Delimiter appended after each printed record.
  explicit RecordPrinter(raw_ostream &O, std::string D)
      : OS(O), Delim(std::move(D)) {}

  /// Construct a record printer writing to \p O with an empty delimiter.
  /// \param O Output stream that receives formatted record text.
  explicit RecordPrinter(raw_ostream &O) : RecordPrinter(O, ""){};

  /// Print a buffer-extents record to the output stream.
  /// \param R Buffer extents record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(BufferExtents &R) override;
  /// Print a wall-clock record to the output stream.
  /// \param R Wall-clock record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(WallclockRecord &R) override;
  /// Print a new-CPU-ID record to the output stream.
  /// \param R New CPU ID record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(NewCPUIDRecord &R) override;
  /// Print a TSC-wrap record to the output stream.
  /// \param R TSC wrap record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(TSCWrapRecord &R) override;
  /// Print a custom-event record to the output stream.
  /// \param R Custom event record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(CustomEventRecord &R) override;
  /// Print a call-argument record to the output stream.
  /// \param R Call argument record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(CallArgRecord &R) override;
  /// Print a PID record to the output stream.
  /// \param R PID record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(PIDRecord &R) override;
  /// Print a new-buffer record to the output stream.
  /// \param R New buffer record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(NewBufferRecord &R) override;
  /// Print an end-of-buffer record to the output stream.
  /// \param R End-of-buffer record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(EndBufferRecord &R) override;
  /// Print a function record to the output stream.
  /// \param R Function record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(FunctionRecord &R) override;
  /// Print a v5 custom-event record to the output stream.
  /// \param R V5 custom event record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(CustomEventRecordV5 &R) override;
  /// Print a typed-event record to the output stream.
  /// \param R Typed event record being visited.
  /// \return Success, or an error if printing failed.
  Error visit(TypedEventRecord &R) override;
};

} // namespace llvm::xray

#endif // LLVM_XRAY_RECORDPRINTER_H
