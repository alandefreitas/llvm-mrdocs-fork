//===- MachineSSAContext.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file declares a specialization of the GenericSSAContext<X>
/// template class for Machine IR.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINESSACONTEXT_H
#define LLVM_CODEGEN_MACHINESSACONTEXT_H

#include "llvm/ADT/GenericSSAContext.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/Support/Printable.h"

namespace llvm {
class MachineInstr;
class MachineFunction;
class Register;

/// Return the instructions in machine basic block \p BB.
/// @param BB Machine basic block whose instructions are returned.
/// @return An iterator range over the instructions in \p BB.
inline auto instrs(const MachineBasicBlock &BB) { return BB.instrs(); }

/// Traits providing Machine IR type aliases for \c GenericSSAContext.
template <> struct GenericSSATraits<MachineFunction> {
  /// Basic-block type: a sequence of instructions and a CFG node.
  using BlockT = MachineBasicBlock;
  /// Function type: a CFG with arguments and return values.
  using FunctionT = MachineFunction;
  /// Instruction type that defines one or more SSA values.
  using InstructionT = MachineInstr;
  /// Handle to an SSA value (a register; Machine IR has no Value object).
  using ValueRefT = Register;
  /// Const SSA value reference (same as ValueRefT for Machine IR).
  using ConstValueRefT = Register;
  /// Use edge from a defining instruction to a using instruction.
  using UseT = MachineOperand;
};

/// SSA context specialized for Machine IR functions.
using MachineSSAContext = GenericSSAContext<MachineFunction>;
} // namespace llvm

#endif // LLVM_CODEGEN_MACHINESSACONTEXT_H
