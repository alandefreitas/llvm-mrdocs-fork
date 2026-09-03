//===- Reassociate.h - Reassociate binary expressions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass reassociates commutative expressions in an order that is designed
// to promote better constant propagation, GCSE, LICM, PRE, etc.
//
// For example: 4 + (x + 5) -> x + (4 + 5)
//
// In the implementation of this algorithm, constants are assigned rank = 0,
// function arguments are rank = 1, and other values are assigned ranks
// corresponding to the reverse post order traversal of current function
// (starting at 2), which effectively gives values in deep loops higher rank
// than values not in loops.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_REASSOCIATE_H
#define LLVM_TRANSFORMS_SCALAR_REASSOCIATE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Compiler.h"
#include <deque>

namespace llvm {

class APInt;
class BasicBlock;
class BinaryOperator;
class Function;
class Instruction;
class IRBuilderBase;
class Value;
struct OverflowTracking;

/// A private "module" namespace for types and utilities used by Reassociate.
/// These are implementation details and should not be used by clients.
namespace reassociate {

/// A value paired with its reassociation rank.
struct ValueEntry {
  /// Rank used to order operands during reassociation.
  unsigned Rank;
  /// The operand value being ranked.
  Value *Op;

  /// Construct a ranked value entry.
  /// @param R Rank assigned to the operand.
  /// @param O Operand value.
  ValueEntry(unsigned R, Value *O) : Rank(R), Op(O) {}
};

/// Compare two value entries by rank, ordering higher ranks first.
/// @param LHS Left-hand value entry.
/// @param RHS Right-hand value entry.
/// @return True if \p LHS should sort before \p RHS (higher rank first).
inline bool operator<(const ValueEntry &LHS, const ValueEntry &RHS) {
  return LHS.Rank > RHS.Rank; // Sort so that highest rank goes to start.
}

/// Utility class representing a base and exponent pair which form one
/// factor of some product.
struct Factor {
  /// Base value of this factor.
  Value *Base;
  /// Exponent applied to the base.
  unsigned Power;

  /// Construct a factor with the given base and power.
  /// @param Base Base value of the factor.
  /// @param Power Exponent applied to the base.
  Factor(Value *Base, unsigned Power) : Base(Base), Power(Power) {}
};

/// Operand of an XOR expression used during XOR optimization.
class XorOpnd;

} // end namespace reassociate

/// Reassociate commutative expressions.
class ReassociatePass : public OptionalPassInfoMixin<ReassociatePass> {
public:
  /// Ordered set of instructions queued for another optimization pass.
  using OrderedSet =
      SetVector<AssertingVH<Instruction>, std::deque<AssertingVH<Instruction>>>;

protected:
  /// Map from basic blocks to their reassociation ranks.
  DenseMap<BasicBlock *, unsigned> RankMap;
  /// Map from values to their reassociation ranks.
  DenseMap<AssertingVH<Value>, unsigned> ValueRankMap;
  /// Instructions queued for another optimization pass.
  OrderedSet RedoInsts;

  /// Maximum number of global reassociation attempts per pair.
  ///
  /// Arbitrary, but prevents quadratic behavior.
  static const unsigned GlobalReassociateLimit = 10;
  /// Number of binary operator opcodes tracked in PairMap.
  static const unsigned NumBinaryOps =
      Instruction::BinaryOpsEnd - Instruction::BinaryOpsBegin;

  /// Cached pairing of two values with a score for global reassociation.
  struct PairMapValue {
    /// First value in the paired expression.
    WeakVH Value1;
    /// Second value in the paired expression.
    WeakVH Value2;
    /// How often this value pair has been observed.
    unsigned Score;
    /// Return true if both value handles still refer to live values.
    /// @return True when both WeakVH entries are non-null.
    bool isValid() const { return Value1 && Value2; }
  };
  /// Per-opcode maps from value pairs to their observed reassociation scores.
  DenseMap<std::pair<Value *, Value *>, PairMapValue> PairMap[NumBinaryOps];

  /// Whether the pass has modified the function.
  bool MadeChange;
  /// Uniformity analysis used when optimizing for divergent control flow.
  UniformityInfo *UA = nullptr;

public:
  /// Run reassociation over the function.
  /// @param F Function whose commutative expressions may be reassociated.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// Run reassociation using an explicitly supplied uniformity analysis.
  /// @param F Function to optimize.
  /// @param UI Uniformity information for the function.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses runImpl(Function &F, UniformityInfo &UI);

private:
  void BuildRankMap(Function &F, ReversePostOrderTraversal<Function *> &RPOT);
  unsigned getRank(Value *V);
  void canonicalizeOperands(Instruction *I);
  void ReassociateExpression(BinaryOperator *I);
  void RewriteExprTree(BinaryOperator *I,
                       SmallVectorImpl<reassociate::ValueEntry> &Ops,
                       OverflowTracking Flags);
  Value *OptimizeExpression(BinaryOperator *I,
                            SmallVectorImpl<reassociate::ValueEntry> &Ops);
  Value *OptimizeAdd(Instruction *I,
                     SmallVectorImpl<reassociate::ValueEntry> &Ops);
  Value *OptimizeXor(Instruction *I,
                     SmallVectorImpl<reassociate::ValueEntry> &Ops);
  bool CombineXorOpnd(BasicBlock::iterator It, reassociate::XorOpnd *Opnd1,
                      APInt &ConstOpnd, Value *&Res);
  bool CombineXorOpnd(BasicBlock::iterator It, reassociate::XorOpnd *Opnd1,
                      reassociate::XorOpnd *Opnd2, APInt &ConstOpnd,
                      Value *&Res);
  Value *buildMinimalMultiplyDAG(IRBuilderBase &Builder,
                                 SmallVectorImpl<reassociate::Factor> &Factors);
  Value *OptimizeMul(BinaryOperator *I,
                     SmallVectorImpl<reassociate::ValueEntry> &Ops);
  Value *RemoveFactorFromExpression(Value *V, Value *Factor, DebugLoc DL);
  void EraseInst(Instruction *I);
  void RecursivelyEraseDeadInsts(Instruction *I, OrderedSet &Insts);
  void OptimizeInst(Instruction *I);
  Instruction *canonicalizeNegFPConstantsForOp(Instruction *I, Instruction *Op,
                                               Value *OtherOp);
  Instruction *canonicalizeNegFPConstants(Instruction *I);
  void BuildPairMap(ReversePostOrderTraversal<Function *> &RPOT);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_REASSOCIATE_H
