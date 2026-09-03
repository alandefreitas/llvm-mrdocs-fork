//===- JumpTableToSwitch.h - ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_JUMP_TABLE_TO_SWITCH_H
#define LLVM_TRANSFORMS_SCALAR_JUMP_TABLE_TO_SWITCH_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass that replaces jump tables of small functions with switches.
class JumpTableToSwitchPass
    : public OptionalPassInfoMixin<JumpTableToSwitchPass> {
  // Necessary until we switch to GUIDs as metadata, after which we can drop it.
  const bool InLTO;

public:
  /// Construct a JumpTableToSwitch pass.
  /// @param InLTO Whether the pass is running in an LTO post-link pipeline.
  explicit JumpTableToSwitchPass(bool InLTO = false) : InLTO(InLTO) {}
  /// Run the pass over the function.
  /// @param F Function to convert jump tables in.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_JUMP_TABLE_TO_SWITCH_H
