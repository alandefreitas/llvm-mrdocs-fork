//===---------------- IncrementalSourceMgr.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file contains IncrementalSourceMgr, an implementation of SourceMgr
/// that allows users to add new instructions incrementally / dynamically.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_INCREMENTALSOURCEMGR_H
#define LLVM_MCA_INCREMENTALSOURCEMGR_H

#include "llvm/MCA/SourceMgr.h"
#include "llvm/Support/Compiler.h"
#include <deque>

namespace llvm {
namespace mca {

/// An implementation of \a SourceMgr that allows users to add new instructions
/// incrementally / dynamically.
///
/// Note that this SourceMgr takes ownership of all \a mca::Instruction.
class LLVM_ABI IncrementalSourceMgr : public SourceMgr {
  /// Owner of all mca::Instruction instances. Note that we use std::deque here
  /// to have a better throughput, in comparison to std::vector or
  /// llvm::SmallVector, as they usually pay a higher re-allocation cost when
  /// there is a large number of instructions.
  std::deque<UniqueInst> InstStorage;

  /// Instructions that are ready to be used. Each of them is a pointer of an
  /// \a UniqueInst inside InstStorage.
  std::deque<Instruction *> Staging;

  /// Current instruction index.
  unsigned TotalCounter = 0U;

  /// End-of-stream flag.
  bool EOS = false;

  /// Called when an instruction is no longer needed.
  using InstFreedCallback = std::function<void(Instruction *)>;
  InstFreedCallback InstFreedCB;

public:
  /// Construct an empty incremental source manager.
  IncrementalSourceMgr() = default;

  /// Incremental source managers are not copy-assignable.
  /// \param IMS Unused; copy assignment is deleted.
  IncrementalSourceMgr &operator=(const IncrementalSourceMgr &IMS) = delete;
  /// Incremental source managers are not copyable.
  /// \param IMS Unused; copy construction is deleted.
  IncrementalSourceMgr(const IncrementalSourceMgr &IMS) = delete;

  /// Clear all stored and staged instructions and reset stream state.
  void clear();

  /// Set a callback that is invoked when a mca::Instruction is
  /// no longer needed. This is usually used for recycling the
  /// instruction.
  /// \param CB Callback invoked with the freed instruction.
  void setOnInstFreedCallback(InstFreedCallback CB) { InstFreedCB = CB; }

  /// Not applicable for incremental source managers; triggers an unreachable.
  /// \return Never returns; this override always aborts.
  ArrayRef<UniqueInst> getInstructions() const override {
    llvm_unreachable("Not applicable");
  }

  /// Whether there is any \a SourceRef to inspect / peek next.
  /// \return True if at least one staged instruction is available to peek.
  bool hasNext() const override { return !Staging.empty(); }
  /// Whether the instruction stream has ended.
  /// \return True if \a endOfStream has been called.
  bool isEnd() const override { return EOS; }

  /// The next \a SourceRef.
  /// \return The next staged instruction paired with its source index.
  SourceRef peekNext() const override {
    assert(hasNext());
    return SourceRef(TotalCounter, *Staging.front());
  }

  /// Add a new instruction.
  /// \param Inst Instruction to take ownership of and stage for consumption.
  void addInst(UniqueInst &&Inst) {
    InstStorage.emplace_back(std::move(Inst));
    Staging.push_back(InstStorage.back().get());
  }

  /// Add a recycled instruction.
  /// \param Inst Previously freed instruction to stage again.
  void addRecycledInst(Instruction *Inst) { Staging.push_back(Inst); }

  /// Advance to the next \a SourceRef.
  void updateNext() override;

  /// Mark the end of instruction stream.
  void endOfStream() { EOS = true; }

#ifndef NDEBUG
  /// Print statistic about instruction recycling stats.
  /// \param OS Output stream to write the statistics to.
  void printStatistic(raw_ostream &OS);
#endif
};

} // end namespace mca
} // end namespace llvm

#endif // LLVM_MCA_INCREMENTALSOURCEMGR_H
