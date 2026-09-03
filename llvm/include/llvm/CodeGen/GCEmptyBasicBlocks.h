//===-- GCEmptyBasicBlocks.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GCEMPTYBASICBLOCKS_H
#define LLVM_CODEGEN_GCEMPTYBASICBLOCKS_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that garbage-collects empty machine basic blocks.
///
/// Empty basic blocks (basic blocks without real code) appear as the result of
/// optimization passes removing instructions. These blocks confuse profile
/// analysis (e.g., basic block sections) since they will share the address of
/// their fallthrough blocks.
class GCEmptyBasicBlocksPass
    : public OptionalPassInfoMixin<GCEmptyBasicBlocksPass> {
public:
  /// Remove empty basic blocks from \p MF.
  /// \param MF Machine function whose empty blocks are garbage-collected.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after garbage-collecting empty basic
  /// blocks.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_GCEMPTYBASICBLOCKS_H
