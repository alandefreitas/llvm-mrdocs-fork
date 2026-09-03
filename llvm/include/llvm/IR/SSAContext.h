//===- SSAContext.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file declares a specialization of the GenericSSAContext<X>
/// class template for LLVM IR.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_SSACONTEXT_H
#define LLVM_IR_SSACONTEXT_H

#include "llvm/ADT/GenericSSAContext.h"
#include "llvm/IR/BasicBlock.h"

namespace llvm {
class BasicBlock;
class Function;
class Instruction;
class Value;

/// Return a range over the instructions in basic block \p BB.
/// @param BB Basic block whose instructions are iterated.
/// @return Range covering the instructions in \p BB.
inline auto instrs(const BasicBlock &BB) {
  return llvm::make_range(BB.begin(), BB.end());
}

/// \c GenericSSATraits specialization mapping LLVM IR types for \c Function.
template <> struct GenericSSATraits<Function> {
  /// Basic-block type: a sequence of instructions and a CFG node.
  using BlockT = BasicBlock;
  /// Function type: a CFG with arguments and return values.
  using FunctionT = Function;
  /// Instruction type that defines one or more SSA values.
  using InstructionT = Instruction;
  /// Handle to an SSA value (pointer to \c Value).
  using ValueRefT = Value *;
  /// Const SSA value reference (const binds to the pointee, not the pointer).
  using ConstValueRefT = const Value *;
  /// Use edge from a defining instruction to a using instruction.
  using UseT = Use;
};

using SSAContext = GenericSSAContext<Function>;

} // namespace llvm

#endif // LLVM_IR_SSACONTEXT_H
