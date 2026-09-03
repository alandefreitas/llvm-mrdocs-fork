//===- FDRTraceWriter.h - XRay FDR Trace Writer -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Test a utility that can write out XRay FDR Mode formatted trace files.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_FDRTRACEWRITER_H
#define LLVM_XRAY_FDRTRACEWRITER_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/FDRRecords.h"
#include "llvm/XRay/XRayRecord.h"

namespace llvm::xray {

/// Writer that hand-crafts XRay Flight Data Recorder (FDR) mode log files.
///
/// This is used primarily for testing, generating sequences of FDR records that
/// can be read/processed. It can also be used to generate various kinds of
/// execution traces without using the XRay runtime. Note that this writer does
/// not do any validation, but uses the types of records defined in the
/// FDRRecords.h file.
class LLVM_ABI FDRTraceWriter : public RecordVisitor {
public:
  /// Construct an FDRTraceWriter that writes to \p O with file header \p H.
  /// \param O Output stream that receives FDR-encoded bytes.
  /// \param H XRay file header written at the start of the stream.
  explicit FDRTraceWriter(raw_ostream &O, const XRayFileHeader &H);
  /// Destroy the FDRTraceWriter.
  ~FDRTraceWriter() override;

  /// Write a buffer-extents metadata record to the output stream.
  /// \param R Buffer extents record to serialize.
  /// \return Success, or an error if writing failed.
  Error visit(BufferExtents &R) override;
  /// Write a wall-clock record to the output stream.
  /// \param R Wall-clock record to serialize.
  /// \return Success, or an error if writing failed.
  Error visit(WallclockRecord &R) override;
  /// Write a new-CPU-ID record to the output stream.
  /// \param R New CPU ID record to serialize.
  /// \return Success, or an error if writing failed.
  Error visit(NewCPUIDRecord &R) override;
  /// Write a TSC-wrap record to the output stream.
  /// \param R TSC wrap record to serialize.
  /// \return Success, or an error if writing failed.
  Error visit(TSCWrapRecord &R) override;
  /// Write a custom-event record to the output stream.
  /// \param R Custom event record to serialize.
  /// \return Success, or an error if writing failed.
  Error visit(CustomEventRecord &R) override;
  /// Write a call-argument record to the output stream.
  /// \param R Call argument record to serialize.
  /// \return Success, or an error if writing failed.
  Error visit(CallArgRecord &R) override;
  /// Write a PID record to the output stream.
  /// \param R PID record to serialize.
  /// \return Success, or an error if writing failed.
  Error visit(PIDRecord &R) override;
  /// Write a new-buffer record to the output stream.
  /// \param R New buffer record to serialize.
  /// \return Success, or an error if writing failed.
  Error visit(NewBufferRecord &R) override;
  /// Write an end-of-buffer record to the output stream.
  /// \param R End-of-buffer record to serialize.
  /// \return Success, or an error if writing failed.
  Error visit(EndBufferRecord &R) override;
  /// Write a function record to the output stream.
  /// \param R Function record to serialize.
  /// \return Success, or an error if writing failed.
  Error visit(FunctionRecord &R) override;
  /// Write a v5 custom-event record to the output stream.
  /// \param R V5 custom event record to serialize.
  /// \return Success, or an error if writing failed.
  Error visit(CustomEventRecordV5 &R) override;
  /// Write a typed-event record to the output stream.
  /// \param R Typed event record to serialize.
  /// \return Success, or an error if writing failed.
  Error visit(TypedEventRecord &R) override;

private:
  support::endian::Writer OS;
};

} // namespace llvm::xray

#endif // LLVM_XRAY_FDRTRACEWRITER_H
