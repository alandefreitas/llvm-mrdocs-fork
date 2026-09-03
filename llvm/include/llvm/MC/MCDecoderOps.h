//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Disassembler decoder state machine ops.
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCDECODEROPS_H
#define LLVM_MC_MCDECODEROPS_H

namespace llvm::MCD {

/// Opcodes for the TableGen-generated disassembler decoder state machine.
enum DecoderOps {
  OPC_Scope = 1, ///< Enter a scope; operand: uleb128 Size.
  OPC_SwitchField, ///< Switch on a bitfield (Start, Len, [Val, Size]...).
  OPC_CheckField, ///< Check a bitfield equals a value (Start, Len, Val).
  OPC_CheckPredicate, ///< Check a predicate; operand: uleb128 PIdx.
  OPC_Decode, ///< Decode an instruction; operands: uleb128 Opcode, uleb128 DIdx.
  OPC_SoftFail, ///< Soft-fail masks; operands: uleb128 PMask, uleb128 NMask.
};

} // namespace llvm::MCD

#endif
