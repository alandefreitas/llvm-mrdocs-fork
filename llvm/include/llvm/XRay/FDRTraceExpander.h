//===- FDRTraceExpander.h - XRay FDR Mode Log Expander --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// We define an FDR record visitor which can re-constitute XRayRecord instances
// from a sequence of FDR mode records in arrival order into a collection.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_FDRTRACEEXPANDER_H
#define LLVM_XRAY_FDRTRACEEXPANDER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/XRay/FDRRecords.h"
#include "llvm/XRay/XRayRecord.h"

namespace llvm::xray {

/// RecordVisitor that reconstitutes XRayRecord instances from FDR mode records.
///
/// Processes a sequence of FDR mode records in arrival order and emits complete
/// XRayRecord instances through a type-erased callback.
class LLVM_ABI TraceExpander : public RecordVisitor {
  // Type-erased callback for handling individual XRayRecord instances.
  function_ref<void(const XRayRecord &)> C;
  int32_t PID = 0;
  int32_t TID = 0;
  uint64_t BaseTSC = 0;
  XRayRecord CurrentRecord{0, 0, RecordTypes::ENTER, 0, 0, 0, 0, {}, {}};
  uint16_t CPUId = 0;
  uint16_t LogVersion = 0;
  bool BuildingRecord = false;
  bool IgnoringRecords = false;

  void resetCurrentRecord();

public:
  /// Construct a TraceExpander that invokes \p F for each expanded record.
  /// \param F Callback invoked with each reconstituted XRayRecord.
  /// \param L FDR log version used when interpreting records.
  explicit TraceExpander(function_ref<void(const XRayRecord &)> F, uint16_t L)
      : C(std::move(F)), LogVersion(L) {}

  /// Expand a buffer-extents record into the current reconstituted state.
  /// \param R Buffer extents record being visited.
  /// \return Success, or an error if expansion failed.
  Error visit(BufferExtents &R) override;
  /// Expand a wall-clock record into the current reconstituted state.
  /// \param R Wall-clock record being visited.
  /// \return Success, or an error if expansion failed.
  Error visit(WallclockRecord &R) override;
  /// Expand a new-CPU-ID record into the current reconstituted state.
  /// \param R New CPU ID record being visited.
  /// \return Success, or an error if expansion failed.
  Error visit(NewCPUIDRecord &R) override;
  /// Expand a TSC-wrap record into the current reconstituted state.
  /// \param R TSC wrap record being visited.
  /// \return Success, or an error if expansion failed.
  Error visit(TSCWrapRecord &R) override;
  /// Expand a custom-event record into an XRayRecord.
  /// \param R Custom event record being visited.
  /// \return Success, or an error if expansion failed.
  Error visit(CustomEventRecord &R) override;
  /// Expand a call-argument record into the current reconstituted state.
  /// \param R Call argument record being visited.
  /// \return Success, or an error if expansion failed.
  Error visit(CallArgRecord &R) override;
  /// Expand a PID record into the current reconstituted state.
  /// \param R PID record being visited.
  /// \return Success, or an error if expansion failed.
  Error visit(PIDRecord &R) override;
  /// Expand a new-buffer record into the current reconstituted state.
  /// \param R New buffer record being visited.
  /// \return Success, or an error if expansion failed.
  Error visit(NewBufferRecord &R) override;
  /// Expand an end-of-buffer record into the current reconstituted state.
  /// \param R End-of-buffer record being visited.
  /// \return Success, or an error if expansion failed.
  Error visit(EndBufferRecord &R) override;
  /// Expand a function record into an XRayRecord.
  /// \param R Function record being visited.
  /// \return Success, or an error if expansion failed.
  Error visit(FunctionRecord &R) override;
  /// Expand a v5 custom-event record into an XRayRecord.
  /// \param R V5 custom event record being visited.
  /// \return Success, or an error if expansion failed.
  Error visit(CustomEventRecordV5 &R) override;
  /// Expand a typed-event record into an XRayRecord.
  /// \param R Typed event record being visited.
  /// \return Success, or an error if expansion failed.
  Error visit(TypedEventRecord &R) override;

  /// Flush any partially built record after all FDR records have been visited.
  ///
  /// Must be called after all the records have been processed, to handle the
  /// most recent record generated.
  /// \return Success, or an error if flushing the final record failed.
  Error flush();
};

} // namespace llvm::xray

#endif // LLVM_XRAY_FDRTRACEEXPANDER_H
