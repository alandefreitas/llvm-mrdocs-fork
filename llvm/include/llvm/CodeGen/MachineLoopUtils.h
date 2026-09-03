//=- MachineLoopUtils.h - Helper functions for manipulating loops -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINELOOPUTILS_H
#define LLVM_CODEGEN_MACHINELOOPUTILS_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class MachineBasicBlock;
class MachineRegisterInfo;
class TargetInstrInfo;

/// Direction in which to peel a single-block loop.
enum LoopPeelDirection {
  LPD_Front, ///< Peel the first iteration of the loop.
  LPD_Back   ///< Peel the last iteration of the loop.
};

/// Peels a single block loop.
///
/// Loop must have two successors, one of which must be itself. Similarly it
/// must have two predecessors, one of which must be itself.
///
/// The loop block is copied and inserted into the CFG such that two copies of
/// the loop follow on from each other. The copy is inserted either before or
/// after the loop based on Direction.
///
/// Phis are updated and an unconditional branch inserted at the end of the
/// clone so as to execute a single iteration.
///
/// The trip count of Loop is not updated.
///
/// \param Direction Whether to peel the first or last iteration.
/// \param Loop The single-block loop to peel.
/// \param MRI Register information used when updating phis and clones.
/// \param TII Target instruction info used to insert the exit branch.
/// \return The newly created peeled copy of the loop block.
LLVM_ABI MachineBasicBlock *PeelSingleBlockLoop(LoopPeelDirection Direction,
                                                MachineBasicBlock *Loop,
                                                MachineRegisterInfo &MRI,
                                                const TargetInstrInfo *TII);

} // namespace llvm

#endif // LLVM_CODEGEN_MACHINELOOPUTILS_H
