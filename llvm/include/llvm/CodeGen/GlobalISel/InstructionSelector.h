//===- llvm/CodeGen/GlobalISel/InstructionSelector.h ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file declares the API for the instruction selector.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_INSTRUCTIONSELECTOR_H
#define LLVM_CODEGEN_GLOBALISEL_INSTRUCTIONSELECTOR_H

#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutor.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class GISelObserverWrapper;

/// Selects (possibly generic) machine instructions to target-specific opcodes.
class LLVM_ABI InstructionSelector : public GIMatchTableExecutor {
public:
  /// Destroy this instruction selector.
  ~InstructionSelector() override;

  /// Select instruction \p I to use only target-specific opcodes.
  ///
  /// It is OK to insert multiple instructions, but they cannot be generic
  /// pre-isel instructions.
  ///
  /// \param I Possibly generic instruction to select.
  /// \returns whether selection succeeded.
  /// \pre  I.getParent() && I.getParent()->getParent()
  /// \post
  ///   if returns true:
  ///     for I in all mutated/inserted instructions:
  ///       !isPreISelGenericOpcode(I.getOpcode())
  virtual bool select(MachineInstr &I) = 0;

  /// Remark emitter used to report instruction selection diagnostics.
  MachineOptimizationRemarkEmitter *MORE = nullptr;

  /// Note: InstructionSelect does not track changed instructions.
  /// changingInstr() and changedInstr() will never be called on these
  /// observers.
  GISelObserverWrapper *AllObservers = nullptr;

protected:
  /// Render a G_FRAME_INDEX operand into \p MIB.
  ///
  /// \param MIB Instruction being built that receives the frame-index operand.
  /// \param MI Source G_FRAME_INDEX instruction providing the operand.
  /// \param OpIdx Operand index; must be -1 for a full-operand render.
  void renderFrameIndex(MachineInstrBuilder &MIB, const MachineInstr &MI,
                        int OpIdx) const;
};
} // namespace llvm

#endif
