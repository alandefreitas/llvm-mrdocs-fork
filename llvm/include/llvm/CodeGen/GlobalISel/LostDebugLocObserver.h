//===----- llvm/CodeGen/GlobalISel/LostDebugLocObserver.h -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Tracks DebugLocs between checkpoints and verifies that they are transferred.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_GLOBALISEL_LOSTDEBUGLOCOBSERVER_H
#define LLVM_CODEGEN_GLOBALISEL_LOSTDEBUGLOCOBSERVER_H

#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
/// Tracks DebugLocs between checkpoints and verifies that they are transferred.
class LLVM_ABI LostDebugLocObserver : public GISelChangeObserver {
  StringRef DebugType;
  SmallSet<DebugLoc, 4> LostDebugLocs;
  SmallPtrSet<MachineInstr *, 4> PotentialMIsForDebugLocs;
  unsigned NumLostDebugLocs = 0;

public:
  /// Construct an observer that reports lost debug locations under \p DebugType.
  /// \param DebugType Debug type used when reporting lost locations.
  LostDebugLocObserver(StringRef DebugType) : DebugType(DebugType) {}

  /// Return the number of debug locations lost since construction.
  /// \return Number of debug locations lost since construction.
  unsigned getNumLostDebugLocs() const { return NumLostDebugLocs; }

  /// Assess whether debug locations have been lost since the last checkpoint.
  ///
  /// Typically this will be when a logical change has been completed such as
  /// the caller has finished replacing some instructions with alternatives.
  /// When CheckDebugLocs is true, the locations will be checked to see if any
  /// have been lost since the last checkpoint. When CheckDebugLocs is false,
  /// it will just reset ready for the next checkpoint without checking
  /// anything. This can be helpful to limit the detection to easy-to-fix
  /// portions of an algorithm before allowing more difficult ones.
  /// \param CheckDebugLocs If true, check for lost locations; if false, only
  ///        reset for the next checkpoint.
  void checkpoint(bool CheckDebugLocs = true);

  /// Record a newly created instruction for debug-location tracking.
  /// \param MI Instruction that was created and inserted.
  void createdInstr(MachineInstr &MI) override;
  /// Handle an instruction about to be erased, noting its debug locations.
  /// \param MI Instruction that is about to be erased.
  void erasingInstr(MachineInstr &MI) override;
  /// Handle an instruction about to be mutated for debug-location tracking.
  /// \param MI Instruction that is about to be mutated.
  void changingInstr(MachineInstr &MI) override;
  /// Handle an instruction that was mutated for debug-location tracking.
  /// \param MI Instruction that was mutated.
  void changedInstr(MachineInstr &MI) override;

private:
  void analyzeDebugLocations();
};

} // namespace llvm
#endif // LLVM_CODEGEN_GLOBALISEL_LOSTDEBUGLOCOBSERVER_H
