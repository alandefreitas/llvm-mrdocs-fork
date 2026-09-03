//===- BlockIndexer.h - FDR Block Indexing Visitor ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An implementation of the RecordVisitor which generates a mapping between a
// thread and a range of records representing a block.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_XRAY_BLOCKINDEXER_H
#define LLVM_XRAY_BLOCKINDEXER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Compiler.h"
#include "llvm/XRay/FDRRecords.h"
#include <cstdint>
#include <vector>

namespace llvm::xray {

/// RecordVisitor that groups FDR records into per-process/thread blocks.
///
/// The BlockIndexer will gather all related records associated with a
/// process+thread and group them by 'Block'.
class LLVM_ABI BlockIndexer : public RecordVisitor {
public:
  /// One contiguous FDR block for a process and thread.
  struct Block {
    /// Process identifier for this block.
    uint64_t ProcessID;
    /// Thread identifier for this block.
    int32_t ThreadID;
    /// Wall-clock timestamp record for this block, if present.
    WallclockRecord *WallclockTime;
    /// Records belonging to this block, in visitation order.
    std::vector<Record *> Records;
  };

  /// Map from process+thread to the sequence of blocks seen for that pair.
  using Index = DenseMap<std::pair<uint64_t, int32_t>, std::vector<Block>>;

private:
  Index &Indices;

  Block CurrentBlock{0, 0, nullptr, {}};

public:
  /// Construct an indexer that appends completed blocks into \p I.
  /// \param I Index that receives flushed blocks.
  explicit BlockIndexer(Index &I) : Indices(I) {}

  /// Visit a buffer-extents metadata record (no-op for indexing).
  /// \param R Buffer extents record being visited.
  /// \return Success, or an error if indexing failed.
  Error visit(BufferExtents &R) override;
  /// Visit a wall-clock record and record it as the block's timestamp.
  /// \param R Wall-clock record being visited.
  /// \return Success, or an error if indexing failed.
  Error visit(WallclockRecord &R) override;
  /// Visit a new-CPU-ID record and append it to the current block.
  /// \param R New CPU ID record being visited.
  /// \return Success, or an error if indexing failed.
  Error visit(NewCPUIDRecord &R) override;
  /// Visit a TSC-wrap record and append it to the current block.
  /// \param R TSC wrap record being visited.
  /// \return Success, or an error if indexing failed.
  Error visit(TSCWrapRecord &R) override;
  /// Visit a custom-event record and append it to the current block.
  /// \param R Custom event record being visited.
  /// \return Success, or an error if indexing failed.
  Error visit(CustomEventRecord &R) override;
  /// Visit a call-argument record and append it to the current block.
  /// \param R Call argument record being visited.
  /// \return Success, or an error if indexing failed.
  Error visit(CallArgRecord &R) override;
  /// Visit a PID record and set the current block's process ID.
  /// \param R PID record being visited.
  /// \return Success, or an error if indexing failed.
  Error visit(PIDRecord &R) override;
  /// Visit a new-buffer record, flushing any prior block first.
  /// \param R New buffer record being visited.
  /// \return Success, or an error if indexing failed.
  Error visit(NewBufferRecord &R) override;
  /// Visit an end-of-buffer record and append it to the current block.
  /// \param R End-of-buffer record being visited.
  /// \return Success, or an error if indexing failed.
  Error visit(EndBufferRecord &R) override;
  /// Visit a function record and append it to the current block.
  /// \param R Function record being visited.
  /// \return Success, or an error if indexing failed.
  Error visit(FunctionRecord &R) override;
  /// Visit a v5 custom-event record and append it to the current block.
  /// \param R V5 custom event record being visited.
  /// \return Success, or an error if indexing failed.
  Error visit(CustomEventRecordV5 &R) override;
  /// Visit a typed-event record and append it to the current block.
  /// \param R Typed event record being visited.
  /// \return Success, or an error if indexing failed.
  Error visit(TypedEventRecord &R) override;

  /// Flush the current block into the index and reset visitor state.
  ///
  /// The flush() function will clear out the current state of the visitor, to
  /// allow for explicitly flushing a block's records to the currently
  /// recognized thread and process combination.
  /// \return Success, or an error if flushing failed.
  Error flush();
};

} // namespace llvm::xray

#endif // LLVM_XRAY_BLOCKINDEXER_H
