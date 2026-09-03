//===---------------------- EntryStage.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the Entry stage of an instruction pipeline.  Its sole
/// purpose in life is to pick instructions in sequence and move them to the
/// next pipeline stage.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_STAGES_ENTRYSTAGE_H
#define LLVM_MCA_STAGES_ENTRYSTAGE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MCA/SourceMgr.h"
#include "llvm/MCA/Stages/Stage.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace mca {

/// A pipeline stage that fetches instructions from a source manager.
///
/// Its sole purpose is to pick instructions in sequence and move them to the
/// next pipeline stage.
class LLVM_ABI EntryStage final : public Stage {
  InstRef CurrentInstruction;
  SmallVector<std::unique_ptr<Instruction>, 16> Instructions;
  SourceMgr &SM;
  unsigned NumRetired;

  // Updates the program counter, and sets 'CurrentInstruction'.
  Error getNextInstruction();

  EntryStage(const EntryStage &Other) = delete;
  EntryStage &operator=(const EntryStage &Other) = delete;

public:
  /// Construct an entry stage that reads instructions from \p SM.
  /// \param SM Source manager that supplies the input instruction stream.
  EntryStage(SourceMgr &SM) : SM(SM), NumRetired(0) {}

  /// Returns true if the current instruction can move to the next stage.
  /// \param IR Instruction reference (unused; availability uses the current
  ///        instruction held by this stage).
  /// \return True if the current instruction can move to the next stage.
  bool isAvailable(const InstRef &IR) const override;

  /// Returns true if there is a pending instruction or more source input.
  /// \return True if there is a pending instruction or more source input.
  bool hasWorkToComplete() const override;

  /// Moves the current instruction to the next stage and fetches another.
  /// \param IR Instruction reference (unused; this stage advances its own
  ///        current instruction).
  /// \return Success, or an error if handing off or fetching fails.
  Error execute(InstRef &IR) override;

  /// Fetches the next instruction at the start of a cycle if needed.
  /// \return Success, or an error if fetching the next instruction fails.
  Error cycleStart() override;

  /// Fetches the next instruction after the pipeline resumes from a pause.
  /// \return Success, or an error if fetching the next instruction fails.
  Error cycleResume() override;

  /// Drops retired instructions from the internal instruction buffer.
  /// \return Success after pruning retired instructions from the buffer.
  Error cycleEnd() override;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_STAGES_ENTRYSTAGE_H
