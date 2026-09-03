//===- GenericSSAContext.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the little GenericSSAContext<X> template class
/// that can be used to implement IR analyses as templates.
/// Specializing these templates allows the analyses to be used over
/// both LLVM IR and Machine IR.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_GENERICSSACONTEXT_H
#define LLVM_ADT_GENERICSSACONTEXT_H

#include "llvm/Support/Printable.h"

namespace llvm {

/// Base class for dominator trees over graph nodes.
///
/// This class is a generic template over graph nodes. It is instantiated for
/// various graphs in the LLVM IR or in the code generator.
/// @tparam NodeT Graph node type (typically a basic-block type).
/// @tparam IsPostDom Set to true for a post-dominator tree; false for a dominator tree.
template <typename NodeT, bool IsPostDom> class DominatorTreeBase;
template <typename> class SmallVectorImpl;

/// Namespace for SSA/intrinsic helpers used by GenericSSAContext.
namespace Intrinsic {
/// Identifier for an LLVM intrinsic function.
using ID = unsigned;
}

/// Traits providing IR-specific type aliases for \c GenericSSAContext.
///
/// Specializations should provide the types used by the template
/// GenericSSAContext below.
template <typename _FunctionT> struct GenericSSATraits;

/// Thin IR-agnostic context for SSA analyses over LLVM IR or Machine IR.
///
/// Ideally this should have been a stateless traits class. But the print methods
/// for Machine IR need access to the owning function. So we track that state in
/// the template itself.
///
/// We use FunctionT as a template argument and not GenericSSATraits to allow
/// forward declarations using well-known typenames.
template <typename _FunctionT> class GenericSSAContext {
  using SSATraits = GenericSSATraits<_FunctionT>;
  const typename SSATraits::FunctionT *F;

public:
  /// Handle to an SSA value (pointer-like; Machine IR has no Value object).
  using ValueRefT = typename SSATraits::ValueRefT;

  /// Const SSA value reference (const binds to the pointee, not the pointer).
  using ConstValueRefT = typename SSATraits::ConstValueRefT;

  /// Null value reference for this IR (default-constructed ValueRefT).
  static constexpr ValueRefT ValueRefNull = {};

  /// Instruction type that defines one or more SSA values.
  using InstructionT = typename SSATraits::InstructionT;

  /// Use edge from a defining instruction to a using instruction.
  using UseT = typename SSATraits::UseT;

  /// Basic-block type: a sequence of instructions and a CFG node.
  using BlockT = typename SSATraits::BlockT;

  /// Function type: a CFG with arguments and return values.
  using FunctionT = typename SSATraits::FunctionT;

  /// Dominator tree over this IR's basic blocks.
  using DominatorTreeT = DominatorTreeBase<BlockT, false>;

  /// Construct a context not bound to any function.
  GenericSSAContext() = default;
  /// Construct a context bound to function \p F.
  /// @param F Function this context will print and query.
  GenericSSAContext(const FunctionT *F) : F(F) {}

  /// Return the function this context is bound to, or null.
  /// @return Function this context is bound to, or null.
  const FunctionT *getFunction() const { return F; }

  /// Return the intrinsic ID for instruction \p I, if it is an intrinsic.
  /// @param I Instruction to classify.
  /// @return Intrinsic ID for \p I, or a non-intrinsic sentinel if none.
  static Intrinsic::ID getIntrinsicID(const InstructionT &I);

  /// Append values defined in \p block to \p defs.
  /// @param defs Output list of defined value references.
  /// @param block Basic block whose definitions are collected.
  static void appendBlockDefs(SmallVectorImpl<ValueRefT> &defs, BlockT &block);
  /// Append const value references defined in \p block to \p defs.
  /// @param defs Output list of defined const value references.
  /// @param block Basic block whose definitions are collected.
  static void appendBlockDefs(SmallVectorImpl<ConstValueRefT> &defs,
                              const BlockT &block);

  /// Append terminator instructions of \p block to \p terms.
  /// @param terms Output list of terminator instructions.
  /// @param block Basic block whose terminators are collected.
  static void appendBlockTerms(SmallVectorImpl<InstructionT *> &terms,
                               BlockT &block);
  /// Append terminator instructions of const \p block to \p terms.
  /// @param terms Output list of const terminator instructions.
  /// @param block Basic block whose terminators are collected.
  static void appendBlockTerms(SmallVectorImpl<const InstructionT *> &terms,
                               const BlockT &block);

  /// Return true if \p Instr is a phi of only constant or undef values.
  /// @param Instr Instruction to classify.
  /// @return True if \p Instr is a phi of only constant or undef values.
  static bool isConstantOrUndefValuePhi(const InstructionT &Instr);

  /// Return true if \p V is always uniform.
  ///
  /// Always-uniform values will not be added to UniformValues. For IR this
  /// identifies constants and globals; for MIR it returns false (all
  /// registers are tracked).
  /// @param V Value to classify.
  /// @return True if \p V is always uniform.
  static bool isAlwaysUniform(ConstValueRefT V);

  /// Return the basic block that defines \p value, or null if none.
  /// @param value Value whose defining block is requested.
  /// @return Basic block that defines \p value, or null if none.
  const BlockT *getDefBlock(ConstValueRefT value) const;

  /// Return a printable view of basic block \p block.
  /// @param block Basic block to print.
  /// @return Printable view of \p block for streaming.
  Printable print(const BlockT *block) const;
  /// Return a printable operand form of basic block \p BB.
  /// @param BB Basic block to print as an operand.
  /// @return Printable operand form of \p BB for streaming.
  Printable printAsOperand(const BlockT *BB) const;
  /// Return a printable view of instruction \p inst.
  /// @param inst Instruction to print.
  /// @return Printable view of \p inst for streaming.
  Printable print(const InstructionT *inst) const;
  /// Return a printable view of value \p value.
  /// @param value Value to print.
  /// @return Printable view of \p value for streaming.
  Printable print(ConstValueRefT value) const;
};
} // namespace llvm

#endif // LLVM_ADT_GENERICSSACONTEXT_H
