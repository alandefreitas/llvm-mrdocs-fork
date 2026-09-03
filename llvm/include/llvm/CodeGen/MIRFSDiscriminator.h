//===----- MIRFSDiscriminator.h: MIR FS Discriminator Support --0-- c++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the supporting functions for adding Machine level IR
// Flow Sensitive discriminators to the instruction debug information. With
// this, a cloned machine instruction in a different MachineBasicBlock will
// have its own discriminator value. This is done in a MIRAddFSDiscriminators
// pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIRFSDISCRIMINATOR_H
#define LLVM_CODEGEN_MIRFSDISCRIMINATOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/Discriminator.h"

#include <cassert>
#include <cstdint>

namespace llvm {
class MachineFunction;

using namespace sampleprof;
/// MIR pass that adds flow-sensitive discriminators to instruction debug info.
class LLVM_ABI MIRAddFSDiscriminators : public MachineFunctionPass {
  MachineFunction *MF = nullptr;
  FSDiscriminatorPass Pass;
  unsigned LowBit;
  unsigned HighBit;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct a pass that adds MIR flow-sensitive discriminators.
  ///
  /// \param P Sequence number this pass is called with, starting from 1.
  MIRAddFSDiscriminators(FSDiscriminatorPass P = FSDiscriminatorPass::Pass1)
      : MachineFunctionPass(ID), Pass(P) {
    LowBit = getFSPassBitBegin(P);
    HighBit = getFSPassBitEnd(P);
    assert(LowBit < HighBit && "HighBit needs to be greater than Lowbit");
  }

  /// Return the name of this pass.
  ///
  /// \return The name of this pass.
  StringRef getPassName() const override {
    return "Add FS discriminators in MIR";
  }

  /// getNumFSBBs() - Return the number of machine BBs that have FS samples.
  ///
  /// \return The number of machine basic blocks that have FS samples.
  unsigned getNumFSBBs();

  /// getNumFSSamples() - Return the number of samples that have flow sensitive
  /// values.
  ///
  /// \return The number of samples that have flow-sensitive values.
  uint64_t getNumFSSamples();

  /// getMachineFunction - Return the current machine function.
  ///
  /// \return The current machine function, or nullptr if none is set.
  const MachineFunction *getMachineFunction() const { return MF; }

private:
  bool runOnMachineFunction(MachineFunction &) override;
};

} // namespace llvm

#endif // LLVM_CODEGEN_MIRFSDISCRIMINATOR_H
