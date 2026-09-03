//===- llvm/CodeGen/MachineBlockPlacement.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEBLOCKPLACEMENT_H
#define LLVM_CODEGEN_MACHINEBLOCKPLACEMENT_H

#include "llvm/CodeGen/MachinePassManager.h"

namespace llvm {

/// New PM pass that places basic blocks based on branch probabilities.
///
/// Reorders machine basic blocks to favor hot paths while preserving CFG
/// structure when probabilities do not strongly suggest otherwise.
class MachineBlockPlacementPass
    : public OptionalPassInfoMixin<MachineBlockPlacementPass> {

  bool AllowTailMerge = true;

public:
  /// Construct a MachineBlockPlacement pass.
  /// \param AllowTailMerge Whether to allow tail merging during placement.
  MachineBlockPlacementPass(bool AllowTailMerge)
      : AllowTailMerge(AllowTailMerge) {}

  /// Place basic blocks in \p MF using branch probability estimates.
  /// \param MF Machine function whose blocks are reordered.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after placing basic blocks.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Print this pass and its options as a pipeline string.
  /// \param OS Stream to write the pipeline string to.
  /// \param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName) const;
};

/// New PM pass that collects statistics about basic block placement.
///
/// Reports how placement interacts with branch probabilities and block
/// frequency information without modifying the function.
class MachineBlockPlacementStatsPass
    : public RequiredPassInfoMixin<MachineBlockPlacementStatsPass> {

public:
  /// Collect basic block placement statistics for \p MF.
  /// \param MF Machine function whose placement statistics are gathered.
  /// \param MFAM Machine function analysis manager providing required analyses.
  /// \return The set of analyses preserved after collecting placement statistics.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINEBLOCKPLACEMENT_H
