//===------------ MachineStableHash.h - MIR Stable Hashing Utilities ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Stable hashing for MachineInstr and MachineOperand. Useful or getting a
// hash across runs, modules, etc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINESTABLEHASH_H
#define LLVM_CODEGEN_MACHINESTABLEHASH_H

#include "llvm/ADT/StableHashing.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MachineOperand;

/// Compute a stable hash of machine operand \p MO.
///
/// \param MO The machine operand to hash.
/// \return A stable hash of the machine operand.
LLVM_ABI stable_hash stableHashValue(const MachineOperand &MO);

/// Compute a stable hash of machine instruction \p MI.
///
/// Returns 0 if no stable hash could be computed. By default, virtual register
/// definitions and memory operands are ignored so the hash is useful for CSE.
///
/// \param MI The machine instruction to hash.
/// \param HashVRegs Whether to include virtual register definitions.
/// \param HashConstantPoolIndices Whether to include constant pool indices.
/// \param HashMemOperands Whether to include memory operands.
/// \return A stable hash of the machine instruction, or 0 if none could be
/// computed.
LLVM_ABI stable_hash stableHashValue(const MachineInstr &MI,
                                     bool HashVRegs = false,
                                     bool HashConstantPoolIndices = false,
                                     bool HashMemOperands = false);

/// Compute a stable hash of machine basic block \p MBB.
///
/// \param MBB The machine basic block to hash.
/// \return A stable hash of the machine basic block.
LLVM_ABI stable_hash stableHashValue(const MachineBasicBlock &MBB);

/// Compute a stable hash of machine function \p MF.
///
/// \param MF The machine function to hash.
/// \return A stable hash of the machine function.
LLVM_ABI stable_hash stableHashValue(const MachineFunction &MF);

} // namespace llvm

#endif
